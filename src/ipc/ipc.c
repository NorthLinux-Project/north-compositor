#define _GNU_SOURCE
#include "ipc/ipc.h"

#include "core/server.h"
#include "ipc/json.h"
#include "output/output.h"
#include "shell/view.h"
#include "shell/workspace.h"
#include "util/arena.h"
#include "util/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/box.h>

#define IPC_CLIENT_BUF_INIT 4096

static void ipc_client_destroy(struct north_ipc_client *client)
{
    if (!client) {
        return;
    }
    wl_list_remove(&client->link);
    if (client->event_source) {
        wl_event_source_remove(client->event_source);
        client->event_source = NULL;
    }
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
    free(client->read_buf);
    free(client);
}

static bool ipc_client_send(struct north_ipc_client *client, uint32_t type, const char *payload,
                            size_t payload_len)
{
    if (!client || client->fd < 0) {
        return false;
    }

    struct north_ipc_header hdr;
    memcpy(hdr.magic, NORTH_IPC_MAGIC, 4);
    hdr.type = type;
    hdr.payload_len = (uint32_t)payload_len;

    size_t total = NORTH_IPC_HEADER_SIZE + payload_len;
    char *buf = malloc(total);
    if (!buf) {
        return false;
    }

    memcpy(buf, &hdr, NORTH_IPC_HEADER_SIZE);
    if (payload && payload_len > 0) {
        memcpy(buf + NORTH_IPC_HEADER_SIZE, payload, payload_len);
    }

    size_t sent = 0;
    while (sent < total) {
        ssize_t ret = write(client->fd, buf + sent, total - sent);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            free(buf);
            return false;
        }
        sent += (size_t)ret;
    }

    free(buf);
    return sent == total;
}

static void ipc_send_reply_json(struct north_ipc_client *client, uint32_t type, const char *json)
{
    size_t len = json ? strlen(json) : 0;
    ipc_client_send(client, type, json, len);
}

static void ipc_send_error(struct north_ipc_client *client, const char *msg)
{
    Arena arena = arena_create(1024);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);
    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "error", msg ? msg : "unknown error");
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    ipc_client_send(client, NORTH_IPC_REPLY_ERROR, json, len);
    arena_destroy(&arena);
}

static void ipc_send_success(struct north_ipc_client *client, const char *status)
{
    Arena arena = arena_create(1024);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);
    json_builder_object_begin(&jb);
    json_builder_key_bool(&jb, "success", true);
    if (status) {
        json_builder_key_string(&jb, "status", status);
    }
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    ipc_client_send(client, NORTH_IPC_REPLY_SUCCESS, json, len);
    arena_destroy(&arena);
}

static void ipc_handle_get_workspaces(struct north_ipc_client *client)
{
    struct north_server *server = client->ipc->server;
    Arena arena = arena_create(16384);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);

    json_builder_array_begin(&jb);
    struct north_output *out;
    wl_list_for_each(out, &server->outputs, link)
    {
        struct north_workspace *ws;
        wl_list_for_each(ws, &out->workspaces, link)
        {
            json_builder_object_begin(&jb);
            json_builder_key_int(&jb, "index", ws->index);
            json_builder_key_string(&jb, "name", ws->name);
            json_builder_key_bool(&jb, "active", (out->active_workspace == ws));
            json_builder_key_string(&jb, "output",
                                    out->wlr_output ? out->wlr_output->name : "unknown");
            json_builder_object_end(&jb);
        }
    }
    json_builder_array_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    ipc_send_reply_json(client, NORTH_IPC_REPLY_SUCCESS, json);
    arena_destroy(&arena);
}

static const char *view_state_to_string(enum north_view_state state)
{
    switch (state) {
    case VIEW_NORMAL:
        return "normal";
    case VIEW_MAXIMIZED:
        return "maximized";
    case VIEW_FULLSCREEN:
        return "fullscreen";
    case VIEW_SNAPPED:
        return "snapped";
    default:
        return "unknown";
    }
}

