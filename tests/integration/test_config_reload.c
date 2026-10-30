#define _DEFAULT_SOURCE
#include "unity.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

void test_config_live_reload_cycle(void)
{
    char cfg_path[] = "/tmp/north_test_config_XXXXXX.conf";
    int fd = mkstemps(cfg_path, 5);
    TEST_ASSERT_TRUE(fd >= 0);

    const char *initial_cfg = "[decoration]\n"
                              "titlebar_height = 30\n"
                              "active_title = #112233ff\n";
    write(fd, initial_cfg, strlen(initial_cfg));
    close(fd);

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

        execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-c", cfg_path, "-R", ready_str,
              (char *)NULL);
        _exit(1);
    }

    close(pipe_fds[1]);

    char socket_name[128] = {0};
    ssize_t read_bytes = read(pipe_fds[0], socket_name, sizeof(socket_name) - 1);
    close(pipe_fds[0]);

    TEST_ASSERT_TRUE(read_bytes > 0);

    FILE *f = fopen(cfg_path, "w");
    TEST_ASSERT_NOT_NULL(f);
    const char *updated_cfg = "[decoration]\n"
                              "titlebar_height = 36\n"
                              "active_title = #aabbccff\n";
    fputs(updated_cfg, f);
    fclose(f);

    usleep(150000);

    f = fopen(cfg_path, "w");
    TEST_ASSERT_NOT_NULL(f);
    const char *broken_cfg = "[unclosed_section_header\nkey = val\n";
    fputs(broken_cfg, f);
    fclose(f);

    usleep(150000);

    int kill_res = kill(pid, 0);
    TEST_ASSERT_EQUAL_INT(0, kill_res);

    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    unlink(cfg_path);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config_live_reload_cycle);
    return UNITY_END();
}
