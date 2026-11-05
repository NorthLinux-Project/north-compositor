#define _GNU_SOURCE
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "unity.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"

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

static pid_t server_pid = -1;

struct screencopy_registry_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_output *output;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct ext_image_copy_capture_manager_v1 *image_copy_capture_manager;
    struct ext_output_image_capture_source_manager_v1 *output_image_capture_source_manager;
    struct ext_session_lock_manager_v1 *session_lock_manager;
};

struct screencopy_frame_state {
    bool buffer_received;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t stride;

    bool flags_received;
    uint32_t flags;

    bool ready_received;
    uint32_t tv_sec_hi;
    uint32_t tv_sec_lo;
    uint32_t tv_nsec;

    bool failed_received;
    bool damage_received;
    bool buffer_done_received;
};

struct ext_capture_session_state {
    bool buffer_size_received;
    uint32_t width;
    uint32_t height;

    bool shm_format_received;
    bool done_received;
    bool stopped_received;
};

struct lock_state {
    bool locked_received;
    bool finished_received;
};

static void frame_handle_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format,
                                uint32_t width, uint32_t height, uint32_t stride)
{
    (void)frame;
    struct screencopy_frame_state *state = data;
    state->buffer_received = true;
    state->format = format;
    state->width = width;
    state->height = height;
    state->stride = stride;
}

static void frame_handle_flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags)
{
    (void)frame;
    struct screencopy_frame_state *state = data;
    state->flags_received = true;
    state->flags = flags;
}

static void frame_handle_ready(void *data, struct zwlr_screencopy_frame_v1 *frame,
                               uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
    (void)frame;
    struct screencopy_frame_state *state = data;
    state->ready_received = true;
    state->tv_sec_hi = tv_sec_hi;
    state->tv_sec_lo = tv_sec_lo;
    state->tv_nsec = tv_nsec;
}

static void frame_handle_failed(void *data, struct zwlr_screencopy_frame_v1 *frame)
{
    (void)frame;
    struct screencopy_frame_state *state = data;
    state->failed_received = true;
}

static void frame_handle_damage(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t x,
                                uint32_t y, uint32_t width, uint32_t height)
{
    (void)frame;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    struct screencopy_frame_state *state = data;
    state->damage_received = true;
}

static void frame_handle_linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *frame,
                                      uint32_t format, uint32_t width, uint32_t height)
{
    (void)data;
    (void)frame;
    (void)format;
    (void)width;
    (void)height;
}

static void frame_handle_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *frame)
{
    (void)frame;
    struct screencopy_frame_state *state = data;
    state->buffer_done_received = true;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
    .damage = frame_handle_damage,
    .linux_dmabuf = frame_handle_linux_dmabuf,
    .buffer_done = frame_handle_buffer_done,
};

static void ext_session_handle_buffer_size(void *data,
                                           struct ext_image_copy_capture_session_v1 *session,
                                           uint32_t width, uint32_t height)
{
    (void)session;
    struct ext_capture_session_state *state = data;
    state->buffer_size_received = true;
    state->width = width;
    state->height = height;
}

static void ext_session_handle_shm_format(void *data,
                                          struct ext_image_copy_capture_session_v1 *session,
                                          uint32_t format)
{
    (void)session;
    (void)format;
    struct ext_capture_session_state *state = data;
    state->shm_format_received = true;
}

static void ext_session_handle_dmabuf_device(void *data,
                                             struct ext_image_copy_capture_session_v1 *session,
                                             struct wl_array *device)
{
    (void)data;
    (void)session;
    (void)device;
}

static void ext_session_handle_dmabuf_format(void *data,
                                             struct ext_image_copy_capture_session_v1 *session,
                                             uint32_t format, struct wl_array *modifiers)
{
    (void)data;
    (void)session;
    (void)format;
    (void)modifiers;
}

static void ext_session_handle_done(void *data, struct ext_image_copy_capture_session_v1 *session)
{
    (void)session;
    struct ext_capture_session_state *state = data;
    state->done_received = true;
}

static void ext_session_handle_stopped(void *data,
                                       struct ext_image_copy_capture_session_v1 *session)
{
    (void)session;
    struct ext_capture_session_state *state = data;
    state->stopped_received = true;
}

