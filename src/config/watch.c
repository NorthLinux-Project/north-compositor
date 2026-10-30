#include "config/watch.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/view.h"
#include "util/log.h"

#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

static int handle_inotify_event(int fd, uint32_t mask, void *data)
{
    (void)mask;
    struct north_config_watch *watch = data;
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(fd, buffer, sizeof(buffer));
    if (len <= 0) {
        return 0;
    }

    LOG_INFO("Config change detected, reloading '%s'", watch->config_path);
    NorthConfigPtrResult res = config_load(watch->config_path);
    if (!res.ok) {
        LOG_ERROR("Config reload failed: %s (keeping active configuration)",
                  res.err.frames[0].context);
        return 0;
    }

    NorthConfig *new_cfg = res.value;
    NorthConfig *old_cfg = watch->server->config;
    watch->server->config = new_cfg;

    struct north_view *view;
    wl_list_for_each(view, &watch->server->views, link)
    {
        float scale = 1.0f;
        struct north_output *out = output_at(watch->server, view->geometry.x, view->geometry.y);
        if (out && out->wlr_output) {
            scale = out->wlr_output->scale;
        }
        decoration_update(&view->decor, view->geometry.width, view->geometry.height, scale);
    }

    config_destroy(old_cfg);
    LOG_INFO("Config reloaded successfully");
    return 0;
}

bool config_watch_init(struct north_config_watch *watch, struct north_server *server,
                       const char *config_path)
{
    memset(watch, 0, sizeof(*watch));
    watch->server = server;
    watch->inotify_fd = -1;
    watch->watch_desc = -1;

    if (!config_path) {
        return false;
    }

    watch->config_path = strdup(config_path);
    char *path_copy = strdup(config_path);
    char *dir = dirname(path_copy);
    watch->dir_path = strdup(dir);
    free(path_copy);

    watch->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (watch->inotify_fd < 0) {
        LOG_WARN("Failed initializing inotify for config reload");
        return false;
    }

    watch->watch_desc = inotify_add_watch(watch->inotify_fd, watch->dir_path,
                                          IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (watch->watch_desc < 0) {
        LOG_WARN("Failed watching directory '%s' for live reload", watch->dir_path);
        close(watch->inotify_fd);
        watch->inotify_fd = -1;
        return false;
    }

    watch->event_source = wl_event_loop_add_fd(server->loop, watch->inotify_fd, WL_EVENT_READABLE,
                                               handle_inotify_event, watch);
    if (!watch->event_source) {
        close(watch->inotify_fd);
        watch->inotify_fd = -1;
        return false;
    }

    LOG_INFO("Watching '%s' for config live reload", watch->config_path);
    return true;
}

void config_watch_finish(struct north_config_watch *watch)
{
    if (watch->event_source) {
        wl_event_source_remove(watch->event_source);
        watch->event_source = NULL;
    }
    if (watch->inotify_fd >= 0) {
        if (watch->watch_desc >= 0) {
            inotify_rm_watch(watch->inotify_fd, watch->watch_desc);
        }
        close(watch->inotify_fd);
        watch->inotify_fd = -1;
    }
    if (watch->config_path) {
        free(watch->config_path);
        watch->config_path = NULL;
    }
    if (watch->dir_path) {
        free(watch->dir_path);
        watch->dir_path = NULL;
    }
}
