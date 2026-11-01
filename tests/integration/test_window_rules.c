#define _GNU_SOURCE
#include "ext-workspace-v1-client-protocol.h"
#include "unity.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#define MAX_WORKSPACES 32

struct test_workspace_entry {
    struct ext_workspace_handle_v1 *handle;
    char name[32];
    uint32_t state;
};

struct client_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *decor_mgr;
    struct ext_workspace_manager_v1 *ws_mgr;
    struct ext_workspace_group_handle_v1 *ws_group;

    struct test_workspace_entry workspaces[MAX_WORKSPACES];
    int workspace_count;
    bool manager_done;

    bool configured;
    int32_t configured_width;
    int32_t configured_height;
    bool is_maximized;
    bool is_fullscreen;
    bool is_activated;

    bool decor_configured;
    uint32_t decor_mode;
};

static pid_t g_server_pid = -1;
static char g_cfg_path[256] = {0};

static void ws_handle_id(void *data, struct ext_workspace_handle_v1 *handle, const char *id)
{
    (void)data;
    (void)handle;
    (void)id;
}

static void ws_handle_name(void *data, struct ext_workspace_handle_v1 *handle, const char *name)
{
    (void)handle;
    struct test_workspace_entry *entry = data;
    snprintf(entry->name, sizeof(entry->name), "%s", name);
}

static void ws_handle_coordinates(void *data, struct ext_workspace_handle_v1 *handle,
                                  struct wl_array *coords)
{
    (void)data;
    (void)handle;
    (void)coords;
}

static void ws_handle_state(void *data, struct ext_workspace_handle_v1 *handle, uint32_t state)
{
    (void)handle;
    struct test_workspace_entry *entry = data;
    entry->state = state;
}

static void ws_handle_capabilities(void *data, struct ext_workspace_handle_v1 *handle,
                                   uint32_t capabilities)
{
    (void)data;
    (void)handle;
    (void)capabilities;
}

static void ws_handle_removed(void *data, struct ext_workspace_handle_v1 *handle)
{
    (void)data;
    (void)handle;
}

static const struct ext_workspace_handle_v1_listener ws_handle_listener = {
    .id = ws_handle_id,
    .name = ws_handle_name,
    .coordinates = ws_handle_coordinates,
    .state = ws_handle_state,
    .capabilities = ws_handle_capabilities,
    .removed = ws_handle_removed,
};

static void ws_group_capabilities(void *data, struct ext_workspace_group_handle_v1 *group,
                                  uint32_t capabilities)
{
    (void)data;
    (void)group;
    (void)capabilities;
}

static void ws_group_output_enter(void *data, struct ext_workspace_group_handle_v1 *group,
                                  struct wl_output *output)
{
    (void)data;
    (void)group;
    (void)output;
}

static void ws_group_output_leave(void *data, struct ext_workspace_group_handle_v1 *group,
                                  struct wl_output *output)
{
    (void)data;
    (void)group;
    (void)output;
}

static void ws_group_workspace_enter(void *data, struct ext_workspace_group_handle_v1 *group,
                                     struct ext_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)group;
    (void)workspace;
}

static void ws_group_workspace_leave(void *data, struct ext_workspace_group_handle_v1 *group,
                                     struct ext_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)group;
    (void)workspace;
}

static const struct ext_workspace_group_handle_v1_listener ws_group_listener = {
    .capabilities = ws_group_capabilities,
    .output_enter = ws_group_output_enter,
    .output_leave = ws_group_output_leave,
    .workspace_enter = ws_group_workspace_enter,
    .workspace_leave = ws_group_workspace_leave,
    .removed = NULL,
};

static void ws_mgr_workspace_group(void *data, struct ext_workspace_manager_v1 *mgr,
                                   struct ext_workspace_group_handle_v1 *group)
{
    (void)mgr;
    struct client_state *state = data;
    state->ws_group = group;
    ext_workspace_group_handle_v1_add_listener(group, &ws_group_listener, state);
}

static void ws_mgr_workspace(void *data, struct ext_workspace_manager_v1 *mgr,
                             struct ext_workspace_handle_v1 *handle)
{
    (void)mgr;
    struct client_state *state = data;
    if (state->workspace_count < MAX_WORKSPACES) {
        struct test_workspace_entry *entry = &state->workspaces[state->workspace_count++];
        memset(entry, 0, sizeof(*entry));
        entry->handle = handle;
        ext_workspace_handle_v1_add_listener(handle, &ws_handle_listener, entry);
    }
}

