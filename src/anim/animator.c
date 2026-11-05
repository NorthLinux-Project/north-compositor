#include "anim/animator.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/switcher.h"
#include "shell/view.h"
#include "shell/workspace.h"
#include "util/log.h"

#include <time.h>
#include <wlr/types/wlr_scene.h>

static uint64_t anim_current_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

struct opacity_iter_data {
    float opacity;
};

static void set_buffer_opacity_iter(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
    (void)sx;
    (void)sy;
    struct opacity_iter_data *d = data;
    wlr_scene_buffer_set_opacity(buffer, d->opacity);
}

void anim_view_set_opacity(struct north_view *view, float opacity)
{
    if (!view) {
        return;
    }
    view->alpha = opacity;
    struct opacity_iter_data d = {.opacity = opacity};
    if (view->surface_tree) {
        wlr_scene_node_for_each_buffer(&view->surface_tree->node, set_buffer_opacity_iter, &d);
    }
    if (view->decor.titlebar_node) {
        wlr_scene_buffer_set_opacity(view->decor.titlebar_node, opacity);
    }
    if (view->decor.enabled && view->server && view->server->config) {
        NorthDecorConfig *dc = &view->server->config->decor;
        NorthColor c = view->decor.active ? dc->active_border : dc->inactive_border;
        float rgba[4] = {c.r * opacity, c.g * opacity, c.b * opacity, c.a * opacity};
        if (view->decor.border_top) {
            wlr_scene_rect_set_color(view->decor.border_top, rgba);
        }
        if (view->decor.border_bottom) {
            wlr_scene_rect_set_color(view->decor.border_bottom, rgba);
        }
        if (view->decor.border_left) {
            wlr_scene_rect_set_color(view->decor.border_left, rgba);
        }
        if (view->decor.border_right) {
            wlr_scene_rect_set_color(view->decor.border_right, rgba);
        }
    }
}

static void anim_step(struct north_animator *anim, float progress)
{
    switch (anim->kind) {
    case NORTH_ANIM_VIEW_OPEN: {
        struct north_view *view = anim->target;
        if (!view) {
            break;
        }
        float opacity = anim->from_alpha + (anim->to_alpha - anim->from_alpha) * progress;
        anim_view_set_opacity(view, opacity);
        if (progress >= 1.0f) {
            anim_view_set_opacity(view, 1.0f);
        }
        break;
    }
    case NORTH_ANIM_VIEW_CLOSE: {
        struct north_view *view = anim->target;
        if (!view) {
            break;
        }
        float opacity = anim->from_alpha + (anim->to_alpha - anim->from_alpha) * progress;
        anim_view_set_opacity(view, opacity);
        break;
    }
    case NORTH_ANIM_WORKSPACE_SLIDE: {
        if (!anim->from_ws || !anim->to_ws) {
            break;
        }
        int w = anim->from_box.width;
        int from_x = (int)((float)(-anim->direction * w) * progress);
        int to_x = (int)((float)(anim->direction * w) * (1.0f - progress));
        wlr_scene_node_set_position(&anim->from_ws->tree->node, from_x, 0);
        wlr_scene_node_set_position(&anim->to_ws->tree->node, to_x, 0);
        break;
    }
    case NORTH_ANIM_SWITCHER_FADE: {
        struct north_switcher *switcher = &anim->server->switcher;
        if (switcher->scene_buffer) {
            float opacity = anim->from_alpha + (anim->to_alpha - anim->from_alpha) * progress;
            wlr_scene_buffer_set_opacity(switcher->scene_buffer, opacity);
        }
        break;
    }
    case NORTH_ANIM_MAXIMIZE: {
        struct north_view *view = anim->target;
        if (!view) {
            break;
        }
        int curr_x =
            anim->from_box.x + (int)((float)(anim->to_box.x - anim->from_box.x) * progress);
        int curr_y =
            anim->from_box.y + (int)((float)(anim->to_box.y - anim->from_box.y) * progress);
        wlr_scene_node_set_position(&view->tree->node, curr_x, curr_y);
        break;
    }
    default:
        break;
    }
}

