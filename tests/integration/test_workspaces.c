#define _GNU_SOURCE
#include "ext-workspace-v1-client-protocol.h"
#include "unity.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <linux/input-event-codes.h>
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
#include <xkbcommon/xkbcommon.h>

#define MAX_WORKSPACES 64

struct test_workspace_entry {
    struct ext_workspace_handle_v1 *handle;
    char id[64];
    char name[32];
    uint32_t state;
    uint32_t capabilities;
    bool removed;
};

struct client_workspace_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct zwp_virtual_keyboard_manager_v1 *vk_mgr;
    struct zwp_virtual_keyboard_v1 *vk;
    struct ext_workspace_manager_v1 *ws_mgr;
    struct ext_workspace_group_handle_v1 *ws_group;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_map;
    uint32_t super_mod;
    uint32_t shift_mod;
    uint32_t ctrl_mod;

    struct test_workspace_entry workspaces[MAX_WORKSPACES];
    int workspace_count;
    bool manager_done;
    bool group_received;
    bool group_removed;

    bool xdg_configured;
    uint32_t last_serial;
};

static pid_t server_pid = -1;

static void ws_handle_id(void *data, struct ext_workspace_handle_v1 *handle, const char *id)
{
    (void)handle;
    struct test_workspace_entry *entry = data;
    if (id) {
        strncpy(entry->id, id, sizeof(entry->id) - 1);
    }
}

static void ws_handle_name(void *data, struct ext_workspace_handle_v1 *handle, const char *name)
{
    (void)handle;
    struct test_workspace_entry *entry = data;
    if (name) {
        strncpy(entry->name, name, sizeof(entry->name) - 1);
    }
}

static void ws_handle_coordinates(void *data, struct ext_workspace_handle_v1 *handle,
                                  struct wl_array *coordinates)
{
    (void)data;
    (void)handle;
    (void)coordinates;
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
    (void)handle;
    struct test_workspace_entry *entry = data;
    entry->capabilities = capabilities;
}

static void ws_handle_removed(void *data, struct ext_workspace_handle_v1 *handle)
{
    (void)handle;
    struct test_workspace_entry *entry = data;
    entry->removed = true;
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

static void ws_group_removed(void *data, struct ext_workspace_group_handle_v1 *group)
{
    (void)group;
    struct client_workspace_state *state = data;
    state->group_removed = true;
}

static const struct ext_workspace_group_handle_v1_listener ws_group_listener = {
    .capabilities = ws_group_capabilities,
    .output_enter = ws_group_output_enter,
    .output_leave = ws_group_output_leave,
    .workspace_enter = ws_group_workspace_enter,
    .workspace_leave = ws_group_workspace_leave,
    .removed = ws_group_removed,
};

static void ws_mgr_workspace_group(void *data, struct ext_workspace_manager_v1 *mgr,
                                   struct ext_workspace_group_handle_v1 *group)
{
    (void)mgr;
    struct client_workspace_state *state = data;
    state->ws_group = group;
    state->group_received = true;
    ext_workspace_group_handle_v1_add_listener(group, &ws_group_listener, state);
}

static void ws_mgr_workspace(void *data, struct ext_workspace_manager_v1 *mgr,
                             struct ext_workspace_handle_v1 *handle)
{
    (void)mgr;
    struct client_workspace_state *state = data;
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
    struct client_workspace_state *state = data;
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

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    struct client_workspace_state *state = data;
    state->xdg_configured = true;
    state->last_serial = serial;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                                   int32_t height, struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
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
    struct client_workspace_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, state);
    } else if (strcmp(interface, "wl_seat") == 0) {
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
    } else if (strcmp(interface, "zwp_virtual_keyboard_manager_v1") == 0) {
        state->vk_mgr =
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
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
    int fd = memfd_create("shm_buffer", MFD_CLOEXEC);
    if (fd < 0)
        return NULL;
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

static void send_shortcut(struct client_workspace_state *state, struct wl_display *display,
                          uint32_t mods, uint32_t evdev_key)
{
    zwp_virtual_keyboard_v1_modifiers(state->vk, mods, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(state->vk, 0, evdev_key, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(state->vk, 0, evdev_key, WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_modifiers(state->vk, 0, 0, 0, 0);
    wl_display_flush(display);
}

static void setup_virtual_keyboard(struct client_workspace_state *state, struct wl_display *display)
{
    state->vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(state->vk_mgr, state->seat);
    TEST_ASSERT_NOT_NULL(state->vk);

    state->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state->xkb_ctx);
    struct xkb_rule_names rules = {.rules = "evdev", .model = "pc105", .layout = "us"};
    state->xkb_map = xkb_keymap_new_from_names(state->xkb_ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state->xkb_map);

    xkb_mod_index_t mod4_idx = xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_LOGO);
    xkb_mod_index_t shift_idx = xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_SHIFT);
    xkb_mod_index_t ctrl_idx = xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_CTRL);
    state->super_mod = (1 << mod4_idx);
    state->shift_mod = (1 << shift_idx);
    state->ctrl_mod = (1 << ctrl_idx);

    char *keymap_str = xkb_keymap_get_as_string(state->xkb_map, XKB_KEYMAP_FORMAT_TEXT_V1);
    TEST_ASSERT_NOT_NULL(keymap_str);
    size_t keymap_len = strlen(keymap_str) + 1;
    int km_fd = memfd_create("vk_keymap", MFD_CLOEXEC);
    TEST_ASSERT_TRUE(km_fd >= 0);
    TEST_ASSERT_EQUAL_INT(0, ftruncate(km_fd, keymap_len));
    TEST_ASSERT_EQUAL_INT((ssize_t)keymap_len, write(km_fd, keymap_str, keymap_len));
    free(keymap_str);

    zwp_virtual_keyboard_v1_keymap(state->vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, km_fd, keymap_len);
    close(km_fd);
    wl_display_roundtrip(display);
}