static void ws_mgr_done(void *data, struct ext_workspace_manager_v1 *mgr)
{
    (void)mgr;
    struct client_state *state = data;
    state->manager_done = true;
}

static void ws_mgr_finished(void *data, struct ext_workspace_manager_v1 *mgr)
{
    (void)data;
    (void)mgr;
}

static const struct ext_workspace_manager_v1_listener ws_mgr_listener = {
    .workspace_group = ws_mgr_workspace_group,
    .workspace = ws_mgr_workspace,
    .done = ws_mgr_done,
    .finished = ws_mgr_finished,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void decor_configure(void *data, struct zxdg_toplevel_decoration_v1 *decor, uint32_t mode)
{
    (void)decor;
    struct client_state *state = data;
    state->decor_configured = true;
    state->decor_mode = mode;
}

static const struct zxdg_toplevel_decoration_v1_listener decor_listener = {
    .configure = decor_configure,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    struct client_state *state = data;
    state->configured = true;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                                   int32_t height, struct wl_array *states)
{
    (void)toplevel;
    struct client_state *state = data;
    state->configured_width = width;
    state->configured_height = height;

    state->is_maximized = false;
    state->is_fullscreen = false;
    state->is_activated = false;

    uint32_t *s;
    wl_array_for_each(s, states)
    {
        if (*s == XDG_TOPLEVEL_STATE_MAXIMIZED) {
            state->is_maximized = true;
        } else if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN) {
            state->is_fullscreen = true;
        } else if (*s == XDG_TOPLEVEL_STATE_ACTIVATED) {
            state->is_activated = true;
        }
    }
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data;
    (void)toplevel;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    (void)version;
    struct client_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, state);
    } else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
        state->decor_mgr =
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (strcmp(interface, "ext_workspace_manager_v1") == 0) {
        state->ws_mgr = wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, 1);
        ext_workspace_manager_v1_add_listener(state->ws_mgr, &ws_mgr_listener, state);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static struct wl_buffer *create_shm_buffer(struct wl_shm *shm, int width, int height)
{
    int stride = width * 4;
    int size = stride * height;
    int fd = memfd_create("shm_buf", MFD_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return NULL;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static pid_t launch_compositor(const char *config_path, char *socket_out, size_t socket_sz)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        setenv("WLR_BACKENDS", "headless", 1);
        setenv("WLR_RENDERER", "pixman", 1);
        setenv("WLR_LIBINPUT_NO_DEVICES", "1", 1);

        char ready_str[32];
        snprintf(ready_str, sizeof(ready_str), "%d", pipe_fds[1]);

        if (config_path) {
            execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-R", ready_str, "-c", config_path,
                  (char *)NULL);
        } else {
            execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-R", ready_str, (char *)NULL);
        }
        _exit(1);
    }

    close(pipe_fds[1]);

    char buf[128] = {0};
    ssize_t n = read(pipe_fds[0], buf, sizeof(buf) - 1);
    close(pipe_fds[0]);

    if (n <= 0) {
        return -1;
    }

    char *nl = strchr(buf, '\n');
    if (nl) {
        *nl = '\0';
    }
    snprintf(socket_out, socket_sz, "%s", buf);
    return pid;
}

static void stop_compositor(pid_t pid)
{
    if (pid > 0) {
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
    }
}

static void cleanup_client_state(struct client_state *state, struct wl_registry *registry,
                                 struct wl_display *display)
{
    for (int i = 0; i < state->workspace_count; i++) {
        if (state->workspaces[i].handle) {
            ext_workspace_handle_v1_destroy(state->workspaces[i].handle);
            state->workspaces[i].handle = NULL;
        }
    }
    state->workspace_count = 0;

