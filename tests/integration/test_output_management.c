#define _GNU_SOURCE
#include "unity.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

#define MAX_HEADS 8
#define MAX_MODES 16

struct test_mode {
    struct zwlr_output_mode_v1 *proxy;
    int32_t width;
    int32_t height;
    int32_t refresh;
    bool preferred;
};

struct test_head {
    struct zwlr_output_head_v1 *proxy;
    char name[64];
    char description[128];
    int32_t enabled;
    int32_t x;
    int32_t y;
    wl_fixed_t scale;
    int32_t transform;
    struct test_mode modes[MAX_MODES];
    int mode_count;
    struct test_mode *current_mode;
    bool finished;
};

struct config_feedback {
    bool succeeded;
    bool failed;
    bool cancelled;
};

struct client_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zwlr_output_manager_v1 *output_mgr;

    struct test_head heads[MAX_HEADS];
    int head_count;
    uint32_t serial;
    bool done_received;

    bool xdg_configured;
    uint32_t last_serial;
};

static void mode_handle_size(void *data, struct zwlr_output_mode_v1 *mode, int32_t width,
                             int32_t height)
{
    (void)mode;
    struct test_mode *m = data;
    m->width = width;
    m->height = height;
}

static void mode_handle_refresh(void *data, struct zwlr_output_mode_v1 *mode, int32_t refresh)
{
    (void)mode;
    struct test_mode *m = data;
    m->refresh = refresh;
}

static void mode_handle_preferred(void *data, struct zwlr_output_mode_v1 *mode)
{
    (void)mode;
    struct test_mode *m = data;
    m->preferred = true;
}

static void mode_handle_finished(void *data, struct zwlr_output_mode_v1 *mode)
{
    (void)data;
    (void)mode;
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
    .size = mode_handle_size,
    .refresh = mode_handle_refresh,
    .preferred = mode_handle_preferred,
    .finished = mode_handle_finished,
};

static void head_handle_name(void *data, struct zwlr_output_head_v1 *head, const char *name)
{
    (void)head;
    struct test_head *h = data;
    if (name) {
        strncpy(h->name, name, sizeof(h->name) - 1);
    }
}

static void head_handle_description(void *data, struct zwlr_output_head_v1 *head,
                                    const char *description)
{
    (void)head;
    struct test_head *h = data;
    if (description) {
        strncpy(h->description, description, sizeof(h->description) - 1);
    }
}

static void head_handle_physical_size(void *data, struct zwlr_output_head_v1 *head, int32_t width,
                                      int32_t height)
{
    (void)data;
    (void)head;
    (void)width;
    (void)height;
}

static void head_handle_mode(void *data, struct zwlr_output_head_v1 *head,
                             struct zwlr_output_mode_v1 *mode)
{
    (void)head;
    struct test_head *h = data;
    if (h->mode_count < MAX_MODES) {
        struct test_mode *m = &h->modes[h->mode_count++];
        memset(m, 0, sizeof(*m));
        m->proxy = mode;
        zwlr_output_mode_v1_add_listener(mode, &mode_listener, m);
    }
}

static void head_handle_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled)
{
    (void)head;
    struct test_head *h = data;
    h->enabled = enabled;
}

static void head_handle_current_mode(void *data, struct zwlr_output_head_v1 *head,
                                     struct zwlr_output_mode_v1 *mode)
{
    (void)head;
    struct test_head *h = data;
    for (int i = 0; i < h->mode_count; i++) {
        if (h->modes[i].proxy == mode) {
            h->current_mode = &h->modes[i];
            break;
        }
    }
}

static void head_handle_position(void *data, struct zwlr_output_head_v1 *head, int32_t x, int32_t y)
{
    (void)head;
    struct test_head *h = data;
    h->x = x;
    h->y = y;
}

static void head_handle_transform(void *data, struct zwlr_output_head_v1 *head, int32_t transform)
{
    (void)head;
    struct test_head *h = data;
    h->transform = transform;
}