static void anim_complete(struct north_animator *anim)
{
    enum north_anim_kind kind = anim->kind;
    void *target = anim->target;
    struct north_server *server = anim->server;
    struct north_workspace *from_ws = anim->from_ws;
    struct north_workspace *to_ws = anim->to_ws;
    NorthBox to_box = anim->to_box;
    float to_alpha = anim->to_alpha;

    north_anim_finish_cb cb = anim->on_finish;
    void *ud = anim->user_data;

    anim_cancel(anim);

    switch (kind) {
    case NORTH_ANIM_VIEW_OPEN: {
        struct north_view *view = target;
        if (view) {
            anim_view_set_opacity(view, 1.0f);
        }
        break;
    }
    case NORTH_ANIM_VIEW_CLOSE:
        break;
    case NORTH_ANIM_WORKSPACE_SLIDE: {
        if (from_ws && from_ws->tree) {
            wlr_scene_node_set_position(&from_ws->tree->node, 0, 0);
            wlr_scene_node_set_enabled(&from_ws->tree->node, false);
        }
        if (to_ws && to_ws->tree) {
            wlr_scene_node_set_position(&to_ws->tree->node, 0, 0);
            wlr_scene_node_set_enabled(&to_ws->tree->node, true);
        }
        break;
    }
    case NORTH_ANIM_SWITCHER_FADE: {
        struct north_switcher *switcher = &server->switcher;
        if (to_alpha <= 0.0f) {
            if (switcher->tree) {
                wlr_scene_node_set_enabled(&switcher->tree->node, false);
            }
            switcher->active = false;
        }
        break;
    }
    case NORTH_ANIM_MAXIMIZE: {
        struct north_view *view = target;
        if (view && view->tree) {
            wlr_scene_node_set_position(&view->tree->node, to_box.x, to_box.y);
        }
        break;
    }
    default:
        break;
    }

    if (cb) {
        cb(ud);
    }
}

static int anim_manager_handle_tick(void *data)
{
    struct north_anim_manager *mgr = data;
    if (wl_list_empty(&mgr->active_animators)) {
        return 0;
    }

    uint64_t now = anim_current_time_ms();
    struct north_animator *anim, *tmp;
    wl_list_for_each_safe(anim, tmp, &mgr->active_animators, link)
    {
        float t = 1.0f;
        if (anim->duration_ms > 0) {
            uint64_t elapsed = (now >= anim->start_time_ms) ? (now - anim->start_time_ms) : 0;
            t = (float)elapsed / (float)anim->duration_ms;
            if (t > 1.0f) {
                t = 1.0f;
            }
        }
        float progress = anim_ease_out_cubic(t);
        anim_step(anim, progress);

        if (t >= 1.0f) {
            anim_complete(anim);
        }
    }

    if (!wl_list_empty(&mgr->active_animators) && mgr->timer) {
        wl_event_source_timer_update(mgr->timer, 16);
    }
    return 0;
}

void anim_manager_init(struct north_anim_manager *mgr, struct north_server *server)
{
    if (!mgr || !server) {
        return;
    }
    mgr->server = server;
    wl_list_init(&mgr->active_animators);
    mgr->timer = wl_event_loop_add_timer(server->loop, anim_manager_handle_tick, mgr);
}

void anim_manager_finish(struct north_anim_manager *mgr)
{
    if (!mgr) {
        return;
    }
    struct north_animator *anim, *tmp;
    wl_list_for_each_safe(anim, tmp, &mgr->active_animators, link) { anim_cancel(anim); }
    if (mgr->timer) {
        wl_event_source_remove(mgr->timer);
        mgr->timer = NULL;
    }
}

static void anim_start(struct north_anim_manager *mgr, struct north_animator *anim)
{
    if (anim->running) {
        anim_cancel(anim);
    }
    anim->running = true;
    anim->start_time_ms = anim_current_time_ms();
    wl_list_insert(&mgr->active_animators, &anim->link);
    if (mgr->timer) {
        wl_event_source_timer_update(mgr->timer, 16);
    }
}

void anim_cancel(struct north_animator *anim)
{
    if (!anim || !anim->running) {
        return;
    }
    wl_list_remove(&anim->link);
    wl_list_init(&anim->link);
    anim->running = false;
    anim->on_finish = NULL;
    anim->user_data = NULL;
}

bool anim_is_running(const struct north_animator *anim) { return anim && anim->running; }

void anim_view_open(struct north_server *server, struct north_view *view)
{
    if (!server || !view) {
        return;
    }
    if (!server->config->anim.enabled || server->config->anim.duration_ms <= 0) {
        anim_view_set_opacity(view, 1.0f);
        return;
    }

    struct north_animator *anim = &view->open_anim;
    anim_cancel(anim);

    anim->server = server;
    anim->kind = NORTH_ANIM_VIEW_OPEN;
    anim->target = view;
    anim->duration_ms = server->config->anim.duration_ms;
    anim->from_alpha = 0.0f;
    anim->to_alpha = 1.0f;
    anim->on_finish = NULL;
    anim->user_data = NULL;
    wl_list_init(&anim->link);

    anim_view_set_opacity(view, 0.0f);
    anim_start(&server->anim_mgr, anim);
}

