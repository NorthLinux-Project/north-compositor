#ifndef CONFIG_WATCH_H
#define CONFIG_WATCH_H

#include <stdbool.h>
#include <wayland-server-core.h>

struct north_server;

struct north_config_watch {
    struct north_server *server;
    char *config_path;
    char *dir_path;
    int inotify_fd;
    int watch_desc;
    struct wl_event_source *event_source;
};

bool config_watch_init(struct north_config_watch *watch, struct north_server *server,
                       const char *config_path);
void config_watch_finish(struct north_config_watch *watch);

#endif
