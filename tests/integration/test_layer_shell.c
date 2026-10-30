#define _GNU_SOURCE
#include "unity.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

struct client_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zwlr_layer_shell_v1 *layer_shell;

    bool layer_configured;
    uint32_t layer_serial;
    uint32_t layer_width;
    uint32_t layer_height;

    bool toplevel_configured;
    uint32_t toplevel_width;
    uint32_t toplevel_height;
    uint32_t toplevel_serial;
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t width, uint32_t height)
{
    (void)surface;
    struct client_state *state = data;
    state->layer_configured = true;
    state->layer_serial = serial;
    state->layer_width = width;
    state->layer_height = height;
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
    (void)data;
    (void)surface;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    struct client_state *state = data;
    state->toplevel_configured = true;
    state->toplevel_serial = serial;
    xdg_surface_ack_configure(surface, serial);
}

static const struct xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                                   int32_t height, struct wl_array *states)
{
    (void)toplevel;
    (void)states;
    struct client_state *state = data;
    if (width > 0 && height > 0) {
        state->toplevel_width = width;
        state->toplevel_height = height;
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
    } else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
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
    int fd = memfd_create("test-shm", MFD_CLOEXEC);
    if (fd < 0)
        return NULL;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
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

void tearDown(void) {}

void test_layer_shell_exclusive_zone_reduces_usable_area(void)
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
    if (nl)
        *nl = '\0';

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);
    TEST_ASSERT_NOT_NULL(state.layer_shell);

    /* Create layer shell panel on bottom layer */
    struct wl_surface *layer_wl_surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(layer_wl_surface);

    struct zwlr_layer_surface_v1 *layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        state.layer_shell, layer_wl_surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "panel");
    TEST_ASSERT_NOT_NULL(layer_surface);
    zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener, &state);

    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(layer_surface, anchors);
    zwlr_layer_surface_v1_set_size(layer_surface, 0, 30);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, 30);

    wl_surface_commit(layer_wl_surface);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.layer_configured);
    TEST_ASSERT_EQUAL_UINT32(1280, state.layer_width);
    TEST_ASSERT_EQUAL_UINT32(30, state.layer_height);

    zwlr_layer_surface_v1_ack_configure(layer_surface, state.layer_serial);
    struct wl_buffer *panel_buf = create_shm_buffer(state.shm, 1280, 30);
    TEST_ASSERT_NOT_NULL(panel_buf);
    wl_surface_attach(layer_wl_surface, panel_buf, 0, 0);
    wl_surface_commit(layer_wl_surface);
    wl_display_roundtrip(display);

    /* Create regular xdg_toplevel and maximize it */
    struct wl_surface *win_surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(win_surface);

    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, win_surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);

    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Maximize Test");

    wl_surface_commit(win_surface);
    wl_display_roundtrip(display);

    struct wl_buffer *win_buf = create_shm_buffer(state.shm, 200, 200);
    TEST_ASSERT_NOT_NULL(win_buf);
    wl_surface_attach(win_surface, win_buf, 0, 0);
    wl_surface_commit(win_surface);
    wl_display_roundtrip(display);

    /* Maximize toplevel */
    state.toplevel_configured = false;
    xdg_toplevel_set_maximized(toplevel);
    wl_surface_commit(win_surface);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.toplevel_configured);
    TEST_ASSERT_EQUAL_UINT32(1280, state.toplevel_width);
    /* Usable height accounts for panel exclusive zone and titlebar height */
    TEST_ASSERT_EQUAL_UINT32(662, state.toplevel_height);

    /* Cleanup */
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(win_surface);
    wl_buffer_destroy(win_buf);

    zwlr_layer_surface_v1_destroy(layer_surface);
    wl_surface_destroy(layer_wl_surface);
    wl_buffer_destroy(panel_buf);

    zwlr_layer_shell_v1_destroy(state.layer_shell);
    xdg_wm_base_destroy(state.wm_base);
    wl_shm_destroy(state.shm);
    wl_compositor_destroy(state.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_layer_shell_exclusive_zone_reduces_usable_area);
    return UNITY_END();
}
