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

struct test_window {
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    bool configured;
    bool is_activated;
    uint32_t last_serial;
};

struct workspace_info {
    struct ext_workspace_handle_v1 *handle;
    char name[32];
    bool active;
};

struct client_state {
    struct wl_display *display;
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

    struct workspace_info workspaces[16];
    int workspace_count;
};

static pid_t server_pid = -1;

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
    struct test_window *win = data;
    win->configured = true;
    win->last_serial = serial;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                                   int32_t height, struct wl_array *states)
{
    (void)toplevel;
    (void)width;
    (void)height;
    struct test_window *win = data;
    win->is_activated = false;
    uint32_t *state;
    wl_array_for_each(state, states)
    {
        if (*state == XDG_TOPLEVEL_STATE_ACTIVATED) {
            win->is_activated = true;
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

static void ws_handle_name(void *data, struct ext_workspace_handle_v1 *handle, const char *name)
{
    (void)handle;
    struct workspace_info *ws = data;
    strncpy(ws->name, name, sizeof(ws->name) - 1);
}

static void ws_handle_state(void *data, struct ext_workspace_handle_v1 *handle, uint32_t state)
{
    (void)handle;
    struct workspace_info *ws = data;
    ws->active = (state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) != 0;
}

static void ws_handle_id(void *data, struct ext_workspace_handle_v1 *handle, const char *id)
{
    (void)data;
    (void)handle;
    (void)id;
}

static void ws_handle_coordinates(void *data, struct ext_workspace_handle_v1 *handle,
                                  struct wl_array *coordinates)
{
    (void)data;
    (void)handle;
    (void)coordinates;
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

static void ws_mgr_workspace(void *data, struct ext_workspace_manager_v1 *mgr,
                             struct ext_workspace_handle_v1 *handle)
{
    (void)mgr;
    struct client_state *state = data;
    if (state->workspace_count < 16) {
        struct workspace_info *ws = &state->workspaces[state->workspace_count++];
        ws->handle = handle;
        ws->active = false;
        ext_workspace_handle_v1_add_listener(handle, &ws_handle_listener, ws);
    }
}

static void ws_mgr_done(void *data, struct ext_workspace_manager_v1 *mgr)
{
    (void)data;
    (void)mgr;
}

static void ws_mgr_workspace_group(void *data, struct ext_workspace_manager_v1 *mgr,
                                   struct ext_workspace_group_handle_v1 *group)
{
    (void)mgr;
    struct client_state *state = data;
    state->ws_group = group;
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

static void cleanup_client(struct client_state *state, struct wl_registry *registry)
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
    if (state->display) {
        wl_display_disconnect(state->display);
        state->display = NULL;
    }
}

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
    } else if (strcmp(interface, "wl_seat") == 0) {
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
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
    int fd = memfd_create("test_anim_buffer", MFD_CLOEXEC);
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

    memset(socket_name_out, 0, socket_name_size);
    ssize_t read_bytes = read(pipe_fds[0], socket_name_out, socket_name_size - 1);
    close(pipe_fds[0]);

    TEST_ASSERT_TRUE(read_bytes > 0);
    char *nl = strchr(socket_name_out, '\n');
    if (nl) {
        *nl = '\0';
    }

    return pid;
}

static void setup_virtual_keyboard(struct client_state *state)
{
    state->vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(state->vk_mgr, state->seat);
    TEST_ASSERT_NOT_NULL(state->vk);

    state->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state->xkb_ctx);
    struct xkb_rule_names rules = {.rules = "evdev", .model = "pc105", .layout = "us"};
    state->xkb_map = xkb_keymap_new_from_names(state->xkb_ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state->xkb_map);

    xkb_mod_index_t mod4_idx = xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_LOGO);
    state->super_mod = (1 << mod4_idx);

    char *keymap_str = xkb_keymap_get_as_string(state->xkb_map, XKB_KEYMAP_FORMAT_TEXT_V1);
    TEST_ASSERT_NOT_NULL(keymap_str);
    size_t keymap_len = strlen(keymap_str) + 1;
    int km_fd = memfd_create("vk_anim_keymap", MFD_CLOEXEC);
    TEST_ASSERT_TRUE(km_fd >= 0);
    TEST_ASSERT_EQUAL_INT(0, ftruncate(km_fd, keymap_len));
    TEST_ASSERT_EQUAL_INT((ssize_t)keymap_len, write(km_fd, keymap_str, keymap_len));
    free(keymap_str);

    zwp_virtual_keyboard_v1_keymap(state->vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, km_fd, keymap_len);
    close(km_fd);
    wl_display_roundtrip(state->display);
}

static void send_shortcut(struct client_state *state, uint32_t mods, uint32_t evdev_key)
{
    zwp_virtual_keyboard_v1_modifiers(state->vk, mods, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(state->vk, 0, evdev_key, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(state->vk, 0, evdev_key, WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_modifiers(state->vk, 0, 0, 0, 0);
    wl_display_flush(state->display);
}

void test_window_open_and_close_animation(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(NULL, socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);

    struct test_window win = {0};
    win.surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(win.surface);

    win.xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, win.surface);
    TEST_ASSERT_NOT_NULL(win.xdg_surface);
    xdg_surface_add_listener(win.xdg_surface, &surface_listener, &win);

    win.toplevel = xdg_surface_get_toplevel(win.xdg_surface);
    TEST_ASSERT_NOT_NULL(win.toplevel);
    xdg_toplevel_add_listener(win.toplevel, &toplevel_listener, &win);
    xdg_toplevel_set_title(win.toplevel, "AnimTestWindow");

    wl_surface_commit(win.surface);
    wl_display_roundtrip(state.display);

    TEST_ASSERT_TRUE(win.configured);

    win.buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(win.buffer);

    wl_surface_attach(win.surface, win.buffer, 0, 0);
    wl_surface_commit(win.surface);
    wl_display_roundtrip(state.display);

    // Wait for the 200ms open animation to complete smoothly
    usleep(250000);
    wl_display_roundtrip(state.display);

    TEST_ASSERT_TRUE(win.is_activated);

    // Close and destroy window
    xdg_toplevel_destroy(win.toplevel);
    xdg_surface_destroy(win.xdg_surface);
    wl_surface_destroy(win.surface);
    wl_buffer_destroy(win.buffer);
    wl_display_roundtrip(state.display);

    // Wait for close animation
    usleep(250000);
    wl_display_roundtrip(state.display);

    cleanup_client(&state, registry);

    kill(server_pid, SIGTERM);
    int status = 0;
    waitpid(server_pid, &status, 0);
    server_pid = -1;
}

void test_workspace_slide_animation(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(NULL, socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    TEST_ASSERT_NOT_NULL(state.vk_mgr);
    TEST_ASSERT_NOT_NULL(state.ws_mgr);
    setup_virtual_keyboard(&state);

    wl_display_roundtrip(state.display);
    TEST_ASSERT_TRUE(state.workspace_count >= 2);

    // Switch to workspace 2 via Super+2
    send_shortcut(&state, state.super_mod, KEY_2);
    // Allow slide animation to run and complete
    usleep(250000);
    wl_display_roundtrip(state.display);

    // Verify workspace 2 is active
    bool ws2_active = false;
    for (int i = 0; i < state.workspace_count; i++) {
        if (strcmp(state.workspaces[i].name, "2") == 0) {
            ws2_active = state.workspaces[i].active;
            break;
        }
    }
    TEST_ASSERT_TRUE(ws2_active);

    // Switch back to workspace 1 via Super+1
    send_shortcut(&state, state.super_mod, KEY_1);
    usleep(250000);
    wl_display_roundtrip(state.display);

    bool ws1_active = false;
    for (int i = 0; i < state.workspace_count; i++) {
        if (strcmp(state.workspaces[i].name, "1") == 0) {
            ws1_active = state.workspaces[i].active;
            break;
        }
    }
    TEST_ASSERT_TRUE(ws1_active);

    cleanup_client(&state, registry);

    kill(server_pid, SIGTERM);
    int status = 0;
    waitpid(server_pid, &status, 0);
    server_pid = -1;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_window_open_and_close_animation);
    RUN_TEST(test_workspace_slide_animation);
    return UNITY_END();
}