static void ipc_handle_get_views(struct north_ipc_client *client)
{
    struct north_server *server = client->ipc->server;
    struct wlr_surface *focused_surface = server->seat.wlr_seat->keyboard_state.focused_surface;

    Arena arena = arena_create(32768);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);

    json_builder_array_begin(&jb);
    struct north_view *view;
    wl_list_for_each(view, &server->views, link)
    {
        const char *title = NULL;
        const char *app_id = NULL;
        if (view->type == NORTH_VIEW_XDG && view->xdg_toplevel) {
            title = view->xdg_toplevel->title;
            app_id = view->xdg_toplevel->app_id;
        } else if (view->type == NORTH_VIEW_XWAYLAND && view->xwayland_surface) {
            title = view->xwayland_surface->title;
            app_id = view->xwayland_surface->class;
        }

        bool focused = (view->surface && view->surface == focused_surface);

        json_builder_object_begin(&jb);
        json_builder_key_int(&jb, "id", (int64_t)(uintptr_t)view);
        json_builder_key_string(&jb, "title", title ? title : "");
        json_builder_key_string(&jb, "app_id", app_id ? app_id : "");
        json_builder_key_int(&jb, "workspace", view->workspace ? view->workspace->index : -1);
        json_builder_key_string(&jb, "state", view_state_to_string(view->state));
        json_builder_key_bool(&jb, "focused", focused);

        json_builder_key(&jb, "geometry");
        json_builder_object_begin(&jb);
        json_builder_key_int(&jb, "x", view->geometry.x);
        json_builder_key_int(&jb, "y", view->geometry.y);
        json_builder_key_int(&jb, "width", view->geometry.width);
        json_builder_key_int(&jb, "height", view->geometry.height);
        json_builder_object_end(&jb);

        json_builder_object_end(&jb);
    }
    json_builder_array_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    ipc_send_reply_json(client, NORTH_IPC_REPLY_SUCCESS, json);
    arena_destroy(&arena);
}

static void ipc_handle_get_outputs(struct north_ipc_client *client)
{
    struct north_server *server = client->ipc->server;
    Arena arena = arena_create(16384);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);

    json_builder_array_begin(&jb);
    struct north_output *out;
    wl_list_for_each(out, &server->outputs, link)
    {
        json_builder_object_begin(&jb);
        json_builder_key_string(&jb, "name", out->wlr_output ? out->wlr_output->name : "unknown");
        json_builder_key_string(&jb, "make", out->wlr_output ? out->wlr_output->make : "unknown");
        json_builder_key_string(&jb, "model", out->wlr_output ? out->wlr_output->model : "unknown");
        json_builder_key_double(&jb, "scale", out->wlr_output ? out->wlr_output->scale : 1.0);
        json_builder_key_int(&jb, "active_workspace",
                             out->active_workspace ? out->active_workspace->index : -1);

        json_builder_key(&jb, "usable_area");
        json_builder_object_begin(&jb);
        json_builder_key_int(&jb, "x", out->usable_area.x);
        json_builder_key_int(&jb, "y", out->usable_area.y);
        json_builder_key_int(&jb, "width", out->usable_area.width);
        json_builder_key_int(&jb, "height", out->usable_area.height);
        json_builder_object_end(&jb);

        json_builder_object_end(&jb);
    }
    json_builder_array_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    ipc_send_reply_json(client, NORTH_IPC_REPLY_SUCCESS, json);
    arena_destroy(&arena);
}

static void ipc_handle_get_version(struct north_ipc_client *client)
{
    Arena arena = arena_create(1024);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);
    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "compositor", "north-compositor");
    json_builder_key_string(&jb, "version", "0.1.0");
    json_builder_key_string(&jb, "wlroots", "0.20.2");
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    ipc_send_reply_json(client, NORTH_IPC_REPLY_SUCCESS, json);
    arena_destroy(&arena);
}

static struct north_view *get_focused_view(struct north_server *server)
{
    struct wlr_surface *focused = server->seat.wlr_seat->keyboard_state.focused_surface;
    if (!focused) {
        return NULL;
    }
    struct north_view *v;
    wl_list_for_each(v, &server->views, link)
    {
        if (v->surface == focused) {
            return v;
        }
    }
    return NULL;
}