    if (state->ws_group) {
        ext_workspace_group_handle_v1_destroy(state->ws_group);
        state->ws_group = NULL;
    }
    if (state->ws_mgr) {
        ext_workspace_manager_v1_destroy(state->ws_mgr);
        state->ws_mgr = NULL;
    }
    if (state->decor_mgr) {
        zxdg_decoration_manager_v1_destroy(state->decor_mgr);
        state->decor_mgr = NULL;
    }
    if (state->wm_base) {
        xdg_wm_base_destroy(state->wm_base);
        state->wm_base = NULL;
    }
    if (state->shm) {
        wl_shm_destroy(state->shm);
        state->shm = NULL;
    }
    if (state->compositor) {
        wl_compositor_destroy(state->compositor);
        state->compositor = NULL;
    }
    if (registry) {
        wl_registry_destroy(registry);
    }
    if (display) {
        wl_display_disconnect(display);
    }
}

void setUp(void)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || access(runtime_dir, W_OK) != 0) {
        char fallback[256];
        snprintf(fallback, sizeof(fallback), "/tmp/north-runtime-%d", (int)getuid());
        mkdir(fallback, 0700);
        setenv("XDG_RUNTIME_DIR", fallback, 1);
    }

    snprintf(g_cfg_path, sizeof(g_cfg_path), "/tmp/north_rules_test_XXXXXX.conf");
    int fd = mkstemps(g_cfg_path, 5);
    TEST_ASSERT_TRUE(fd >= 0);

    const char *test_config = "[workspaces]\n"
                              "count = 5\n"
                              "\n"
                              "[rule:ws_rule]\n"
                              "app_id = test-ws-app\n"
                              "workspace = 3\n"
                              "\n"
                              "[rule:size_rule]\n"
                              "app_id = test-size-app\n"
                              "size = 700x500\n"
                              "\n"
                              "[rule:snap_rule]\n"
                              "app_id = test-snap-app\n"
                              "snap = right\n"
                              "\n"
                              "[rule:max_rule]\n"
                              "app_id = test-max-app\n"
                              "maximize = true\n"
                              "\n"
                              "[rule:fs_rule]\n"
                              "app_id = test-fs-app\n"
                              "fullscreen = true\n"
                              "\n"
                              "[rule:ssd_rule]\n"
                              "app_id = test-ssd-app\n"
                              "ssd = false\n"
                              "\n"
                              "[rule:late_rule]\n"
                              "title = *SpecialPopup*\n"
                              "maximize = true\n"
                              "\n"
                              "[rule:override_rule]\n"
                              "app_id = test-override-app\n"
                              "size = 650x450\n"
                              "maximize = false\n"
                              "\n"
                              "[rule:ssd_enforce_rule]\n"
                              "app_id = test-ssd-enforce-app\n"
                              "ssd = true\n"
                              "\n"
                              "[rule:compound_rule]\n"
                              "app_id = test-compound-app\n"
                              "title = TargetTitle\n"
                              "maximize = true\n";

    ssize_t w = write(fd, test_config, strlen(test_config));
    TEST_ASSERT_TRUE(w > 0);
    close(fd);
}

void tearDown(void)
{
    if (g_server_pid > 0) {
        stop_compositor(g_server_pid);
        g_server_pid = -1;
    }
    if (g_cfg_path[0] != '\0') {
        unlink(g_cfg_path);
        g_cfg_path[0] = '\0';
    }
}

void test_rule_workspace_placement(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);
    TEST_ASSERT_NOT_NULL(state.ws_mgr);
    TEST_ASSERT_EQUAL_INT(5, state.workspace_count);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);

    xdg_toplevel_set_app_id(toplevel, "test-ws-app");
    xdg_toplevel_set_title(toplevel, "Workspace Test Window");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    /* Initially Workspace 1 is active, so window placed on Workspace 3 is inactive */
    TEST_ASSERT_FALSE(state.is_activated);

    /* Switch to Workspace 3 */
    ext_workspace_handle_v1_activate(state.workspaces[2].handle);
    ext_workspace_manager_v1_commit(state.ws_mgr);
    wl_display_flush(display);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    /* Now Workspace 3 is active and window receives focus/activation */
    TEST_ASSERT_TRUE(state.is_activated);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    cleanup_client_state(&state, registry, display);
}

void test_rule_initial_floating_size(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);

    xdg_toplevel_set_app_id(toplevel, "test-size-app");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    /* Rule configured size = 700x500 */
    TEST_ASSERT_EQUAL_INT(700, state.configured_width);
    TEST_ASSERT_EQUAL_INT(500, state.configured_height);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    cleanup_client_state(&state, registry, display);
}

