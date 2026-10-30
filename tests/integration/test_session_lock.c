#define _GNU_SOURCE
#include "ext-session-lock-v1-client-protocol.h"
#include "unity.h"

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

struct test_lock_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_output *output;
    struct ext_session_lock_manager_v1 *lock_manager;
    struct ext_session_lock_v1 *session_lock;
    struct ext_session_lock_surface_v1 *lock_surface;

    bool lock_surface_configured;
    uint32_t configure_serial;
    uint32_t configure_width;
    uint32_t configure_height;

    bool session_locked;
    bool session_finished;
};

static void lock_handle_locked(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    struct test_lock_state *state = data;
    state->session_locked = true;
}

static void lock_handle_finished(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    struct test_lock_state *state = data;
    state->session_finished = true;
}

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = lock_handle_locked,
    .finished = lock_handle_finished,
};

static void lock_surface_configure(void *data, struct ext_session_lock_surface_v1 *surface,
                                   uint32_t serial, uint32_t width, uint32_t height)
{
    (void)surface;
    struct test_lock_state *state = data;
    state->lock_surface_configured = true;
    state->configure_serial = serial;
    state->configure_width = width;
    state->configure_height = height;
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_configure,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    (void)version;
    struct test_lock_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "wl_output") == 0 && !state->output) {
        state->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    } else if (strcmp(interface, "ext_session_lock_manager_v1") == 0) {
        state->lock_manager =
            wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
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
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
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

void test_session_lock_lifecycle(void)
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

    struct test_lock_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.output);
    TEST_ASSERT_NOT_NULL(state.lock_manager);

    state.session_lock = ext_session_lock_manager_v1_lock(state.lock_manager);
    TEST_ASSERT_NOT_NULL(state.session_lock);
    ext_session_lock_v1_add_listener(state.session_lock, &lock_listener, &state);

    struct wl_surface *surface = wl_compositor_create_surface(state.compositor);
    TEST_ASSERT_NOT_NULL(surface);

    state.lock_surface =
        ext_session_lock_v1_get_lock_surface(state.session_lock, surface, state.output);
    TEST_ASSERT_NOT_NULL(state.lock_surface);
    ext_session_lock_surface_v1_add_listener(state.lock_surface, &lock_surface_listener, &state);

    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.lock_surface_configured);
    TEST_ASSERT_EQUAL_UINT32(1280, state.configure_width);
    TEST_ASSERT_EQUAL_UINT32(720, state.configure_height);

    ext_session_lock_surface_v1_ack_configure(state.lock_surface, state.configure_serial);
    struct wl_buffer *buffer = create_shm_buffer(state.shm, 1280, 720);
    TEST_ASSERT_NOT_NULL(buffer);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.session_locked);
    TEST_ASSERT_FALSE(state.session_finished);

    ext_session_lock_surface_v1_destroy(state.lock_surface);
    ext_session_lock_v1_unlock_and_destroy(state.session_lock);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);

    wl_display_roundtrip(display);

    ext_session_lock_manager_v1_destroy(state.lock_manager);
    wl_output_destroy(state.output);
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

void test_session_lock_crash_fail_secure(void)
{
    int pipe_fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(pipe_fds));

    pid_t comp_pid = fork();
    TEST_ASSERT_TRUE(comp_pid >= 0);

    if (comp_pid == 0) {
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

    pid_t client_pid = fork();
    TEST_ASSERT_TRUE(client_pid >= 0);

    if (client_pid == 0) {
        struct wl_display *display = wl_display_connect(socket_name);
        if (!display)
            _exit(1);

        struct test_lock_state state = {0};
        struct wl_registry *registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, &state);

        wl_display_roundtrip(display);
        if (!state.lock_manager)
            _exit(2);

        state.session_lock = ext_session_lock_manager_v1_lock(state.lock_manager);
        if (!state.session_lock)
            _exit(3);

        wl_display_roundtrip(display);

        /* Abruptly exit client without unlocking to simulate crash */
        _exit(0);
    }

    int client_status = 0;
    waitpid(client_pid, &client_status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(client_status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(client_status));

    /* Wait for compositor to terminate itself via fail-secure */
    int comp_status = 0;
    waitpid(comp_pid, &comp_status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(comp_status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(comp_status));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_session_lock_lifecycle);
    RUN_TEST(test_session_lock_crash_fail_secure);
    return UNITY_END();
}