static pid_t launch_compositor(const char *config_path, char *socket_name_out,
                               size_t socket_name_size)
{
    int pipe_fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(pipe_fds));

    pid_t pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        close(pipe_fds[0]);

        setenv("WLR_BACKENDS", "headless", 1);
        setenv("WLR_RENDERER", "pixman", 1);
        setenv("WLR_LIBINPUT_NO_DEVICES", "1", 1);

        char ready_str[32];
        snprintf(ready_str, sizeof(ready_str), "%d", pipe_fds[1]);

        if (config_path) {
            execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-c", config_path, "-R", ready_str,
                  (char *)NULL);
        } else {
            execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-R", ready_str, (char *)NULL);
        }
        _exit(1);
    }

    close(pipe_fds[1]);

    char socket_name[128] = {0};
    ssize_t read_bytes = read(pipe_fds[0], socket_name, sizeof(socket_name) - 1);
    close(pipe_fds[0]);

    TEST_ASSERT_TRUE(read_bytes > 0);
    char *nl = strchr(socket_name, '\n');
    if (nl)
        *nl = '\0';

    strncpy(socket_name_out, socket_name, socket_name_size - 1);
    socket_name_out[socket_name_size - 1] = '\0';
    return pid;
}

static int get_active_workspace_index(struct client_workspace_state *state)
{
    for (int i = 0; i < state->workspace_count; i++) {
        if (!state->workspaces[i].removed &&
            (state->workspaces[i].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE)) {
            return i;
        }
    }
    return -1;
}

static bool wait_for_active_workspace(struct wl_display *display,
                                      struct client_workspace_state *state, int target_index,
                                      int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        wl_display_roundtrip(display);
        if (get_active_workspace_index(state) == target_index) {
            return true;
        }
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
        nanosleep(&ts, NULL);
        elapsed += 10;
    }
    return get_active_workspace_index(state) == target_index;
}

static void cleanup_client_state(struct client_workspace_state *state, struct wl_registry *registry,
                                 struct wl_display *display)
{
    if (state->vk) {
        zwp_virtual_keyboard_v1_destroy(state->vk);
        state->vk = NULL;
    }
    if (state->xkb_map) {
        xkb_keymap_unref(state->xkb_map);
        state->xkb_map = NULL;
    }
    if (state->xkb_ctx) {
        xkb_context_unref(state->xkb_ctx);
        state->xkb_ctx = NULL;
    }
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
    if (state->vk_mgr) {
        zwp_virtual_keyboard_manager_v1_destroy(state->vk_mgr);
        state->vk_mgr = NULL;
    }
    if (state->seat) {
        wl_seat_destroy(state->seat);
        state->seat = NULL;
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
}

void tearDown(void)
{
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        int status = 0;
        waitpid(server_pid, &status, 0);
        server_pid = -1;
    }
}

void test_workspace_enumeration_and_initial_active_state(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(NULL, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_workspace_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.ws_mgr);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.group_received);
    TEST_ASSERT_EQUAL_INT(9, state.workspace_count);
    TEST_ASSERT_TRUE(state.manager_done);

    for (int i = 0; i < 9; i++) {
        char expected_name[16];
        snprintf(expected_name, sizeof(expected_name), "%d", i + 1);
        TEST_ASSERT_EQUAL_STRING(expected_name, state.workspaces[i].name);
        if (i == 0) {
            TEST_ASSERT_TRUE(state.workspaces[i].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
        } else {
            TEST_ASSERT_FALSE(state.workspaces[i].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
        }
    }

    cleanup_client_state(&state, registry, display);
}

void test_workspace_switch_via_keybind_and_window_visibility(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(NULL, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_workspace_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);
    TEST_ASSERT_NOT_NULL(state.seat);
    TEST_ASSERT_NOT_NULL(state.vk_mgr);
    TEST_ASSERT_NOT_NULL(state.ws_mgr);

    setup_virtual_keyboard(&state, display);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Workspace Test Window");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 640, 480);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    TEST_ASSERT_EQUAL_INT(0, get_active_workspace_index(&state));

    send_shortcut(&state, display, state.super_mod, KEY_2);
    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 1, 1000));
    TEST_ASSERT_EQUAL_INT(1, get_active_workspace_index(&state));
    TEST_ASSERT_FALSE(state.workspaces[0].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
    TEST_ASSERT_TRUE(state.workspaces[1].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);

    send_shortcut(&state, display, state.super_mod, KEY_1);
    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 0, 1000));
    TEST_ASSERT_EQUAL_INT(0, get_active_workspace_index(&state));
    TEST_ASSERT_TRUE(state.workspaces[0].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
    TEST_ASSERT_FALSE(state.workspaces[1].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    cleanup_client_state(&state, registry, display);
}

void test_workspace_move_view_via_keybind(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(NULL, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_workspace_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    setup_virtual_keyboard(&state, display);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Move View Test Window");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    TEST_ASSERT_EQUAL_INT(0, get_active_workspace_index(&state));

    send_shortcut(&state, display, state.super_mod | state.shift_mod, KEY_3);
    wl_display_roundtrip(display);

    TEST_ASSERT_EQUAL_INT(0, get_active_workspace_index(&state));

    send_shortcut(&state, display, state.super_mod, KEY_3);
    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 2, 1000));
    TEST_ASSERT_EQUAL_INT(2, get_active_workspace_index(&state));

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    cleanup_client_state(&state, registry, display);
}