static void head_handle_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale)
{
    (void)head;
    struct test_head *h = data;
    h->scale = scale;
}

static void head_handle_finished(void *data, struct zwlr_output_head_v1 *head)
{
    (void)head;
    struct test_head *h = data;
    h->finished = true;
}

static void head_handle_make(void *data, struct zwlr_output_head_v1 *head, const char *make)
{
    (void)data;
    (void)head;
    (void)make;
}

static void head_handle_model(void *data, struct zwlr_output_head_v1 *head, const char *model)
{
    (void)data;
    (void)head;
    (void)model;
}

static void head_handle_serial_number(void *data, struct zwlr_output_head_v1 *head,
                                      const char *serial_number)
{
    (void)data;
    (void)head;
    (void)serial_number;
}

static void head_handle_adaptive_sync(void *data, struct zwlr_output_head_v1 *head, uint32_t state)
{
    (void)data;
    (void)head;
    (void)state;
}

static const struct zwlr_output_head_v1_listener head_listener = {
    .name = head_handle_name,
    .description = head_handle_description,
    .physical_size = head_handle_physical_size,
    .mode = head_handle_mode,
    .enabled = head_handle_enabled,
    .current_mode = head_handle_current_mode,
    .position = head_handle_position,
    .transform = head_handle_transform,
    .scale = head_handle_scale,
    .finished = head_handle_finished,
    .make = head_handle_make,
    .model = head_handle_model,
    .serial_number = head_handle_serial_number,
    .adaptive_sync = head_handle_adaptive_sync,
};

static void mgr_handle_head(void *data, struct zwlr_output_manager_v1 *mgr,
                            struct zwlr_output_head_v1 *head)
{
    (void)mgr;
    struct client_state *state = data;
    if (state->head_count < MAX_HEADS) {
        struct test_head *h = &state->heads[state->head_count++];
        memset(h, 0, sizeof(*h));
        h->proxy = head;
        zwlr_output_head_v1_add_listener(head, &head_listener, h);
    }
}

static void mgr_handle_done(void *data, struct zwlr_output_manager_v1 *mgr, uint32_t serial)
{
    (void)mgr;
    struct client_state *state = data;
    state->serial = serial;
    state->done_received = true;
}

static void mgr_handle_finished(void *data, struct zwlr_output_manager_v1 *mgr)
{
    (void)data;
    (void)mgr;
}

static const struct zwlr_output_manager_v1_listener mgr_listener = {
    .head = mgr_handle_head,
    .done = mgr_handle_done,
    .finished = mgr_handle_finished,
};

static void config_handle_succeeded(void *data, struct zwlr_output_configuration_v1 *config)
{
    (void)config;
    struct config_feedback *fb = data;
    fb->succeeded = true;
}

static void config_handle_failed(void *data, struct zwlr_output_configuration_v1 *config)
{
    (void)config;
    struct config_feedback *fb = data;
    fb->failed = true;
}

static void config_handle_cancelled(void *data, struct zwlr_output_configuration_v1 *config)
{
    (void)config;
    struct config_feedback *fb = data;
    fb->cancelled = true;
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
    .succeeded = config_handle_succeeded,
    .failed = config_handle_failed,
    .cancelled = config_handle_cancelled,
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
    struct client_state *state = data;
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
    struct client_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, state);
    } else if (strcmp(interface, "zwlr_output_manager_v1") == 0) {
        state->output_mgr = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 4);
        zwlr_output_manager_v1_add_listener(state->output_mgr, &mgr_listener, state);
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
    int fd = memfd_create("test_shm", MFD_CLOEXEC);
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

static pid_t launch_compositor(int headless_outputs, char *socket_name_out, size_t socket_name_size)
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
        if (headless_outputs > 0) {
            char outputs_str[16];
            snprintf(outputs_str, sizeof(outputs_str), "%d", headless_outputs);
            setenv("WLR_HEADLESS_OUTPUTS", outputs_str, 1);
        }

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