static const struct ext_image_copy_capture_session_v1_listener ext_session_listener = {
    .buffer_size = ext_session_handle_buffer_size,
    .shm_format = ext_session_handle_shm_format,
    .dmabuf_device = ext_session_handle_dmabuf_device,
    .dmabuf_format = ext_session_handle_dmabuf_format,
    .done = ext_session_handle_done,
    .stopped = ext_session_handle_stopped,
};

static void lock_handle_locked(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    struct lock_state *state = data;
    state->locked_received = true;
}

static void lock_handle_finished(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    struct lock_state *state = data;
    state->finished_received = true;
}

struct lock_surface_state {
    bool configured;
    uint32_t serial;
    uint32_t width;
    uint32_t height;
};

static void lock_surface_handle_configure(void *data, struct ext_session_lock_surface_v1 *surface,
                                          uint32_t serial, uint32_t width, uint32_t height)
{
    (void)surface;
    struct lock_surface_state *state = data;
    state->configured = true;
    state->serial = serial;
    state->width = width;
    state->height = height;
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_handle_configure,
};

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = lock_handle_locked,
    .finished = lock_handle_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    (void)version;
    struct screencopy_registry_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "wl_output") == 0 && !state->output) {
        state->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    } else if (strcmp(interface, "zwlr_screencopy_manager_v1") == 0) {
        state->screencopy_manager =
            wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 3);
    } else if (strcmp(interface, "ext_image_copy_capture_manager_v1") == 0) {
        state->image_copy_capture_manager =
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
    } else if (strcmp(interface, "ext_output_image_capture_source_manager_v1") == 0) {
        state->output_image_capture_source_manager = wl_registry_bind(
            registry, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    } else if (strcmp(interface, "ext_session_lock_manager_v1") == 0) {
        state->session_lock_manager =
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

static void cleanup_registry_state(struct screencopy_registry_state *state)
{
    if (state->screencopy_manager) {
        zwlr_screencopy_manager_v1_destroy(state->screencopy_manager);
        state->screencopy_manager = NULL;
    }
    if (state->image_copy_capture_manager) {
        ext_image_copy_capture_manager_v1_destroy(state->image_copy_capture_manager);
        state->image_copy_capture_manager = NULL;
    }
    if (state->output_image_capture_source_manager) {
        ext_output_image_capture_source_manager_v1_destroy(
            state->output_image_capture_source_manager);
        state->output_image_capture_source_manager = NULL;
    }
    if (state->session_lock_manager) {
        ext_session_lock_manager_v1_destroy(state->session_lock_manager);
        state->session_lock_manager = NULL;
    }
    if (state->output) {
        wl_output_destroy(state->output);
        state->output = NULL;
    }
    if (state->shm) {
        wl_shm_destroy(state->shm);
        state->shm = NULL;
    }
    if (state->compositor) {
        wl_compositor_destroy(state->compositor);
        state->compositor = NULL;
    }
}

static struct wl_buffer *create_shm_buffer(struct wl_shm *shm, int width, int height,
                                           uint32_t stride, uint32_t format, void **data_out)
{
    size_t size = (size_t)stride * (size_t)height;
    int fd = memfd_create("test-screencopy-shm", MFD_CLOEXEC);
    if (fd < 0)
        return NULL;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return NULL;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    if (data_out) {
        *data_out = data;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer *buf =
        wl_shm_pool_create_buffer(pool, 0, width, height, (int32_t)stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static pid_t spawn_compositor(char *socket_name, size_t socket_name_size)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0)
        return -1;

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
        execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-R", ready_str, (char *)NULL);
        _exit(1);
    }

    close(pipe_fds[1]);
    ssize_t read_bytes = read(pipe_fds[0], socket_name, socket_name_size - 1);
    close(pipe_fds[0]);
    if (read_bytes <= 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    char *nl = strchr(socket_name, '\n');
    if (nl)
        *nl = '\0';
    server_pid = pid;
    return pid;
}

static void stop_compositor(pid_t pid)
{
    if (pid > 0) {
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
        server_pid = -1;
        TEST_ASSERT_TRUE(WIFEXITED(status));
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
    }
}

void setUp(void)
{
    server_pid = -1;
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
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }
}

void test_screencopy_globals_advertised(void)
{
    char socket_name[128] = {0};
    pid_t pid = spawn_compositor(socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct screencopy_registry_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    TEST_ASSERT_NOT_NULL(state.compositor);
    TEST_ASSERT_NOT_NULL(state.shm);
    TEST_ASSERT_NOT_NULL(state.output);
    TEST_ASSERT_NOT_NULL(state.screencopy_manager);
    TEST_ASSERT_NOT_NULL(state.image_copy_capture_manager);
    TEST_ASSERT_NOT_NULL(state.output_image_capture_source_manager);

    cleanup_registry_state(&state);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    stop_compositor(pid);
}

void test_screencopy_capture_frame(void)
{
    char socket_name[128] = {0};
    pid_t pid = spawn_compositor(socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct screencopy_registry_state reg_state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &reg_state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(reg_state.screencopy_manager);
    TEST_ASSERT_NOT_NULL(reg_state.output);
    TEST_ASSERT_NOT_NULL(reg_state.shm);

    struct screencopy_frame_state frame_state = {0};
    struct zwlr_screencopy_frame_v1 *frame = zwlr_screencopy_manager_v1_capture_output(
        reg_state.screencopy_manager, 0, reg_state.output);
    TEST_ASSERT_NOT_NULL(frame);
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, &frame_state);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(frame_state.buffer_received);
    TEST_ASSERT_TRUE(frame_state.width > 0);
    TEST_ASSERT_TRUE(frame_state.height > 0);
    TEST_ASSERT_TRUE(frame_state.stride >= frame_state.width * 4);

    void *data = NULL;
    struct wl_buffer *buffer =
        create_shm_buffer(reg_state.shm, (int)frame_state.width, (int)frame_state.height,
                          frame_state.stride, frame_state.format, &data);
    TEST_ASSERT_NOT_NULL(buffer);
    TEST_ASSERT_NOT_NULL(data);

    zwlr_screencopy_frame_v1_copy(frame, buffer);

    for (int i = 0; i < 30 && !frame_state.ready_received && !frame_state.failed_received; i++) {
        usleep(10000);
        wl_display_roundtrip(display);
    }

    TEST_ASSERT_TRUE(frame_state.ready_received);
    TEST_ASSERT_FALSE(frame_state.failed_received);

    zwlr_screencopy_frame_v1_destroy(frame);
    wl_buffer_destroy(buffer);
    munmap(data, (size_t)frame_state.stride * (size_t)frame_state.height);

    cleanup_registry_state(&reg_state);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    stop_compositor(pid);
}

void test_screencopy_lock_privacy(void)
{
    char socket_name[128] = {0};
    pid_t pid = spawn_compositor(socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct screencopy_registry_state reg_state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &reg_state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(reg_state.screencopy_manager);
    TEST_ASSERT_NOT_NULL(reg_state.session_lock_manager);
    TEST_ASSERT_NOT_NULL(reg_state.output);
    TEST_ASSERT_NOT_NULL(reg_state.shm);

    struct lock_state l_state = {0};
    struct ext_session_lock_v1 *lock =
        ext_session_lock_manager_v1_lock(reg_state.session_lock_manager);
    TEST_ASSERT_NOT_NULL(lock);
    ext_session_lock_v1_add_listener(lock, &lock_listener, &l_state);

    struct wl_surface *surface = wl_compositor_create_surface(reg_state.compositor);
    TEST_ASSERT_NOT_NULL(surface);

    struct lock_surface_state ls_state = {0};
    struct ext_session_lock_surface_v1 *lock_surface =
        ext_session_lock_v1_get_lock_surface(lock, surface, reg_state.output);
    TEST_ASSERT_NOT_NULL(lock_surface);
    ext_session_lock_surface_v1_add_listener(lock_surface, &lock_surface_listener, &ls_state);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(ls_state.configured);

    ext_session_lock_surface_v1_ack_configure(lock_surface, ls_state.serial);
    struct wl_buffer *lock_buf =
        create_shm_buffer(reg_state.shm, (int)ls_state.width, (int)ls_state.height,
                          ls_state.width * 4, WL_SHM_FORMAT_ARGB8888, NULL);
    TEST_ASSERT_NOT_NULL(lock_buf);
    wl_surface_attach(surface, lock_buf, 0, 0);
    wl_surface_commit(surface);

    wl_display_roundtrip(display);
    TEST_ASSERT_TRUE(l_state.locked_received);

    struct screencopy_frame_state frame_state = {0};
    struct zwlr_screencopy_frame_v1 *frame = zwlr_screencopy_manager_v1_capture_output(
        reg_state.screencopy_manager, 0, reg_state.output);
    TEST_ASSERT_NOT_NULL(frame);
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, &frame_state);

    wl_display_roundtrip(display);

    if (frame_state.buffer_received) {
        void *data = NULL;
        struct wl_buffer *buffer =
            create_shm_buffer(reg_state.shm, (int)frame_state.width, (int)frame_state.height,
                              frame_state.stride, frame_state.format, &data);
        TEST_ASSERT_NOT_NULL(buffer);
        zwlr_screencopy_frame_v1_copy(frame, buffer);

        for (int i = 0; i < 20 && !frame_state.failed_received && !frame_state.ready_received;
             i++) {
            usleep(10000);
            wl_display_roundtrip(display);
        }

        TEST_ASSERT_TRUE(frame_state.failed_received);
        TEST_ASSERT_FALSE(frame_state.ready_received);

        wl_buffer_destroy(buffer);
        if (data) {
            munmap(data, (size_t)frame_state.stride * (size_t)frame_state.height);
        }
    } else {
        TEST_ASSERT_TRUE(frame_state.failed_received);
        TEST_ASSERT_FALSE(frame_state.ready_received);
    }

    zwlr_screencopy_frame_v1_destroy(frame);
    ext_session_lock_surface_v1_destroy(lock_surface);
    ext_session_lock_v1_unlock_and_destroy(lock);
    wl_surface_destroy(surface);
    wl_buffer_destroy(lock_buf);
    wl_display_roundtrip(display);

    cleanup_registry_state(&reg_state);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    stop_compositor(pid);
}

void test_ext_image_copy_capture_session(void)
{
    char socket_name[128] = {0};
    pid_t pid = spawn_compositor(socket_name, sizeof(socket_name));
    TEST_ASSERT_TRUE(pid > 0);

    struct wl_display *display = wl_display_connect(socket_name);
    TEST_ASSERT_NOT_NULL(display);

    struct screencopy_registry_state reg_state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &reg_state);

    wl_display_roundtrip(display);
    TEST_ASSERT_NOT_NULL(reg_state.image_copy_capture_manager);
    TEST_ASSERT_NOT_NULL(reg_state.output_image_capture_source_manager);
    TEST_ASSERT_NOT_NULL(reg_state.output);

    struct ext_image_capture_source_v1 *source =
        ext_output_image_capture_source_manager_v1_create_source(
            reg_state.output_image_capture_source_manager, reg_state.output);
    TEST_ASSERT_NOT_NULL(source);

    struct ext_capture_session_state session_state = {0};
    struct ext_image_copy_capture_session_v1 *session =
        ext_image_copy_capture_manager_v1_create_session(reg_state.image_copy_capture_manager,
                                                         source, 0);
    TEST_ASSERT_NOT_NULL(session);
    ext_image_copy_capture_session_v1_add_listener(session, &ext_session_listener, &session_state);

    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(session_state.buffer_size_received);
    TEST_ASSERT_TRUE(session_state.width > 0);
    TEST_ASSERT_TRUE(session_state.height > 0);
    TEST_ASSERT_TRUE(session_state.shm_format_received);
    TEST_ASSERT_TRUE(session_state.done_received);

    ext_image_copy_capture_session_v1_destroy(session);
    ext_image_capture_source_v1_destroy(source);
    wl_display_roundtrip(display);

    cleanup_registry_state(&reg_state);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    stop_compositor(pid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_screencopy_globals_advertised);
    RUN_TEST(test_screencopy_capture_frame);
    RUN_TEST(test_screencopy_lock_privacy);
    RUN_TEST(test_ext_image_copy_capture_session);
    return UNITY_END();
}
