#define _GNU_SOURCE
#include "unity.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
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

struct client_state {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct zwp_virtual_keyboard_manager_v1 *vk_mgr;
    struct zwp_virtual_keyboard_v1 *vk;
    struct zwlr_virtual_pointer_manager_v1 *vp_mgr;
    struct zwlr_virtual_pointer_v1 *vp;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_map;
    uint32_t alt_mod;
    uint32_t shift_mod;
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
    uint32_t *entry;
    wl_array_for_each(entry, states)
    {
        if (*entry == XDG_TOPLEVEL_STATE_ACTIVATED) {
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
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
    } else if (strcmp(interface, "zwp_virtual_keyboard_manager_v1") == 0) {
        state->vk_mgr =
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    } else if (strcmp(interface, "zwlr_virtual_pointer_manager_v1") == 0) {
        state->vp_mgr =
            wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
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

static struct test_window *create_window(struct client_state *state, const char *title, int width,
                                         int height)
{
    struct test_window *win = calloc(1, sizeof(*win));
    TEST_ASSERT_NOT_NULL(win);

    win->surface = wl_compositor_create_surface(state->compositor);
    TEST_ASSERT_NOT_NULL(win->surface);

    win->xdg_surface = xdg_wm_base_get_xdg_surface(state->wm_base, win->surface);
    TEST_ASSERT_NOT_NULL(win->xdg_surface);
    xdg_surface_add_listener(win->xdg_surface, &surface_listener, win);

    win->toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    TEST_ASSERT_NOT_NULL(win->toplevel);
    xdg_toplevel_add_listener(win->toplevel, &toplevel_listener, win);

    if (title) {
        xdg_toplevel_set_title(win->toplevel, title);
    }

    wl_surface_commit(win->surface);
    wl_display_roundtrip(state->display);

    win->buffer = create_shm_buffer(state->shm, width, height);
    TEST_ASSERT_NOT_NULL(win->buffer);

    wl_surface_attach(win->surface, win->buffer, 0, 0);
    wl_surface_commit(win->surface);
    wl_display_roundtrip(state->display);

    return win;
}

static void destroy_window(struct test_window *win)
{
    if (!win) {
        return;
    }
    if (win->toplevel) {
        xdg_toplevel_destroy(win->toplevel);
    }
    if (win->xdg_surface) {
        xdg_surface_destroy(win->xdg_surface);
    }
    if (win->surface) {
        wl_surface_destroy(win->surface);
    }
    if (win->buffer) {
        wl_buffer_destroy(win->buffer);
    }
    free(win);
}

static void setup_virtual_input(struct client_state *state)
{
    TEST_ASSERT_NOT_NULL(state->vk_mgr);
    state->vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(state->vk_mgr, state->seat);
    TEST_ASSERT_NOT_NULL(state->vk);

    state->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state->xkb_ctx);
    struct xkb_rule_names rules = {.rules = "evdev", .model = "pc105", .layout = "us"};
    state->xkb_map = xkb_keymap_new_from_names(state->xkb_ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    TEST_ASSERT_NOT_NULL(state->xkb_map);

    xkb_mod_index_t alt_idx = xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_ALT);
    xkb_mod_index_t shift_idx = xkb_keymap_mod_get_index(state->xkb_map, XKB_MOD_NAME_SHIFT);
    state->alt_mod = (1 << alt_idx);
    state->shift_mod = (1 << shift_idx);

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
    wl_display_roundtrip(state->display);

    if (state->vp_mgr) {
        state->vp =
            zwlr_virtual_pointer_manager_v1_create_virtual_pointer(state->vp_mgr, state->seat);
    }
}

static void send_alt_tab(struct client_state *state)
{
    zwp_virtual_keyboard_v1_modifiers(state->vk, state->alt_mod, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_TAB, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_TAB, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(state->display);
}

static void send_tab(struct client_state *state)
{
    zwp_virtual_keyboard_v1_modifiers(state->vk, state->alt_mod, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_TAB, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_TAB, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(state->display);
}

static void send_shift_tab(struct client_state *state)
{
    zwp_virtual_keyboard_v1_modifiers(state->vk, state->alt_mod | state->shift_mod, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_TAB, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_TAB, WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_modifiers(state->vk, state->alt_mod, 0, 0, 0);
    wl_display_flush(state->display);
}

static void release_alt(struct client_state *state)
{
    zwp_virtual_keyboard_v1_modifiers(state->vk, 0, 0, 0, 0);
    wl_display_flush(state->display);
}

static void send_escape(struct client_state *state)
{
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_ESC, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(state->vk, 0, KEY_ESC, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(state->display);
}

static void send_pointer_click(struct client_state *state, uint32_t x, uint32_t y)
{
    if (!state->vp) {
        return;
    }
    zwlr_virtual_pointer_v1_motion_absolute(state->vp, 0, x, y, 1280, 720);
    zwlr_virtual_pointer_v1_frame(state->vp);
    zwlr_virtual_pointer_v1_button(state->vp, 0, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(state->vp);
    zwlr_virtual_pointer_v1_button(state->vp, 0, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(state->vp);
    wl_display_flush(state->display);
}

static pid_t launch_compositor(char *socket_name_out, size_t socket_name_size)
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

    close(pipe_fds[1]);

    char socket_name[128] = {0};
    ssize_t read_bytes = read(pipe_fds[0], socket_name, sizeof(socket_name) - 1);
    close(pipe_fds[0]);

    TEST_ASSERT_TRUE(read_bytes > 0);
    char *nl = strchr(socket_name, '\n');
    if (nl) {
        *nl = '\0';
    }

    strncpy(socket_name_out, socket_name, socket_name_size - 1);
    socket_name_out[socket_name_size - 1] = '\0';
    return pid;
}

static bool wait_for_activation(struct wl_display *display, struct test_window *win,
                                bool expected_activated, int timeout_ms)
{
    int elapsed = 0;
    while (win->is_activated != expected_activated && elapsed < timeout_ms) {
        wl_display_roundtrip(display);
        if (win->is_activated == expected_activated) {
            return true;
        }
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
        nanosleep(&ts, NULL);
        elapsed += 10;
    }
    return win->is_activated == expected_activated;
}

static void cleanup_client(struct client_state *state, struct wl_registry *registry)
{
    if (state->vp) {
        zwlr_virtual_pointer_v1_destroy(state->vp);
        state->vp = NULL;
    }
    if (state->vp_mgr) {
        zwlr_virtual_pointer_manager_v1_destroy(state->vp_mgr);
        state->vp_mgr = NULL;
    }
    if (state->vk) {
        zwp_virtual_keyboard_v1_destroy(state->vk);
        state->vk = NULL;
    }
    if (state->vk_mgr) {
        zwp_virtual_keyboard_manager_v1_destroy(state->vk_mgr);
        state->vk_mgr = NULL;
    }
    if (state->xkb_map) {
        xkb_keymap_unref(state->xkb_map);
        state->xkb_map = NULL;
    }
    if (state->xkb_ctx) {
        xkb_context_unref(state->xkb_ctx);
        state->xkb_ctx = NULL;
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
    if (state->seat) {
        wl_seat_destroy(state->seat);
        state->seat = NULL;
    }
    if (registry) {
        wl_registry_destroy(registry);
    }
    if (state->display) {
        wl_display_disconnect(state->display);
        state->display = NULL;
    }
}

void test_switcher_open_and_cycle(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    setup_virtual_input(&state);

    struct test_window *win1 = create_window(&state, "Window 1", 400, 300);
    struct test_window *win2 = create_window(&state, "Window 2", 400, 300);
    struct test_window *win3 = create_window(&state, "Window 3", 400, 300);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win3, true, 500));
    TEST_ASSERT_FALSE(win1->is_activated);
    TEST_ASSERT_FALSE(win2->is_activated);

    send_alt_tab(&state);
    send_tab(&state);
    release_alt(&state);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win1, true, 500));
    TEST_ASSERT_FALSE(win3->is_activated);

    send_alt_tab(&state);
    send_shift_tab(&state);
    release_alt(&state);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win1, true, 500));

    send_shift_tab(&state);
    release_alt(&state);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win2, true, 500));
    TEST_ASSERT_FALSE(win1->is_activated);

