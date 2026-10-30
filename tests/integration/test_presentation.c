#define _GNU_SOURCE
#include "fractional-scale-v1-client-protocol.h"
#include "linux-drm-syncobj-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"
#include "unity.h"
#include "viewporter-client-protocol.h"
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

struct client_presentation_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_mgr;
    struct wp_presentation *presentation;
    struct wp_tearing_control_manager_v1 *tearing_mgr;
    struct wp_linux_drm_syncobj_manager_v1 *syncobj_mgr;

    bool xdg_configured;
    uint32_t last_serial;

    bool clock_id_received;
    uint32_t clock_id;

    bool preferred_scale_received;
    uint32_t preferred_scale;

    bool presentation_feedback_received;
    bool presented;
    bool discarded;
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
    struct client_presentation_state *state = data;
    state->xdg_configured = true;
    state->last_serial = serial;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
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

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void presentation_clock_id(void *data, struct wp_presentation *presentation, uint32_t clk_id)
{
    (void)presentation;
    struct client_presentation_state *state = data;
    state->clock_id_received = true;
    state->clock_id = clk_id;
}

static const struct wp_presentation_listener presentation_listener = {
    .clock_id = presentation_clock_id,
};

static void fractional_scale_preferred_scale(void *data,
                                             struct wp_fractional_scale_v1 *fractional_scale,
                                             uint32_t scale)
{
    (void)fractional_scale;
    struct client_presentation_state *state = data;
    state->preferred_scale_received = true;
    state->preferred_scale = scale;
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_preferred_scale,
};

static void feedback_sync_output(void *data, struct wp_presentation_feedback *feedback,
                                 struct wl_output *output)
{
    (void)data;
    (void)feedback;
    (void)output;
}

static void feedback_presented(void *data, struct wp_presentation_feedback *feedback,
                               uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec,
                               uint32_t refresh, uint32_t seq_hi, uint32_t seq_lo, uint32_t flags)
{
    (void)feedback;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    (void)refresh;
    (void)seq_hi;
    (void)seq_lo;
    (void)flags;
    struct client_presentation_state *state = data;
    state->presentation_feedback_received = true;
    state->presented = true;
    wp_presentation_feedback_destroy(feedback);
}

static void feedback_discarded(void *data, struct wp_presentation_feedback *feedback)
{
    struct client_presentation_state *state = data;
    state->presentation_feedback_received = true;
    state->discarded = true;
    wp_presentation_feedback_destroy(feedback);
}

static const struct wp_presentation_feedback_listener feedback_listener = {
    .sync_output = feedback_sync_output,
    .presented = feedback_presented,
    .discarded = feedback_discarded,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    (void)version;
    struct client_presentation_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, state);
    } else if (strcmp(interface, "wp_viewporter") == 0) {
        state->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (strcmp(interface, "wp_fractional_scale_manager_v1") == 0) {
        state->fractional_scale_mgr =
            wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp(interface, "wp_presentation") == 0) {
        state->presentation = wl_registry_bind(registry, name, &wp_presentation_interface, 1);
        wp_presentation_add_listener(state->presentation, &presentation_listener, state);
    } else if (strcmp(interface, "wp_tearing_control_manager_v1") == 0) {
        state->tearing_mgr =
            wl_registry_bind(registry, name, &wp_tearing_control_manager_v1_interface, 1);
    } else if (strcmp(interface, "wp_linux_drm_syncobj_manager_v1") == 0) {
        state->syncobj_mgr =
            wl_registry_bind(registry, name, &wp_linux_drm_syncobj_manager_v1_interface, 1);
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

    int fd = memfd_create("test-pres-shm", MFD_CLOEXEC);
    if (fd < 0)
        return NULL;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
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

void test_presentation_and_scaling_lifecycle(void)
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

    struct client_presentation_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);
    TEST_ASSERT_NOT_NULL(state.viewporter);
    TEST_ASSERT_NOT_NULL(state.fractional_scale_mgr);
    TEST_ASSERT_NOT_NULL(state.presentation);
    TEST_ASSERT_NOT_NULL(state.tearing_mgr);

    TEST_ASSERT_TRUE(state.clock_id_received);
    TEST_ASSERT_EQUAL_UINT32(CLOCK_MONOTONIC, state.clock_id);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);

    struct wp_fractional_scale_v1 *fractional =
        wp_fractional_scale_manager_v1_get_fractional_scale(state.fractional_scale_mgr, surface);
    TEST_ASSERT_NOT_NULL(fractional);
    wp_fractional_scale_v1_add_listener(fractional, &fractional_scale_listener, &state);

    struct wp_viewport *viewport = wp_viewporter_get_viewport(state.viewporter, surface);
    TEST_ASSERT_NOT_NULL(viewport);
    wp_viewport_set_source(viewport, wl_fixed_from_int(0), wl_fixed_from_int(0),
                           wl_fixed_from_int(100), wl_fixed_from_int(100));
    wp_viewport_set_destination(viewport, 200, 200);

    struct wp_tearing_control_v1 *tearing =
        wp_tearing_control_manager_v1_get_tearing_control(state.tearing_mgr, surface);
    TEST_ASSERT_NOT_NULL(tearing);
    wp_tearing_control_v1_set_presentation_hint(tearing,
                                                WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC);

    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, &state);

    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &xdg_toplevel_listener, &state);

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.xdg_configured);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 100, 100);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, 100, 100);

    struct wp_presentation_feedback *feedback =
        wp_presentation_feedback(state.presentation, surface);
    TEST_ASSERT_NOT_NULL(feedback);
    wp_presentation_feedback_add_listener(feedback, &feedback_listener, &state);

    wl_surface_commit(surface);
    for (int i = 0; i < 20 && !state.presentation_feedback_received; i++) {
        usleep(10000);
        wl_display_roundtrip(display);
    }

    TEST_ASSERT_TRUE(state.preferred_scale_received);
    TEST_ASSERT_TRUE(state.preferred_scale > 0);

    TEST_ASSERT_TRUE(state.presentation_feedback_received);
    TEST_ASSERT_TRUE(state.presented || state.discarded);

    wp_tearing_control_v1_set_presentation_hint(tearing,
                                                WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC);
    wp_tearing_control_v1_destroy(tearing);
    wp_viewport_destroy(viewport);
    wp_fractional_scale_v1_destroy(fractional);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    wl_display_roundtrip(display);

    wp_tearing_control_manager_v1_destroy(state.tearing_mgr);
    wp_presentation_destroy(state.presentation);
    wp_fractional_scale_manager_v1_destroy(state.fractional_scale_mgr);
    wp_viewporter_destroy(state.viewporter);
    xdg_wm_base_destroy(state.wm_base);
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
    RUN_TEST(test_presentation_and_scaling_lifecycle);
    return UNITY_END();
}
