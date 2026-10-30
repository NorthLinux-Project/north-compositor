#include "unity.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

struct registry_state {
    bool has_compositor;
    bool has_subcompositor;
    bool has_xdg_wm_base;
    bool has_seat;
    bool has_output;
    bool has_decoration;
    bool has_layer_shell;
    bool has_foreign_toplevel;
    bool has_idle_notify;
    bool has_idle_inhibit;
    bool has_session_lock;
    bool has_pointer_constraints;
    bool has_relative_pointer;
    bool has_screencopy;
    bool has_image_copy_capture;
    bool has_viewporter;
    bool has_fractional_scale;
    bool has_presentation;
    bool has_tearing_control;
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    (void)registry;
    (void)name;
    (void)version;
    struct registry_state *state = data;

    if (strcmp(interface, "wl_compositor") == 0)
        state->has_compositor = true;
    else if (strcmp(interface, "wl_subcompositor") == 0)
        state->has_subcompositor = true;
    else if (strcmp(interface, "xdg_wm_base") == 0)
        state->has_xdg_wm_base = true;
    else if (strcmp(interface, "wl_seat") == 0)
        state->has_seat = true;
    else if (strcmp(interface, "wl_output") == 0)
        state->has_output = true;
    else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0)
        state->has_decoration = true;
    else if (strcmp(interface, "zwlr_layer_shell_v1") == 0)
        state->has_layer_shell = true;
    else if (strcmp(interface, "zwlr_foreign_toplevel_manager_v1") == 0)
        state->has_foreign_toplevel = true;
    else if (strcmp(interface, "ext_idle_notifier_v1") == 0)
        state->has_idle_notify = true;
    else if (strcmp(interface, "zwp_idle_inhibit_manager_v1") == 0)
        state->has_idle_inhibit = true;
    else if (strcmp(interface, "ext_session_lock_manager_v1") == 0)
        state->has_session_lock = true;
    else if (strcmp(interface, "zwp_pointer_constraints_v1") == 0)
        state->has_pointer_constraints = true;
    else if (strcmp(interface, "zwp_relative_pointer_manager_v1") == 0)
        state->has_relative_pointer = true;
    else if (strcmp(interface, "zwlr_screencopy_manager_v1") == 0)
        state->has_screencopy = true;
    else if (strcmp(interface, "ext_image_copy_capture_manager_v1") == 0)
        state->has_image_copy_capture = true;
    else if (strcmp(interface, "wp_viewporter") == 0)
        state->has_viewporter = true;
    else if (strcmp(interface, "wp_fractional_scale_manager_v1") == 0)
        state->has_fractional_scale = true;
    else if (strcmp(interface, "wp_presentation") == 0)
        state->has_presentation = true;
    else if (strcmp(interface, "wp_tearing_control_manager_v1") == 0)
        state->has_tearing_control = true;
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

void test_headless_compositor_advertises_globals(void)
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

    struct registry_state state = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);

    wl_display_roundtrip(display);

    TEST_ASSERT_TRUE(state.has_compositor);
    TEST_ASSERT_TRUE(state.has_subcompositor);
    TEST_ASSERT_TRUE(state.has_xdg_wm_base);
    TEST_ASSERT_TRUE(state.has_seat);
    TEST_ASSERT_TRUE(state.has_decoration);
    TEST_ASSERT_TRUE(state.has_layer_shell);
    TEST_ASSERT_TRUE(state.has_foreign_toplevel);
    TEST_ASSERT_TRUE(state.has_idle_notify);
    TEST_ASSERT_TRUE(state.has_idle_inhibit);
    TEST_ASSERT_TRUE(state.has_session_lock);
    TEST_ASSERT_TRUE(state.has_pointer_constraints);
    TEST_ASSERT_TRUE(state.has_relative_pointer);
    TEST_ASSERT_TRUE(state.has_screencopy);
    TEST_ASSERT_TRUE(state.has_image_copy_capture);
    TEST_ASSERT_TRUE(state.has_viewporter);
    TEST_ASSERT_TRUE(state.has_fractional_scale);
    TEST_ASSERT_TRUE(state.has_presentation);
    TEST_ASSERT_TRUE(state.has_tearing_control);

    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
}

void test_startup_cmd_termination_flag(void)
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

        execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-R", ready_str, "-S", "sleep 0.05",
              (char *)NULL);
        _exit(1);
    }

    close(pipe_fds[1]);

    char socket_name[128] = {0};
    ssize_t read_bytes = read(pipe_fds[0], socket_name, sizeof(socket_name) - 1);
    close(pipe_fds[0]);

    TEST_ASSERT_TRUE(read_bytes > 0);

    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_headless_compositor_advertises_globals);
    RUN_TEST(test_startup_cmd_termination_flag);
    return UNITY_END();
}
