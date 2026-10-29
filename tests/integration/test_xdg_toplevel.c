#include "unity.h"
#include <stdbool.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct client_state {
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *decor_mgr;
    bool configured;
    bool decor_configured;
    uint32_t decor_mode;
    uint32_t last_serial;
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void decor_configure(void *data, struct zxdg_toplevel_decoration_v1 *decor, uint32_t mode) {
    (void)decor;
    struct client_state *state = data;
    state->decor_configured = true;
    state->decor_mode = mode;
}

static const struct zxdg_toplevel_decoration_v1_listener decor_listener = {
    .configure = decor_configure,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    struct client_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, state);
    } else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
        state->decor_mgr = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct client_state *state = data;
    state->configured = true;
    state->last_serial = serial;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height, struct wl_array *states) {
    (void)toplevel;
    (void)states;
    printf("xdg_toplevel_configure: width=%d, height=%d\n", width, height);
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)data;
    (void)toplevel;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

void setUp(void) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || access(runtime_dir, W_OK) != 0) {
        char fallback[256];
        snprintf(fallback, sizeof(fallback), "/tmp/north-runtime-%d", (int)getuid());
        mkdir(fallback, 0700);
        setenv("XDG_RUNTIME_DIR", fallback, 1);
    }
}
void tearDown(void) {}

void test_xdg_toplevel_configure_cycle(void) {
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
    if (nl) *nl = '\0';

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.wm_base);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);

    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);

    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Test Window");

    struct zxdg_toplevel_decoration_v1 *decor = NULL;
    if (state.decor_mgr) {
        decor = zxdg_decoration_manager_v1_get_toplevel_decoration(state.decor_mgr, toplevel);
        TEST_ASSERT_NOT_NULL(decor);
        zxdg_toplevel_decoration_v1_add_listener(decor, &decor_listener, &state);
        zxdg_toplevel_decoration_v1_set_mode(decor, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.configured);
    if (decor) {
        TEST_ASSERT_TRUE(state.decor_configured);
        TEST_ASSERT_EQUAL_UINT32(ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE, state.decor_mode);
    }

    state.configured = false;
    xdg_toplevel_set_maximized(toplevel);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.configured);

    if (decor) {
        zxdg_toplevel_decoration_v1_destroy(decor);
    }

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);

    if (state.decor_mgr) {
        zxdg_decoration_manager_v1_destroy(state.decor_mgr);
    }
    xdg_wm_base_destroy(state.wm_base);
    wl_compositor_destroy(state.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_xdg_toplevel_configure_cycle);
    return UNITY_END();
}