    destroy_window(win1);
    destroy_window(win2);
    destroy_window(win3);
    cleanup_client(&state, registry);
}

void test_switcher_commit_on_modifier_release(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    setup_virtual_input(&state);

    struct test_window *win1 = create_window(&state, "Window A", 400, 300);
    struct test_window *win2 = create_window(&state, "Window B", 400, 300);
    struct test_window *win3 = create_window(&state, "Window C", 400, 300);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win3, true, 500));

    send_alt_tab(&state);
    release_alt(&state);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win2, true, 500));
    TEST_ASSERT_FALSE(win3->is_activated);

    destroy_window(win1);
    destroy_window(win2);
    destroy_window(win3);
    cleanup_client(&state, registry);
}

void test_switcher_cancel_on_escape(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    setup_virtual_input(&state);

    struct test_window *win1 = create_window(&state, "Window A", 400, 300);
    struct test_window *win2 = create_window(&state, "Window B", 400, 300);
    struct test_window *win3 = create_window(&state, "Window C", 400, 300);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win3, true, 500));

    send_alt_tab(&state);
    send_escape(&state);
    release_alt(&state);

    wl_display_roundtrip(state.display);
    TEST_ASSERT_TRUE(win3->is_activated);
    TEST_ASSERT_FALSE(win2->is_activated);
    TEST_ASSERT_FALSE(win1->is_activated);

    destroy_window(win1);
    destroy_window(win2);
    destroy_window(win3);
    cleanup_client(&state, registry);
}