void test_rule_edge_snap_zone(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);

    xdg_toplevel_set_app_id(toplevel, "test-snap-app");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    /* On a 1280x720 headless display, snap right occupies 640 width (half of 1280) */
    TEST_ASSERT_EQUAL_INT(640, state.configured_width);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    cleanup_client_state(&state, registry, display);
}

void test_rule_maximize_and_fullscreen(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    /* 1. Maximize rule */
    struct wl_surface *s1 = wl_compositor_create_surface(state.compositor);
    struct xdg_surface *xs1 = xdg_wm_base_get_xdg_surface(state.wm_base, s1);
    xdg_surface_add_listener(xs1, &surface_listener, &state);
    struct xdg_toplevel *tl1 = xdg_surface_get_toplevel(xs1);
    xdg_toplevel_add_listener(tl1, &toplevel_listener, &state);
    xdg_toplevel_set_app_id(tl1, "test-max-app");

    wl_surface_commit(s1);
    wl_display_roundtrip(display);

    struct wl_buffer *b1 = create_shm_buffer(state.shm, 400, 300);
    wl_surface_attach(s1, b1, 0, 0);
    wl_surface_commit(s1);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.is_maximized);
    TEST_ASSERT_FALSE(state.is_fullscreen);

    xdg_toplevel_destroy(tl1);
    xdg_surface_destroy(xs1);
    wl_surface_destroy(s1);
    wl_buffer_destroy(b1);

    /* 2. Fullscreen rule */
    struct wl_surface *s2 = wl_compositor_create_surface(state.compositor);
    struct xdg_surface *xs2 = xdg_wm_base_get_xdg_surface(state.wm_base, s2);
    xdg_surface_add_listener(xs2, &surface_listener, &state);
    struct xdg_toplevel *tl2 = xdg_surface_get_toplevel(xs2);
    xdg_toplevel_add_listener(tl2, &toplevel_listener, &state);
    xdg_toplevel_set_app_id(tl2, "test-fs-app");

    wl_surface_commit(s2);
    wl_display_roundtrip(display);

    struct wl_buffer *b2 = create_shm_buffer(state.shm, 400, 300);
    wl_surface_attach(s2, b2, 0, 0);
    wl_surface_commit(s2);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.is_fullscreen);

    xdg_toplevel_destroy(tl2);
    xdg_surface_destroy(xs2);
    wl_surface_destroy(s2);
    wl_buffer_destroy(b2);

    cleanup_client_state(&state, registry, display);
}

void test_rule_ssd_override(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.decor_mgr);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);

    xdg_toplevel_set_app_id(toplevel, "test-ssd-app");

    struct zxdg_toplevel_decoration_v1 *decor =
        zxdg_decoration_manager_v1_get_toplevel_decoration(state.decor_mgr, toplevel);
    TEST_ASSERT_NOT_NULL(decor);
    zxdg_toplevel_decoration_v1_add_listener(decor, &decor_listener, &state);
    zxdg_toplevel_decoration_v1_set_mode(decor, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    /* Server should have configured client-side decoration because ssd = false */
    TEST_ASSERT_TRUE(state.decor_configured);
    TEST_ASSERT_EQUAL_UINT32(ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE, state.decor_mode);

    zxdg_toplevel_decoration_v1_destroy(decor);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    cleanup_client_state(&state, registry, display);
}

void test_rule_late_title_matching(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);

    /* Initially no title and generic app_id */
    xdg_toplevel_set_app_id(toplevel, "generic-app");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    /* No rule matched yet */
    TEST_ASSERT_FALSE(state.is_maximized);

    /* Set title to match late rule "*SpecialPopup*" */
    xdg_toplevel_set_title(toplevel, "My SpecialPopup Window");
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    /* Rule should have applied maximize */
    TEST_ASSERT_TRUE(state.is_maximized);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    cleanup_client_state(&state, registry, display);
}

void test_rule_client_maximize_override_false(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);

    xdg_toplevel_set_app_id(toplevel, "test-override-app");
    /* Client requests to be maximized */
    xdg_toplevel_set_maximized(toplevel);

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    /* Rule specifies maximize = false and size = 650x450; client request must be overridden */
    TEST_ASSERT_FALSE(state.is_maximized);
    TEST_ASSERT_EQUAL_INT(650, state.configured_width);
    TEST_ASSERT_EQUAL_INT(450, state.configured_height);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    cleanup_client_state(&state, registry, display);
}