static void stop_compositor(pid_t pid)
{
    if (pid > 0) {
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
        TEST_ASSERT_TRUE(WIFEXITED(status));
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
    }
}

static void cleanup_client_state(struct client_state *state, struct wl_registry *registry,
                                 struct wl_display *display)
{
    for (int i = 0; i < state->head_count; i++) {
        for (int m = 0; m < state->heads[i].mode_count; m++) {
            zwlr_output_mode_v1_destroy(state->heads[i].modes[m].proxy);
        }
        zwlr_output_head_v1_destroy(state->heads[i].proxy);
    }
    if (state->output_mgr) {
        zwlr_output_manager_v1_destroy(state->output_mgr);
    }
    if (state->shm) {
        wl_shm_destroy(state->shm);
    }
    if (state->compositor) {
        wl_compositor_destroy(state->compositor);
    }
    if (state->wm_base) {
        xdg_wm_base_destroy(state->wm_base);
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

void tearDown(void) {}

void test_output_manager_head_discovery(void)
{
    char socket_name[128] = {0};
    pid_t pid = launch_compositor(1, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.output_mgr);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_TRUE(state.serial > 0);
    TEST_ASSERT_EQUAL_INT(1, state.head_count);

    struct test_head *head = &state.heads[0];
    TEST_ASSERT_TRUE(strlen(head->name) > 0);
    TEST_ASSERT_EQUAL_INT(1, head->enabled);
    TEST_ASSERT_EQUAL_INT(0, head->x);
    TEST_ASSERT_EQUAL_INT(0, head->y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, wl_fixed_to_double(head->scale));

    cleanup_client_state(&state, registry, display);
    stop_compositor(pid);
}

void test_output_manager_test_configuration_valid(void)
{
    char socket_name[128] = {0};
    pid_t pid = launch_compositor(1, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.output_mgr);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(1, state.head_count);

    struct zwlr_output_configuration_v1 *config =
        zwlr_output_manager_v1_create_configuration(state.output_mgr, state.serial);
    TEST_ASSERT_NOT_NULL(config);

    struct config_feedback fb = {0};
    zwlr_output_configuration_v1_add_listener(config, &config_listener, &fb);

    struct zwlr_output_configuration_head_v1 *config_head =
        zwlr_output_configuration_v1_enable_head(config, state.heads[0].proxy);
    TEST_ASSERT_NOT_NULL(config_head);

    zwlr_output_configuration_head_v1_set_custom_mode(config_head, 1024, 768, 60000);
    zwlr_output_configuration_head_v1_set_position(config_head, 0, 0);
    zwlr_output_configuration_head_v1_set_scale(config_head, wl_fixed_from_double(1.0));

    zwlr_output_configuration_v1_test(config);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(fb.succeeded);
    TEST_ASSERT_FALSE(fb.failed);
    TEST_ASSERT_FALSE(fb.cancelled);

    zwlr_output_configuration_head_v1_destroy(config_head);
    zwlr_output_configuration_v1_destroy(config);

    cleanup_client_state(&state, registry, display);
    stop_compositor(pid);
}

void test_output_manager_test_configuration_invalid(void)
{
    char socket_name[128] = {0};
    pid_t pid = launch_compositor(1, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.output_mgr);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(1, state.head_count);

    struct zwlr_output_configuration_v1 *config =
        zwlr_output_manager_v1_create_configuration(state.output_mgr, state.serial);
    TEST_ASSERT_NOT_NULL(config);

    struct config_feedback fb = {0};
    zwlr_output_configuration_v1_add_listener(config, &config_listener, &fb);

    struct zwlr_output_configuration_head_v1 *config_head =
        zwlr_output_configuration_v1_enable_head(config, state.heads[0].proxy);
    TEST_ASSERT_NOT_NULL(config_head);

    /* Request an unsupported custom resolution below minimum limits (32x32),
     * which wlroots allows as a valid request but compositor validation rejects. */
    zwlr_output_configuration_head_v1_set_custom_mode(config_head, 32, 32, 60000);
    zwlr_output_configuration_head_v1_set_position(config_head, 0, 0);

    zwlr_output_configuration_v1_test(config);
    wl_display_roundtrip(display);

    TEST_ASSERT_FALSE(fb.succeeded);
    TEST_ASSERT_TRUE(fb.failed);

    zwlr_output_configuration_head_v1_destroy(config_head);
    zwlr_output_configuration_v1_destroy(config);

    cleanup_client_state(&state, registry, display);
    stop_compositor(pid);
}

void test_output_manager_apply_mode_and_scale(void)
{
    char socket_name[128] = {0};
    pid_t pid = launch_compositor(1, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.output_mgr);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(1, state.head_count);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Scale Test Window");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.xdg_configured);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct zwlr_output_configuration_v1 *config =
        zwlr_output_manager_v1_create_configuration(state.output_mgr, state.serial);
    TEST_ASSERT_NOT_NULL(config);

    struct config_feedback fb = {0};
    zwlr_output_configuration_v1_add_listener(config, &config_listener, &fb);

    struct zwlr_output_configuration_head_v1 *config_head =
        zwlr_output_configuration_v1_enable_head(config, state.heads[0].proxy);
    TEST_ASSERT_NOT_NULL(config_head);

    zwlr_output_configuration_head_v1_set_custom_mode(config_head, 1600, 900, 60000);
    zwlr_output_configuration_head_v1_set_position(config_head, 200, 100);
    zwlr_output_configuration_head_v1_set_scale(config_head, wl_fixed_from_double(1.5));

    state.done_received = false;
    zwlr_output_configuration_v1_apply(config);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(fb.succeeded);
    TEST_ASSERT_FALSE(fb.failed);
    zwlr_output_configuration_head_v1_destroy(config_head);
    zwlr_output_configuration_v1_destroy(config);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);

    struct test_head *head = &state.heads[0];
    TEST_ASSERT_EQUAL_INT(1, head->enabled);
    TEST_ASSERT_EQUAL_INT(200, head->x);
    TEST_ASSERT_EQUAL_INT(100, head->y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, wl_fixed_to_double(head->scale));

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    cleanup_client_state(&state, registry, display);
    stop_compositor(pid);
}

void test_output_manager_disable_enable(void)
{
    char socket_name[128] = {0};
    pid_t pid = launch_compositor(2, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.output_mgr);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(2, state.head_count);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Output Mgmt Window");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.xdg_configured);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct zwlr_output_configuration_v1 *config =
        zwlr_output_manager_v1_create_configuration(state.output_mgr, state.serial);
    TEST_ASSERT_NOT_NULL(config);

    struct config_feedback fb = {0};
    zwlr_output_configuration_v1_add_listener(config, &config_listener, &fb);

    zwlr_output_configuration_v1_disable_head(config, state.heads[0].proxy);

    state.done_received = false;
    zwlr_output_configuration_v1_apply(config);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(fb.succeeded);
    TEST_ASSERT_FALSE(fb.failed);
    zwlr_output_configuration_v1_destroy(config);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(0, state.heads[0].enabled);
    TEST_ASSERT_EQUAL_INT(1, state.heads[1].enabled);

    memset(&fb, 0, sizeof(fb));
    config = zwlr_output_manager_v1_create_configuration(state.output_mgr, state.serial);
    TEST_ASSERT_NOT_NULL(config);
    zwlr_output_configuration_v1_add_listener(config, &config_listener, &fb);

    struct zwlr_output_configuration_head_v1 *config_head =
        zwlr_output_configuration_v1_enable_head(config, state.heads[0].proxy);
    TEST_ASSERT_NOT_NULL(config_head);

    zwlr_output_configuration_head_v1_set_custom_mode(config_head, 1280, 720, 60000);
    zwlr_output_configuration_head_v1_set_position(config_head, 1280, 0);
    zwlr_output_configuration_head_v1_set_scale(config_head, wl_fixed_from_double(1.0));

    state.done_received = false;
    zwlr_output_configuration_v1_apply(config);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(fb.succeeded);
    TEST_ASSERT_FALSE(fb.failed);
    zwlr_output_configuration_head_v1_destroy(config_head);
    zwlr_output_configuration_v1_destroy(config);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(1, state.heads[0].enabled);
    TEST_ASSERT_EQUAL_INT(1280, state.heads[0].x);
    TEST_ASSERT_EQUAL_INT(0, state.heads[0].y);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    cleanup_client_state(&state, registry, display);
    stop_compositor(pid);
}

void test_output_manager_disable_all_and_reenable(void)
{
    char socket_name[128] = {0};
    pid_t pid = launch_compositor(2, socket_name, sizeof(socket_name));

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct client_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(state.output_mgr);
    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.wm_base);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(2, state.head_count);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, surface);
    TEST_ASSERT_NOT_NULL(xdg_surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &state);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    TEST_ASSERT_NOT_NULL(toplevel);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(toplevel, "Headless All Disabled Window");

    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.xdg_configured);

    struct wl_buffer *buffer = create_shm_buffer(state.shm, 400, 300);
    TEST_ASSERT_NOT_NULL(buffer);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    struct zwlr_output_configuration_v1 *config =
        zwlr_output_manager_v1_create_configuration(state.output_mgr, state.serial);
    TEST_ASSERT_NOT_NULL(config);

    struct config_feedback fb = {0};
    zwlr_output_configuration_v1_add_listener(config, &config_listener, &fb);

    zwlr_output_configuration_v1_disable_head(config, state.heads[0].proxy);
    zwlr_output_configuration_v1_disable_head(config, state.heads[1].proxy);

    state.done_received = false;
    zwlr_output_configuration_v1_apply(config);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(fb.succeeded);
    TEST_ASSERT_FALSE(fb.failed);
    zwlr_output_configuration_v1_destroy(config);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(0, state.heads[0].enabled);
    TEST_ASSERT_EQUAL_INT(0, state.heads[1].enabled);

    memset(&fb, 0, sizeof(fb));
    config = zwlr_output_manager_v1_create_configuration(state.output_mgr, state.serial);
    TEST_ASSERT_NOT_NULL(config);
    zwlr_output_configuration_v1_add_listener(config, &config_listener, &fb);

    struct zwlr_output_configuration_head_v1 *config_head =
        zwlr_output_configuration_v1_enable_head(config, state.heads[0].proxy);
    TEST_ASSERT_NOT_NULL(config_head);

    zwlr_output_configuration_head_v1_set_custom_mode(config_head, 1280, 720, 60000);
    zwlr_output_configuration_head_v1_set_position(config_head, 0, 0);
    zwlr_output_configuration_head_v1_set_scale(config_head, wl_fixed_from_double(1.0));

    state.done_received = false;
    zwlr_output_configuration_v1_apply(config);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(fb.succeeded);
    TEST_ASSERT_FALSE(fb.failed);
    zwlr_output_configuration_head_v1_destroy(config_head);
    zwlr_output_configuration_v1_destroy(config);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(state.done_received);
    TEST_ASSERT_EQUAL_INT(1, state.heads[0].enabled);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    cleanup_client_state(&state, registry, display);
    stop_compositor(pid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_output_manager_head_discovery);
    RUN_TEST(test_output_manager_test_configuration_valid);
    RUN_TEST(test_output_manager_test_configuration_invalid);
    RUN_TEST(test_output_manager_apply_mode_and_scale);
    RUN_TEST(test_output_manager_disable_enable);
    RUN_TEST(test_output_manager_disable_all_and_reenable);
    return UNITY_END();
}
