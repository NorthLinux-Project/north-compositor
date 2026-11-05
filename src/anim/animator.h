#ifndef ANIMATOR_H
#define ANIMATOR_H

#include "config/config.h"
#include "util/geom.h"

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct north_server;
struct north_view;
struct north_output;
struct north_workspace;
struct north_animator;

enum north_anim_kind {
    NORTH_ANIM_NONE = 0,
    NORTH_ANIM_VIEW_OPEN,
    NORTH_ANIM_VIEW_CLOSE,
    NORTH_ANIM_WORKSPACE_SLIDE,
    NORTH_ANIM_SWITCHER_FADE,
    NORTH_ANIM_MAXIMIZE,
};

typedef void (*north_anim_finish_cb)(void *data);

struct north_animator {
    struct wl_list link;
    struct north_server *server;
    enum north_anim_kind kind;
    void *target;

    int duration_ms;
    uint64_t start_time_ms;
    bool running;

    north_anim_finish_cb on_finish;
    void *user_data;

    float from_alpha;
    float to_alpha;

    NorthBox from_box;
    NorthBox to_box;

    int direction;
    struct north_workspace *from_ws;
    struct north_workspace *to_ws;
};

struct north_anim_manager {
    struct north_server *server;
    struct wl_list active_animators;
    struct wl_event_source *timer;
};

static inline float anim_ease_out_cubic(float t)
{
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    float one_minus_t = 1.0f - t;
    return 1.0f - (one_minus_t * one_minus_t * one_minus_t);
}

void anim_manager_init(struct north_anim_manager *mgr, struct north_server *server);
void anim_manager_finish(struct north_anim_manager *mgr);

void anim_view_open(struct north_server *server, struct north_view *view);
void anim_view_close(struct north_server *server, struct north_view *view,
                     north_anim_finish_cb on_finish, void *user_data);
void anim_workspace_slide(struct north_server *server, struct north_output *output,
                          struct north_workspace *from, struct north_workspace *to, int direction);
void anim_switcher_fade(struct north_server *server, bool in);
void anim_view_maximize(struct north_server *server, struct north_view *view, NorthBox from,
                        NorthBox to);

void anim_cancel(struct north_animator *anim);
bool anim_is_running(const struct north_animator *anim);

void anim_view_set_opacity(struct north_view *view, float opacity);

#endif
