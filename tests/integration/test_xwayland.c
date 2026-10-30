#define _GNU_SOURCE
#include "unity.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

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

static pid_t s_comp_pid = -1;

void tearDown(void)
{
    if (s_comp_pid > 0) {
        kill(s_comp_pid, SIGTERM);
        waitpid(s_comp_pid, NULL, 0);
        s_comp_pid = -1;
    }
}

void test_xwayland_window_lifecycle(void)
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

    s_comp_pid = comp_pid;
    close(pipe_fds[1]);

    char buf[256] = {0};
    ssize_t read_bytes = read(pipe_fds[0], buf, sizeof(buf) - 1);
    close(pipe_fds[0]);

    if (read_bytes <= 0) {
        TEST_FAIL_MESSAGE("Compositor failed to initialize ready pipe");
    }

    char *line1 = strtok(buf, "\n");
    (void)line1;
    char *x_display = strtok(NULL, "\n");
    if (!x_display || strlen(x_display) == 0) {
        TEST_IGNORE_MESSAGE("Xwayland socket unavailable in sandboxed environment");
        return;
    }

    pid_t client_pid = fork();
    TEST_ASSERT_TRUE(client_pid >= 0);

    if (client_pid == 0) {
        xcb_connection_t *conn = xcb_connect(x_display, NULL);
        if (xcb_connection_has_error(conn)) {
            _exit(1);
        }

        const xcb_setup_t *setup = xcb_get_setup(conn);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        xcb_screen_t *screen = iter.data;
        if (!screen)
            _exit(2);

        xcb_window_t win = xcb_generate_id(conn);
        uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
        uint32_t values[2] = {screen->white_pixel,
                              XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY};

        xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root, 50, 50, 400, 300, 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask, values);

        const char *title = "Test X11 Window";
        xcb_icccm_set_wm_name(conn, win, XCB_ATOM_STRING, 8, strlen(title), title);

        xcb_map_window(conn, win);
        xcb_flush(conn);

        usleep(50000);

        xcb_destroy_window(conn, win);
        xcb_flush(conn);

        xcb_disconnect(conn);
        _exit(0);
    }

    int client_status = 0;
    waitpid(client_pid, &client_status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(client_status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(client_status));

    kill(comp_pid, SIGTERM);
    int comp_status = 0;
    waitpid(comp_pid, &comp_status, 0);
    s_comp_pid = -1;
    TEST_ASSERT_TRUE(WIFEXITED(comp_status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(comp_status));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_xwayland_window_lifecycle);
    return UNITY_END();
}