static void ipc_handle_command(struct north_ipc_client *client, const char *cmd)
{
    if (!cmd || *cmd == '\0') {
        ipc_send_error(client, "empty command");
        return;
    }

    struct north_server *server = client->ipc->server;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", cmd);

    char *verb = strtok(buf, " \t\r\n");
    if (!verb) {
        ipc_send_error(client, "invalid command format");
        return;
    }

    if (strcmp(verb, "workspace") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (!arg) {
            ipc_send_error(client, "missing workspace index");
            return;
        }
        int target = atoi(arg);
        if (target > 0) {
            target -= 1;
        }
        struct north_output *out =
            output_at(server, server->seat.cursor->x, server->seat.cursor->y);
        if (!out && !wl_list_empty(&server->outputs)) {
            out = wl_container_of(server->outputs.next, out, link);
        }
        if (!out) {
            ipc_send_error(client, "no active output");
            return;
        }
        workspace_switch(out, target);
        ipc_send_success(client, "switched workspace");
        return;
    }

    if (strcmp(verb, "move-to-workspace") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (!arg) {
            ipc_send_error(client, "missing workspace index");
            return;
        }
        int target = atoi(arg);
        if (target > 0) {
            target -= 1;
        }
        struct north_view *v = get_focused_view(server);
        if (!v) {
            ipc_send_error(client, "no focused window");
            return;
        }
        workspace_move_view(v, target);
        ipc_send_success(client, "moved view to workspace");
        return;
    }

    if (strcmp(verb, "focus") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (!arg || strcmp(arg, "next") == 0) {
            view_focus_next(server);
            ipc_send_success(client, "focused next view");
            return;
        }
        if (strcmp(arg, "prev") == 0) {
            view_focus_prev(server);
            ipc_send_success(client, "focused prev view");
            return;
        }
        ipc_send_error(client, "unknown focus direction (use 'next' or 'prev')");
        return;
    }

    if (strcmp(verb, "close") == 0) {
        struct north_view *v = get_focused_view(server);
        if (!v) {
            ipc_send_error(client, "no focused window");
            return;
        }
        view_close(v);
        ipc_send_success(client, "closed view");
        return;
    }

    if (strcmp(verb, "maximize") == 0) {
        struct north_view *v = get_focused_view(server);
        if (!v) {
            ipc_send_error(client, "no focused window");
            return;
        }
        view_set_maximized(v, v->state != VIEW_MAXIMIZED);
        ipc_send_success(client, "toggled maximize");
        return;
    }

    if (strcmp(verb, "fullscreen") == 0) {
        struct north_view *v = get_focused_view(server);
        if (!v) {
            ipc_send_error(client, "no focused window");
            return;
        }
        view_set_fullscreen(v, v->state != VIEW_FULLSCREEN);
        ipc_send_success(client, "toggled fullscreen");
        return;
    }

    if (strcmp(verb, "reload") == 0) {
        ipc_broadcast_config_event(server, "reload");
        ipc_send_success(client, "configuration reload signaled");
        return;
    }

    if (strcmp(verb, "quit") == 0) {
        ipc_send_success(client, "quitting compositor");
        wl_display_terminate(server->display);
        return;
    }

    ipc_send_error(client, "unknown command");
}