void anim_view_close(struct north_server *server, struct north_view *view,
                     north_anim_finish_cb on_finish, void *user_data)
{
    if (!server || !view) {
        if (on_finish) {
            on_finish(user_data);
        }
        return;
    }
    if (!server->config->anim.enabled || server->config->anim.duration_ms <= 0) {
        if (on_finish) {
            on_finish(user_data);
        }
        return;
    }

    struct north_animator *anim = &view->close_anim;
    if (anim->running) {
        return;
    }

    anim_cancel(&view->open_anim);

    anim->server = server;
    anim->kind = NORTH_ANIM_VIEW_CLOSE;
    anim->target = view;
    anim->duration_ms = server->config->anim.duration_ms;
    anim->from_alpha = 1.0f;
    anim->to_alpha = 0.0f;
    anim->on_finish = on_finish;
    anim->user_data = user_data;
    wl_list_init(&anim->link);

    anim_start(&server->anim_mgr, anim);
}

void anim_workspace_slide(struct north_server *server, struct north_output *output,
                          struct north_workspace *from, struct north_workspace *to, int direction)
{
    if (!server || !output || !from || !to) {
        return;
    }

    int width = output->usable_area.width;
    if (width <= 0 && output->wlr_output) {
        width = output->wlr_output->width;
    }
    if (width <= 0) {
        width = 1920;
    }

    if (!server->config->anim.enabled || server->config->anim.duration_ms <= 0) {
        wlr_scene_node_set_position(&from->tree->node, 0, 0);
        wlr_scene_node_set_enabled(&from->tree->node, false);
        wlr_scene_node_set_position(&to->tree->node, 0, 0);
        wlr_scene_node_set_enabled(&to->tree->node, true);
        return;
    }

    struct north_animator *anim = &output->workspace_anim;
    if (anim->running) {
        anim_complete(anim);
    }

    anim->server = server;
    anim->kind = NORTH_ANIM_WORKSPACE_SLIDE;
    anim->target = output;
    anim->duration_ms = server->config->anim.duration_ms;
    anim->direction = (direction >= 0) ? 1 : -1;
    anim->from_ws = from;
    anim->to_ws = to;
    anim->from_box = (NorthBox){.width = width, .height = 0};
    anim->on_finish = NULL;
    anim->user_data = NULL;
    wl_list_init(&anim->link);

    wlr_scene_node_set_enabled(&from->tree->node, true);
    wlr_scene_node_set_enabled(&to->tree->node, true);
    wlr_scene_node_set_position(&from->tree->node, 0, 0);
    wlr_scene_node_set_position(&to->tree->node, anim->direction * width, 0);

    anim_start(&server->anim_mgr, anim);
}

void anim_switcher_fade(struct north_server *server, bool in)
{
    if (!server) {
        return;
    }
    struct north_switcher *switcher = &server->switcher;
    if (!switcher->scene_buffer) {
        return;
    }

    if (!server->config->anim.enabled || server->config->anim.duration_ms <= 0) {
        if (in) {
            wlr_scene_buffer_set_opacity(switcher->scene_buffer, 1.0f);
        } else {
            if (switcher->tree) {
                wlr_scene_node_set_enabled(&switcher->tree->node, false);
            }
            switcher->active = false;
        }
        return;
    }

    struct north_animator *anim = &switcher->fade_anim;
    anim_cancel(anim);

    anim->server = server;
    anim->kind = NORTH_ANIM_SWITCHER_FADE;
    anim->target = switcher;
    anim->duration_ms = server->config->anim.duration_ms;
    anim->from_alpha = in ? 0.0f : 1.0f;
    anim->to_alpha = in ? 1.0f : 0.0f;
    anim->on_finish = NULL;
    anim->user_data = NULL;
    wl_list_init(&anim->link);

    wlr_scene_buffer_set_opacity(switcher->scene_buffer, anim->from_alpha);
    anim_start(&server->anim_mgr, anim);
}

void anim_view_maximize(struct north_server *server, struct north_view *view, NorthBox from,
                        NorthBox to)
{
    if (!server || !view) {
        return;
    }
    if (!server->config->anim.enabled || server->config->anim.duration_ms <= 0 ||
        view->open_anim.running) {
        wlr_scene_node_set_position(&view->tree->node, to.x, to.y);
        return;
    }

    struct north_animator *anim = &view->maximize_anim;
    anim_cancel(anim);

    anim->server = server;
    anim->kind = NORTH_ANIM_MAXIMIZE;
    anim->target = view;
    anim->duration_ms = server->config->anim.duration_ms;
    anim->from_box = from;
    anim->to_box = to;
    anim->on_finish = NULL;
    anim->user_data = NULL;
    wl_list_init(&anim->link);

    wlr_scene_node_set_position(&view->tree->node, from.x, from.y);
    anim_start(&server->anim_mgr, anim);
}
