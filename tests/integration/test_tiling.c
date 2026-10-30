#define _GNU_SOURCE
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

struct client_tiling_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct zwp_virtual_keyboard_manager_v1 *vk_mgr;
    struct zwp_virtual_keyboard_v1 *vk;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_map;
    uint32_t super_mod;
    uint32_t alt_mod;

    bool xdg_configured;
    uint32_t last_serial;
    int32_t configured_width;
    int32_t configured_height;
    bool is_maximized;
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
    struct client_tiling_state *state = data;
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
    (void)toplevel;
    struct client_tiling_state *state = data;
    state->configured_width = width;
    state->configured_height = height;
    state->is_maximized = false;

    uint32_t *entry;
    wl_array_for_each(entry, states)
    {
        if (*entry == XDG_TOPLEVEL_STATE_MAXIMIZED) {
            state->is_maximized = true;
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
    struct client_tiling_state *state = data;

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

static bool wait_for_configure(struct wl_display *display, struct client_tiling_state *state,
                               int timeout_ms)
{
    state->xdg_configured = false;
    int elapsed = 0;
    while (!state->xdg_configured && elapsed < timeout_ms) {
        wl_display_roundtrip(display);
        if (state->xdg_configured)
            return true;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
        nanosleep(&ts, NULL);
        elapsed += 10;
    }
    return state->xdg_configured;
}

static void send_shortcut(struct client_tiling_state *state, struct wl_display *display,
                          uint32_t mods, uint32_t evdev_key)
{
    zwp_virtual_keyboard_v1_modifiers(state->vk, mods, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(state->vk, 0, evdev_key, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(state->vk, 0, evdev_key, WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_modifiers(state->vk, 0, 0, 0, 0);
    wl_display_flush(display);
}

void test_tiling_shortcuts_and_snapping(void)
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

        execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-R", ready_str, (char *)NULL);
        _exit(1);
    }

    server_pid = pid;
    close(pipe_fds[1]);

    char socket_name[128] = {0};
    ssize_t read_bytes = read(pipe_fds[0], socket_name, sizeof(socket_name) - 1);
    close(pipe_fds[0]);

    TEST_ASSERT_TRUE(read_bytes > 0);
    char *nl = strchr(socket_name, '\n');
    if (nl)
        *nl = '\0';

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_tiling_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);
    TEST_ASSERT_NOT_NULL(state.seat);
    TEST_ASSERT_NOT_NULL(state.vk_mgr);

    state.vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(state.vk_mgr, state.seat);
    TEST_ASSERT_NOT_NULL(state.vk);

    state.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state.xkb_ctx);
    struct xkb_rule_names rules = {.rules = "evdev", .model = "pc105", .layout = "us"};
    state.xkb_map = xkb_keymap_new_from_names(state.xkb_ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state.xkb_map);

    xkb_mod_index_t mod4_idx = xkb_keymap_mod_get_index(state.xkb_map, XKB_MOD_NAME_LOGO);
    xkb_mod_index_t mod1_idx = xkb_keymap_mod_get_index(state.xkb_map, XKB_MOD_NAME_ALT);
    state.super_mod = (1 << mod4_idx);
    state.alt_mod = (1 << mod1_idx);

    char *keymap_str = xkb_keymap_get_as_string(state.xkb_map, XKB_KEYMAP_FORMAT_TEXT_V1);
    TEST_ASSERT_NOT_NULL(keymap_str);
    size_t keymap_len = strlen(keymap_str) + 1;
    int km_fd = memfd_create("vk_keymap", MFD_CLOEXEC);
    TEST_ASSERT_TRUE(km_fd >= 0);
    TEST_ASSERT_EQUAL_INT(0, ftruncate(km_fd, keymap_len));
    TEST_ASSERT_EQUAL_INT((ssize_t)keymap_len, write(km_fd, keymap_str, keymap_len));
    free(keymap_str);

    zwp_virtual_keyboard_v1_keymap(state.vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, km_fd, keymap_len);
    close(km_fd);
    wl_display_roundtrip(display);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);

    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Tiling Test Window");

    wl_surface_commit(surface);
    TEST_ASSERT_TRUE(wait_for_configure(display, &state, 1000));

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 640, 480);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    send_shortcut(&state, display, state.super_mod, KEY_LEFT);
    TEST_ASSERT_TRUE(wait_for_configure(display, &state, 1000));
    TEST_ASSERT_EQUAL_INT32(640, state.configured_width);
    TEST_ASSERT_EQUAL_INT32(692, state.configured_height);

    send_shortcut(&state, display, state.super_mod, KEY_DOWN);
    TEST_ASSERT_TRUE(wait_for_configure(display, &state, 1000));
    TEST_ASSERT_EQUAL_INT32(640, state.configured_width);
    TEST_ASSERT_EQUAL_INT32(480, state.configured_height);

    send_shortcut(&state, display, state.super_mod, KEY_RIGHT);
    TEST_ASSERT_TRUE(wait_for_configure(display, &state, 1000));
    TEST_ASSERT_EQUAL_INT32(640, state.configured_width);
    TEST_ASSERT_EQUAL_INT32(692, state.configured_height);

    send_shortcut(&state, display, state.super_mod, KEY_DOWN);
    TEST_ASSERT_TRUE(wait_for_configure(display, &state, 1000));
    TEST_ASSERT_EQUAL_INT32(640, state.configured_width);
    TEST_ASSERT_EQUAL_INT32(480, state.configured_height);

    send_shortcut(&state, display, state.super_mod | state.alt_mod, KEY_LEFT);
    TEST_ASSERT_TRUE(wait_for_configure(display, &state, 1000));
    TEST_ASSERT_EQUAL_INT32(640, state.configured_width);
    TEST_ASSERT_EQUAL_INT32(332, state.configured_height);

    send_shortcut(&state, display, state.super_mod, KEY_DOWN);
    TEST_ASSERT_TRUE(wait_for_configure(display, &state, 1000));
    TEST_ASSERT_EQUAL_INT32(640, state.configured_width);
    TEST_ASSERT_EQUAL_INT32(480, state.configured_height);

    zwp_virtual_keyboard_v1_destroy(state.vk);
    xkb_keymap_unref(state.xkb_map);
    xkb_context_unref(state.xkb_ctx);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    wl_display_roundtrip(display);

    zwp_virtual_keyboard_manager_v1_destroy(state.vk_mgr);
    xdg_wm_base_destroy(state.wm_base);
    wl_seat_destroy(state.seat);
    wl_shm_destroy(state.shm);
    wl_compositor_destroy(state.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    kill(pid, SIGTERM);
    server_pid = -1;
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tiling_shortcuts_and_snapping);
    return UNITY_END();
}