static void ipc_handle_subscribe(struct north_ipc_client *client, const char *payload,
                                 size_t payload_len)
{
    Arena arena = arena_create(4096);
    JsonValue *root = json_parse(&arena, payload, payload_len);

    if (root && root->type == JSON_ARRAY) {
        for (size_t i = 0; i < root->array_val.count; i++) {
            const char *evt = json_string_value(root->array_val.items[i]);
            if (!evt) {
                continue;
            }
            if (strcmp(evt, "workspace") == 0) {
                client->subscribed_events |= NORTH_IPC_SUB_WORKSPACE;
            } else if (strcmp(evt, "window") == 0) {
                client->subscribed_events |= NORTH_IPC_SUB_WINDOW;
            } else if (strcmp(evt, "output") == 0) {
                client->subscribed_events |= NORTH_IPC_SUB_OUTPUT;
            } else if (strcmp(evt, "config") == 0) {
                client->subscribed_events |= NORTH_IPC_SUB_CONFIG;
            } else if (strcmp(evt, "all") == 0) {
                client->subscribed_events |= (NORTH_IPC_SUB_WORKSPACE | NORTH_IPC_SUB_WINDOW |
                                              NORTH_IPC_SUB_OUTPUT | NORTH_IPC_SUB_CONFIG);
            }
        }
    } else {
        client->subscribed_events |= (NORTH_IPC_SUB_WORKSPACE | NORTH_IPC_SUB_WINDOW |
                                      NORTH_IPC_SUB_OUTPUT | NORTH_IPC_SUB_CONFIG);
    }

    ipc_send_success(client, "subscribed");
    arena_destroy(&arena);
}

static void ipc_process_message(struct north_ipc_client *client, uint32_t type, const char *payload,
                                size_t payload_len)
{
    switch (type) {
    case NORTH_IPC_MSG_COMMAND:
        ipc_handle_command(client, payload);
        break;
    case NORTH_IPC_MSG_GET_WORKSPACES:
        ipc_handle_get_workspaces(client);
        break;
    case NORTH_IPC_MSG_GET_VIEWS:
        ipc_handle_get_views(client);
        break;
    case NORTH_IPC_MSG_GET_OUTPUTS:
        ipc_handle_get_outputs(client);
        break;
    case NORTH_IPC_MSG_GET_VERSION:
        ipc_handle_get_version(client);
        break;
    case NORTH_IPC_MSG_SUBSCRIBE:
        ipc_handle_subscribe(client, payload, payload_len);
        break;
    default:
        ipc_send_error(client, "unknown message type");
        break;
    }
}

static int ipc_handle_client_event(int fd, uint32_t mask, void *data)
{
    (void)fd;
    struct north_ipc_client *client = data;

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        ipc_client_destroy(client);
        return 0;
    }

    if (mask & WL_EVENT_READABLE) {
        if (client->read_len >= client->read_cap) {
            size_t new_cap = client->read_cap * 2;
            char *new_buf = realloc(client->read_buf, new_cap);
            if (!new_buf) {
                ipc_client_destroy(client);
                return 0;
            }
            client->read_buf = new_buf;
            client->read_cap = new_cap;
        }

        ssize_t n = read(client->fd, client->read_buf + client->read_len,
                         client->read_cap - client->read_len);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                return 0;
            }
            ipc_client_destroy(client);
            return 0;
        }

        client->read_len += (size_t)n;

        while (client->read_len >= NORTH_IPC_HEADER_SIZE) {
            struct north_ipc_header hdr;
            memcpy(&hdr, client->read_buf, NORTH_IPC_HEADER_SIZE);

            if (memcmp(hdr.magic, NORTH_IPC_MAGIC, 4) != 0) {
                LOG_ERROR("IPC client sent invalid magic header; disconnecting");
                ipc_client_destroy(client);
                return 0;
            }

            if (hdr.payload_len > NORTH_IPC_MAX_PAYLOAD) {
                LOG_ERROR("IPC client payload exceeded maximum size (%u bytes)", hdr.payload_len);
                ipc_client_destroy(client);
                return 0;
            }

            size_t total_packet = NORTH_IPC_HEADER_SIZE + hdr.payload_len;
            if (client->read_len < total_packet) {
                break;
            }

            char *payload = malloc(hdr.payload_len + 1);
            if (payload) {
                memcpy(payload, client->read_buf + NORTH_IPC_HEADER_SIZE, hdr.payload_len);
                payload[hdr.payload_len] = '\0';
                ipc_process_message(client, hdr.type, payload, hdr.payload_len);
                free(payload);
            }

            size_t remaining = client->read_len - total_packet;
            if (remaining > 0) {
                memmove(client->read_buf, client->read_buf + total_packet, remaining);
            }
            client->read_len = remaining;
        }
    }

    return 0;
}