void test_rule_ssd_enforced_true(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.decor_mgr);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);

    xdg_toplevel_set_app_id(toplevel, "test-ssd-enforce-app");

    struct zxdg_toplevel_decoration_v1 *decor =
        zxdg_decoration_manager_v1_get_toplevel_decoration(state.decor_mgr, toplevel);
    TEST_ASSERT_NOT_NULL(decor);
    zxdg_toplevel_decoration_v1_add_listener(decor, &decor_listener, &state);
    /* Client explicitly requests client-side decoration mode */
    zxdg_toplevel_decoration_v1_set_mode(decor, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    /* Rule specifies ssd = true; must enforce SERVER_SIDE despite client CSD request */
    TEST_ASSERT_TRUE(state.decor_configured);
    TEST_ASSERT_EQUAL_UINT32(ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE, state.decor_mode);

    zxdg_toplevel_decoration_v1_destroy(decor);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    cleanup_client_state(&state, registry, display);
}

void test_rule_app_id_and_title_compound(void)
{
    char socket_name[128] = {0};
    g_server_pid = launch_compositor(g_cfg_path, socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(g_server_pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    /* 1. Window with matching app_id but non-matching title: must NOT match rule */
    struct wl_surface *s1 = wl_compositor_create_surface(state.compositor);
    struct xdg_surface *xs1 = xdg_wm_base_get_xdg_surface(state.wm_base, s1);
    xdg_surface_add_listener(xs1, &surface_listener, &state);
    struct xdg_toplevel *tl1 = xdg_surface_get_toplevel(xs1);
    xdg_toplevel_add_listener(tl1, &toplevel_listener, &state);
    xdg_toplevel_set_app_id(tl1, "test-compound-app");
    xdg_toplevel_set_title(tl1, "NonMatchingTitle");

    wl_surface_commit(s1);
    wl_display_roundtrip(display);

    struct wl_buffer *b1 = create_shm_buffer(state.shm, 400, 300);
    wl_surface_attach(s1, b1, 0, 0);
    wl_surface_commit(s1);
    wl_display_roundtrip(display);

    TEST_ASSERT_FALSE(state.is_maximized);

    xdg_toplevel_destroy(tl1);
    xdg_surface_destroy(xs1);
    wl_surface_destroy(s1);
    wl_buffer_destroy(b1);

    /* 2. Window with matching app_id AND matching title: MUST match rule */
    struct wl_surface *s2 = wl_compositor_create_surface(state.compositor);
    struct xdg_surface *xs2 = xdg_wm_base_get_xdg_surface(state.wm_base, s2);
    xdg_surface_add_listener(xs2, &surface_listener, &state);
    struct xdg_toplevel *tl2 = xdg_surface_get_toplevel(xs2);
    xdg_toplevel_add_listener(tl2, &toplevel_listener, &state);
    xdg_toplevel_set_app_id(tl2, "test-compound-app");
    xdg_toplevel_set_title(tl2, "TargetTitle");

    wl_surface_commit(s2);
    wl_display_roundtrip(display);

    struct wl_buffer *b2 = create_shm_buffer(state.shm, 400, 300);
    wl_surface_attach(s2, b2, 0, 0);
    wl_surface_commit(s2);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.is_maximized);

    xdg_toplevel_destroy(tl2);
    xdg_surface_destroy(xs2);
    wl_surface_destroy(s2);
    wl_buffer_destroy(b2);

    cleanup_client_state(&state, registry, display);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rule_workspace_placement);
    RUN_TEST(test_rule_initial_floating_size);
    RUN_TEST(test_rule_edge_snap_zone);
    RUN_TEST(test_rule_maximize_and_fullscreen);
    RUN_TEST(test_rule_ssd_override);
    RUN_TEST(test_rule_late_title_matching);
    RUN_TEST(test_rule_client_maximize_override_false);
    RUN_TEST(test_rule_ssd_enforced_true);
    RUN_TEST(test_rule_app_id_and_title_compound);
    return UNITY_END();
}