void test_workspace_cycling_next_and_prev(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(NULL, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_workspace_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    setup_virtual_keyboard(&state, display);

    TEST_ASSERT_EQUAL_INT(0, get_active_workspace_index(&state));

    send_shortcut(&state, display, state.super_mod | state.ctrl_mod, KEY_RIGHT);
    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 1, 1000));
    TEST_ASSERT_EQUAL_INT(1, get_active_workspace_index(&state));

    send_shortcut(&state, display, state.super_mod | state.ctrl_mod, KEY_RIGHT);
    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 2, 1000));
    TEST_ASSERT_EQUAL_INT(2, get_active_workspace_index(&state));

    send_shortcut(&state, display, state.super_mod | state.ctrl_mod, KEY_LEFT);
    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 1, 1000));
    TEST_ASSERT_EQUAL_INT(1, get_active_workspace_index(&state));

    send_shortcut(&state, display, state.super_mod | state.ctrl_mod, KEY_LEFT);
    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 0, 1000));
    TEST_ASSERT_EQUAL_INT(0, get_active_workspace_index(&state));

    cleanup_client_state(&state, registry, display);
}

void test_workspace_client_protocol_activate_commit(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(NULL, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_workspace_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    TEST_ASSERT_EQUAL_INT(9, state.workspace_count);
    TEST_ASSERT_EQUAL_INT(0, get_active_workspace_index(&state));

    ext_workspace_handle_v1_activate(state.workspaces[1].handle);
    ext_workspace_manager_v1_commit(state.ws_mgr);
    wl_display_flush(display);

    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 1, 1000));
    TEST_ASSERT_EQUAL_INT(1, get_active_workspace_index(&state));
    TEST_ASSERT_FALSE(state.workspaces[0].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
    TEST_ASSERT_TRUE(state.workspaces[1].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);

    ext_workspace_handle_v1_activate(state.workspaces[4].handle);
    ext_workspace_manager_v1_commit(state.ws_mgr);
    wl_display_flush(display);

    TEST_ASSERT_TRUE(wait_for_active_workspace(display, &state, 4, 1000));
    TEST_ASSERT_EQUAL_INT(4, get_active_workspace_index(&state));

    cleanup_client_state(&state, registry, display);
}

void test_workspace_hot_reload_count_reduction_and_migration(void)
{
    char cfg_path[] = "/tmp/north_test_ws_XXXXXX.conf";
    int fd = mkstemps(cfg_path, 5);
    TEST_ASSERT_TRUE(fd >= 0);

    const char *initial_cfg = "[workspaces]\ncount = 5\n";
    write(fd, initial_cfg, strlen(initial_cfg));
    close(fd);

    char socket_name[128] = {0};
    server_pid = launch_compositor(cfg_path, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_workspace_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    TEST_ASSERT_EQUAL_INT(5, state.workspace_count);

    FILE *f = fopen(cfg_path, "w");
    TEST_ASSERT_NOT_NULL(f);
    const char *reduced_cfg = "[workspaces]\ncount = 3\n";
    fputs(reduced_cfg, f);
    fclose(f);

    int elapsed = 0;
    while (elapsed < 1000) {
        wl_display_roundtrip(display);
        if (state.workspaces[3].removed && state.workspaces[4].removed) {
            break;
        }
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
        nanosleep(&ts, NULL);
        elapsed += 10;
    }

    TEST_ASSERT_TRUE(state.workspaces[3].removed);
    TEST_ASSERT_TRUE(state.workspaces[4].removed);

    cleanup_client_state(&state, registry, display);
    unlink(cfg_path);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_workspace_enumeration_and_initial_active_state);
    RUN_TEST(test_workspace_switch_via_keybind_and_window_visibility);
    RUN_TEST(test_workspace_move_view_via_keybind);
    RUN_TEST(test_workspace_cycling_next_and_prev);
    RUN_TEST(test_workspace_client_protocol_activate_commit);
    RUN_TEST(test_workspace_hot_reload_count_reduction_and_migration);
    return UNITY_END();
}