static int ipc_handle_connection(int fd, uint32_t mask, void *data)
{
    (void)mask;
    struct north_ipc_server *ipc = data;

    int client_fd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        return 0;
    }

    struct north_ipc_client *client = calloc(1, sizeof(*client));
    if (!client) {
        close(client_fd);
        return 0;
    }

    client->ipc = ipc;
    client->fd = client_fd;
    client->read_cap = IPC_CLIENT_BUF_INIT;
    client->read_buf = malloc(client->read_cap);
    if (!client->read_buf) {
        close(client_fd);
        free(client);
        return 0;
    }

    client->event_source = wl_event_loop_add_fd(
        ipc->server->loop, client_fd, WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
        ipc_handle_client_event, client);
    if (!client->event_source) {
        close(client_fd);
        free(client->read_buf);
        free(client);
        return 0;
    }

    wl_list_insert(&ipc->clients, &client->link);
    return 0;
}

bool north_ipc_init(struct north_ipc_server *ipc, struct north_server *server)
{
    if (!ipc || !server || !server->loop) {
        return false;
    }

    memset(ipc, 0, sizeof(*ipc));
    ipc->server = server;
    ipc->socket_fd = -1;
    wl_list_init(&ipc->clients);

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    char path_buf[512];
    const char *display_socket = server->socket ? server->socket : getenv("WAYLAND_DISPLAY");
    if (!display_socket) {
        display_socket = "wayland-0";
    }

    if (runtime_dir && access(runtime_dir, W_OK) == 0) {
        snprintf(path_buf, sizeof(path_buf), "%s/north-ipc-%s.sock", runtime_dir, display_socket);
    } else {
        snprintf(path_buf, sizeof(path_buf), "/tmp/north-runtime-%d/north-ipc-%s.sock",
                 (int)getuid(), display_socket);
        char dir_buf[256];
        snprintf(dir_buf, sizeof(dir_buf), "/tmp/north-runtime-%d", (int)getuid());
        mkdir(dir_buf, 0700);
    }

    ipc->socket_path = strdup(path_buf);
    unlink(ipc->socket_path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        LOG_ERROR("Failed to create IPC Unix socket: %s", strerror(errno));
        free(ipc->socket_path);
        ipc->socket_path = NULL;
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERROR("Failed to bind IPC socket to '%s': %s", ipc->socket_path, strerror(errno));
        close(fd);
        unlink(ipc->socket_path);
        free(ipc->socket_path);
        ipc->socket_path = NULL;
        return false;
    }

    if (listen(fd, 16) != 0) {
        LOG_ERROR("Failed to listen on IPC socket: %s", strerror(errno));
        close(fd);
        unlink(ipc->socket_path);
        free(ipc->socket_path);
        ipc->socket_path = NULL;
        return false;
    }

    ipc->socket_fd = fd;
    ipc->event_source =
        wl_event_loop_add_fd(server->loop, fd, WL_EVENT_READABLE, ipc_handle_connection, ipc);
    if (!ipc->event_source) {
        LOG_ERROR("Failed to add IPC socket to event loop");
        close(fd);
        unlink(ipc->socket_path);
        free(ipc->socket_path);
        ipc->socket_path = NULL;
        ipc->socket_fd = -1;
        return false;
    }

    setenv("NORTH_IPC_SOCK", ipc->socket_path, 1);
    LOG_INFO("IPC server listening on '%s'", ipc->socket_path);
    return true;
}

void north_ipc_finish(struct north_ipc_server *ipc)
{
    if (!ipc) {
        return;
    }

    struct north_ipc_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &ipc->clients, link) { ipc_client_destroy(client); }

    if (ipc->event_source) {
        wl_event_source_remove(ipc->event_source);
        ipc->event_source = NULL;
    }

    if (ipc->socket_fd >= 0) {
        close(ipc->socket_fd);
        ipc->socket_fd = -1;
    }

    if (ipc->socket_path) {
        unlink(ipc->socket_path);
        free(ipc->socket_path);
        ipc->socket_path = NULL;
    }
}

