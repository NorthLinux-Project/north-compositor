#include "core/server.h"
#include "config/config.h"
#include "config/watch.h"
#include "util/log.h"
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <wlr/util/log.h>

static void ensure_runtime_dir(void) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || access(runtime_dir, W_OK) != 0) {
        char fallback[256];
        snprintf(fallback, sizeof(fallback), "/tmp/north-runtime-%d", (int)getuid());
        mkdir(fallback, 0700);
        setenv("XDG_RUNTIME_DIR", fallback, 1);
    }
}

static void crash_handler(int sig) {
    fprintf(stderr, "\nFatal signal intercepted: %d\n", sig);
    log_dump_ring_buffer(stderr);
    _exit(sig);
}

static void register_signals(void) {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
}

#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
const char *__lsan_default_suppressions(void) {
    return "leak:libfontconfig\n"
           "leak:libglib\n";
}
#endif

static pid_t g_startup_pid = -1;
static bool g_terminate_on_startup_exit = false;
static struct north_server *g_server = NULL;

static int handle_signal(int sig, void *data) {
    struct north_server *server = data;
    LOG_INFO("Caught signal %d, terminating compositor", sig);
    wl_display_terminate(server->display);
    return 0;
}

static int handle_sigchld(int sig, void *data) {
    (void)sig;
    (void)data;
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (g_terminate_on_startup_exit && pid == g_startup_pid) {
            LOG_INFO("Startup process %d exited with status %d, terminating compositor", pid, status);
            if (g_server && g_server->display) {
                wl_display_terminate(g_server->display);
            }
        }
    }
    return 0;
}

static void wlr_log_callback(enum wlr_log_importance importance, const char *fmt, va_list args) {
    LogLevel level = LOG_LEVEL_INFO;
    switch (importance) {
        case WLR_ERROR: level = LOG_LEVEL_ERROR; break;
        case WLR_INFO: level = LOG_LEVEL_INFO; break;
        case WLR_DEBUG: level = LOG_LEVEL_DEBUG; break;
        default: level = LOG_LEVEL_TRACE; break;
    }
    log_emitv(level, "wlroots", 0, "wlr", fmt, args);
}

int main(int argc, char *argv[]) {
    ensure_runtime_dir();
    log_init();
    register_signals();

    const char *config_path = NULL;
    const char *startup_cmd = NULL;
    int ready_fd = -1;

    int opt;
    while ((opt = getopt(argc, argv, "c:s:S:R:dh")) != -1) {
        switch (opt) {
            case 'c':
                config_path = optarg;
                break;
            case 's':
                startup_cmd = optarg;
                g_terminate_on_startup_exit = false;
                break;
            case 'S':
                startup_cmd = optarg;
                g_terminate_on_startup_exit = true;
                break;
            case 'R':
                ready_fd = atoi(optarg);
                break;
            case 'd':
                log_set_level(LOG_LEVEL_DEBUG);
                break;
            case 'h':
            default:
                fprintf(stderr, "Usage: %s [-c config] [-s startup_cmd] [-S startup_cmd] [-R ready_fd] [-d]\n", argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }

    wlr_log_init(WLR_DEBUG, wlr_log_callback);

    NorthConfigPtrResult cfg_res = config_load(config_path);
    if (!cfg_res.ok) {
        error_print(&cfg_res.err, stderr);
        return 1;
    }
    NorthConfig *config = cfg_res.value;

    struct north_server server;
    BoolResult init_res = server_init(&server, config);
    if (!init_res.ok) {
        error_print(&init_res.err, stderr);
        config_destroy(config);
        return 1;
    }

    g_server = &server;
    struct wl_event_source *sigint_source = wl_event_loop_add_signal(server.loop, SIGINT, handle_signal, &server);
    struct wl_event_source *sigterm_source = wl_event_loop_add_signal(server.loop, SIGTERM, handle_signal, &server);
    struct wl_event_source *sigchld_source = wl_event_loop_add_signal(server.loop, SIGCHLD, handle_sigchld, NULL);

    struct north_config_watch watch;
    if (config_path) {
        config_watch_init(&watch, &server, config_path);
    }

    const char *socket = NULL;
    BoolResult start_res = server_start(&server, &socket);
    if (!start_res.ok) {
        error_print(&start_res.err, stderr);
        if (sigint_source) wl_event_source_remove(sigint_source);
        if (sigterm_source) wl_event_source_remove(sigterm_source);
        if (sigchld_source) wl_event_source_remove(sigchld_source);
        if (config_path) config_watch_finish(&watch);
        server_finish(&server);
        config_destroy(config);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, 1);
    LOG_INFO("Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);

    if (ready_fd >= 0) {
        dprintf(ready_fd, "%s\n", socket);
        close(ready_fd);
    }

    if (startup_cmd) {
        LOG_INFO("Running startup command: %s", startup_cmd);
        g_startup_pid = fork();
        if (g_startup_pid == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", startup_cmd, (char *)NULL);
            _exit(1);
        }
    }

    wl_display_run(server.display);

    if (g_startup_pid > 0) {
        kill(g_startup_pid, SIGTERM);
    }
    if (sigint_source) wl_event_source_remove(sigint_source);
    if (sigterm_source) wl_event_source_remove(sigterm_source);
    if (sigchld_source) wl_event_source_remove(sigchld_source);

    if (config_path) {
        config_watch_finish(&watch);
    }
    server_finish(&server);
    config_destroy(config);
    return 0;
}
