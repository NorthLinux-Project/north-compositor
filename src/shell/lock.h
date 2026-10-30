#ifndef LOCK_H
#define LOCK_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>

struct north_server;
struct north_output;

struct north_session_lock_surface {
    struct wl_list link;
    struct north_session_lock *lock;
    struct north_output *output;
    struct wlr_session_lock_surface_v1 *wlr_surface;
    struct wlr_scene_tree *scene_tree;
    bool committed;

    struct wl_listener commit;
    struct wl_listener destroy;
};

struct north_session_lock {
    struct north_server *server;
    struct wlr_session_lock_v1 *wlr_lock;
    struct wl_list surfaces;
    bool locked_sent;
    bool unlocked;

    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
};

void session_lock_init(struct north_server *server);
void session_lock_finish(struct north_server *server);
void session_lock_handle_new_lock(struct wl_listener *listener, void *data);

#endif