void ipc_broadcast_workspace_event(struct north_server *server, struct north_workspace *ws,
                                   const char *change)
{
    if (!server || !ws || wl_list_empty(&server->ipc.clients)) {
        return;
    }

    Arena arena = arena_create(2048);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);

    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "change", change ? change : "focus");
    json_builder_key(&jb, "workspace");
    json_builder_object_begin(&jb);
    json_builder_key_int(&jb, "index", ws->index);
    json_builder_key_string(&jb, "name", ws->name);
    json_builder_key_string(&jb, "output",
                            (ws->output && ws->output->wlr_output) ? ws->output->wlr_output->name
                                                                   : "unknown");
    json_builder_object_end(&jb);
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);

    struct north_ipc_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &server->ipc.clients, link)
    {
        if (client->subscribed_events & NORTH_IPC_SUB_WORKSPACE) {
            ipc_client_send(client, NORTH_IPC_EVENT_WORKSPACE, json, len);
        }
    }

    arena_destroy(&arena);
}

void ipc_broadcast_window_event(struct north_server *server, struct north_view *view,
                                const char *change)
{
    if (!server || !view || wl_list_empty(&server->ipc.clients)) {
        return;
    }

    Arena arena = arena_create(2048);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);

    const char *title = NULL;
    const char *app_id = NULL;
    if (view->type == NORTH_VIEW_XDG && view->xdg_toplevel) {
        title = view->xdg_toplevel->title;
        app_id = view->xdg_toplevel->app_id;
    } else if (view->type == NORTH_VIEW_XWAYLAND && view->xwayland_surface) {
        title = view->xwayland_surface->title;
        app_id = view->xwayland_surface->class;
    }

    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "change", change ? change : "state");
    json_builder_key(&jb, "window");
    json_builder_object_begin(&jb);
    json_builder_key_int(&jb, "id", (int64_t)(uintptr_t)view);
    json_builder_key_string(&jb, "title", title ? title : "");
    json_builder_key_string(&jb, "app_id", app_id ? app_id : "");
    json_builder_key_int(&jb, "workspace", view->workspace ? view->workspace->index : -1);
    json_builder_key_string(&jb, "state", view_state_to_string(view->state));
    json_builder_object_end(&jb);
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);

    struct north_ipc_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &server->ipc.clients, link)
    {
        if (client->subscribed_events & NORTH_IPC_SUB_WINDOW) {
            ipc_client_send(client, NORTH_IPC_EVENT_WINDOW, json, len);
        }
    }

    arena_destroy(&arena);
}

void ipc_broadcast_output_event(struct north_server *server, struct north_output *output,
                                const char *change)
{
    if (!server || !output || wl_list_empty(&server->ipc.clients)) {
        return;
    }

    Arena arena = arena_create(2048);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);

    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "change", change ? change : "update");
    json_builder_key(&jb, "output");
    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "name", output->wlr_output ? output->wlr_output->name : "unknown");
    json_builder_key_int(&jb, "width", output->usable_area.width);
    json_builder_key_int(&jb, "height", output->usable_area.height);
    json_builder_object_end(&jb);
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);

    struct north_ipc_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &server->ipc.clients, link)
    {
        if (client->subscribed_events & NORTH_IPC_SUB_OUTPUT) {
            ipc_client_send(client, NORTH_IPC_EVENT_OUTPUT, json, len);
        }
    }

    arena_destroy(&arena);
}

void ipc_broadcast_config_event(struct north_server *server, const char *change)
{
    if (!server || wl_list_empty(&server->ipc.clients)) {
        return;
    }

    Arena arena = arena_create(1024);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);

    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "change", change ? change : "reload");
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);

    struct north_ipc_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &server->ipc.clients, link)
    {
        if (client->subscribed_events & NORTH_IPC_SUB_CONFIG) {
            ipc_client_send(client, NORTH_IPC_EVENT_CONFIG, json, len);
        }
    }

    arena_destroy(&arena);
}