void test_switcher_click_selection(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    setup_virtual_input(&state);

    struct test_window *win1 = create_window(&state, "Win 1", 400, 300);
    struct test_window *win2 = create_window(&state, "Win 2", 400, 300);
    struct test_window *win3 = create_window(&state, "Win 3", 400, 300);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win3, true, 500));

    send_alt_tab(&state);
    send_pointer_click(&state, 640, 360);
    release_alt(&state);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win2, true, 500));

    send_alt_tab(&state);
    send_pointer_click(&state, 50, 50);
    release_alt(&state);

    wl_display_roundtrip(state.display);
    TEST_ASSERT_TRUE(win2->is_activated);

    destroy_window(win1);
    destroy_window(win2);
    destroy_window(win3);
    cleanup_client(&state, registry);
}

void test_switcher_single_window_noop(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    setup_virtual_input(&state);

    send_alt_tab(&state);
    send_tab(&state);
    release_alt(&state);
    wl_display_roundtrip(state.display);

    struct test_window *win1 = create_window(&state, "Solo Window", 400, 300);
    TEST_ASSERT_TRUE(wait_for_activation(state.display, win1, true, 500));

    send_alt_tab(&state);
    send_tab(&state);
    release_alt(&state);

    wl_display_roundtrip(state.display);
    TEST_ASSERT_TRUE(win1->is_activated);

    destroy_window(win1);
    cleanup_client(&state, registry);
}

void test_switcher_window_unmap_mid_cycle(void)
{
    char socket_name[128] = {0};
    server_pid = launch_compositor(socket_name, sizeof(socket_name));

    struct client_state state = {0};
    state.display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(state.display);

    struct wl_registry *registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    setup_virtual_input(&state);

    struct test_window *win1 = create_window(&state, "Win 1", 400, 300);
    struct test_window *win2 = create_window(&state, "Win 2", 400, 300);
    struct test_window *win3 = create_window(&state, "Win 3", 400, 300);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win3, true, 500));

    send_alt_tab(&state);

    /* Unmap win2 while switcher is active */
    wl_surface_attach(win2->surface, NULL, 0, 0);
    wl_surface_commit(win2->surface);
    wl_display_roundtrip(state.display);

    release_alt(&state);

    TEST_ASSERT_TRUE(wait_for_activation(state.display, win1, true, 500) ||
                     wait_for_activation(state.display, win3, true, 500));

    destroy_window(win1);
    destroy_window(win2);
    destroy_window(win3);
    cleanup_client(&state, registry);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_switcher_open_and_cycle);
    RUN_TEST(test_switcher_commit_on_modifier_release);
    RUN_TEST(test_switcher_cancel_on_escape);
    RUN_TEST(test_switcher_click_selection);
    RUN_TEST(test_switcher_single_window_noop);
    RUN_TEST(test_switcher_window_unmap_mid_cycle);
    return UNITY_END();
}
