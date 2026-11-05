#include "shell/view.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/workspace.h"
#include "util/log.h"
#include "util/match.h"

#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

struct north_view *view_init_base(struct north_server *server, enum north_view_type type,
                                  const struct north_view_impl *impl)
{
    struct north_view *view = calloc(1, sizeof(*view));
    if (!view) {
        return NULL;
    }
    view->server = server;
    view->type = type;
    view->impl = impl;
    wl_list_init(&view->link);

    view->tree = wlr_scene_tree_create(server->layers.views);
    if (!view->tree) {
        free(view);
        return NULL;
    }
    view->tree->node.data = view;
    view->rule_ssd = -1;
    view->alpha = 1.0f;

    decoration_init(&view->decor, view);
    return view;
}

void view_handle_unmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, unmap);
    const char *title = (view->type == NORTH_VIEW_XDG && view->xdg_toplevel)
                            ? view->xdg_toplevel->title
                            : (view->xwayland_surface ? view->xwayland_surface->title : NULL);
    LOG_INFO("View unmapped: title='%s'", title ? title : "(null)");

    anim_cancel(&view->open_anim);
    anim_cancel(&view->close_anim);
    anim_cancel(&view->maximize_anim);

    switcher_notify_view_unmap(view->server, view);

    wl_list_remove(&view->link);
    wl_list_init(&view->link);

    if (view->toplevel_handle) {
        struct north_server *server = view->server;
        struct north_output *out;
        wl_list_for_each(out, &server->outputs, link)
        {
            wlr_foreign_toplevel_handle_v1_output_leave(view->toplevel_handle, out->wlr_output);
        }
        wlr_foreign_toplevel_handle_v1_set_activated(view->toplevel_handle, false);
    }

    if (view->tree && view->server) {
        wlr_scene_node_reparent(&view->tree->node, view->server->layers.views);
    }

    view->workspace = NULL;
    ipc_broadcast_window_event(view->server, view, "unmap");
    view_focus_top_visible(view->server);
}

void view_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, destroy);
    view_destroy(view);
}

void view_handle_request_move(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_move);
    seat_begin_move(&view->server->seat, view);
}

static void view_handle_foreign_request_maximize(struct wl_listener *listener, void *data)
{
    struct north_view *view = wl_container_of(listener, view, foreign_request_maximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    view_set_maximized(view, event->maximized);
}

static void view_handle_foreign_request_minimize(struct wl_listener *listener, void *data)
{
    struct north_view *view = wl_container_of(listener, view, foreign_request_minimize);
    struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
    if (!event->minimized) {
        view_focus(view);
    }
}

static void view_handle_foreign_request_activate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, foreign_request_activate);
    view_focus(view);
}

static void view_handle_foreign_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct north_view *view = wl_container_of(listener, view, foreign_request_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    view_set_fullscreen(view, event->fullscreen);
}

static void view_handle_foreign_request_close(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, foreign_request_close);
    view_close(view);
}

void view_setup_foreign_toplevel(struct north_view *view)
{
    struct north_server *server = view->server;
    if (server->foreign_toplevel_manager) {
        view->toplevel_handle =
            wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_manager);
        if (view->toplevel_handle) {
            view->foreign_request_maximize.notify = view_handle_foreign_request_maximize;
            wl_signal_add(&view->toplevel_handle->events.request_maximize,
                          &view->foreign_request_maximize);

            view->foreign_request_minimize.notify = view_handle_foreign_request_minimize;
            wl_signal_add(&view->toplevel_handle->events.request_minimize,
                          &view->foreign_request_minimize);

            view->foreign_request_activate.notify = view_handle_foreign_request_activate;
            wl_signal_add(&view->toplevel_handle->events.request_activate,
                          &view->foreign_request_activate);

            view->foreign_request_fullscreen.notify = view_handle_foreign_request_fullscreen;
            wl_signal_add(&view->toplevel_handle->events.request_fullscreen,
                          &view->foreign_request_fullscreen);

            view->foreign_request_close.notify = view_handle_foreign_request_close;
            wl_signal_add(&view->toplevel_handle->events.request_close,
                          &view->foreign_request_close);
        }
    }
}

void view_destroy(struct north_view *view)
{
    if (!view)
        return;

    anim_cancel(&view->open_anim);
    anim_cancel(&view->close_anim);
    anim_cancel(&view->maximize_anim);

    if (view->map.link.prev && view->map.link.next) {
        wl_list_remove(&view->map.link);
    }
    if (view->unmap.link.prev && view->unmap.link.next) {
        wl_list_remove(&view->unmap.link);
    }
    if (view->commit.link.prev && view->commit.link.next) {
        wl_list_remove(&view->commit.link);
    }
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->set_title.link);
    wl_list_remove(&view->set_app_id.link);
    if (view->type == NORTH_VIEW_XWAYLAND) {
        wl_list_remove(&view->associate.link);
        wl_list_remove(&view->dissociate.link);
        wl_list_remove(&view->request_configure.link);
        wl_list_remove(&view->request_activate.link);
        wl_list_remove(&view->set_decorations.link);
    }

    if (view->toplevel_handle) {
        wl_list_remove(&view->foreign_request_maximize.link);
        wl_list_remove(&view->foreign_request_minimize.link);
        wl_list_remove(&view->foreign_request_activate.link);
        wl_list_remove(&view->foreign_request_fullscreen.link);
        wl_list_remove(&view->foreign_request_close.link);
        wlr_foreign_toplevel_handle_v1_destroy(view->toplevel_handle);
        view->toplevel_handle = NULL;
    }

    if (view->server) {
        switcher_notify_view_unmap(view->server, view);
    }

    if (view->link.next && view->link.prev) {
        wl_list_remove(&view->link);
    }

    decoration_finish(&view->decor);
    wlr_scene_node_destroy(&view->tree->node);
    free(view);
}

void view_focus(struct north_view *view)
{
    if (!view)
        return;

    struct north_server *server = view->server;
    struct wlr_seat *seat = server->seat.wlr_seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;

    if (prev_surface) {
        struct north_view *prev_view;
        wl_list_for_each(prev_view, &server->views, link)
        {
            if (prev_view->surface == prev_surface) {
                if (prev_view->impl && prev_view->impl->set_activated) {
                    prev_view->impl->set_activated(prev_view, false);
                }
                decoration_set_active(&prev_view->decor, false);
                if (prev_view->toplevel_handle) {
                    wlr_foreign_toplevel_handle_v1_set_activated(prev_view->toplevel_handle, false);
                }
                break;
            }
        }
    }

    wlr_scene_node_raise_to_top(&view->tree->node);

    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);

    if (view->impl && view->impl->set_activated) {
        view->impl->set_activated(view, true);
    }
    decoration_set_active(&view->decor, true);
    if (view->toplevel_handle) {
        wlr_foreign_toplevel_handle_v1_set_activated(view->toplevel_handle, true);
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard && view->surface) {
        wlr_seat_keyboard_notify_enter(seat, view->surface, keyboard->keycodes,
                                       keyboard->num_keycodes, &keyboard->modifiers);
    }
    ipc_broadcast_window_event(server, view, "focus");
}

void view_clear_focus(struct north_server *server)
{
    if (!server) {
        return;
    }
    struct wlr_seat *seat = server->seat.wlr_seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;

    if (prev_surface) {
        struct north_view *prev_view;
        wl_list_for_each(prev_view, &server->views, link)
        {
            if (prev_view->surface == prev_surface) {
                if (prev_view->impl && prev_view->impl->set_activated) {
                    prev_view->impl->set_activated(prev_view, false);
                }
                decoration_set_active(&prev_view->decor, false);
                if (prev_view->toplevel_handle) {
                    wlr_foreign_toplevel_handle_v1_set_activated(prev_view->toplevel_handle, false);
                }
                break;
            }
        }
    }

    wlr_seat_keyboard_clear_focus(seat);
}

void view_focus_top_visible(struct north_server *server)
{
    if (!server) {
        return;
    }
    struct north_view *v;
    wl_list_for_each(v, &server->views, link)
    {
        if (workspace_view_is_visible(v)) {
            view_focus(v);
            return;
        }
    }
    view_clear_focus(server);
}

void view_focus_next(struct north_server *server)
{
    if (!server || wl_list_empty(&server->views)) {
        return;
    }

    struct north_view *focused = NULL;
    struct wlr_surface *focused_surface = server->seat.wlr_seat->keyboard_state.focused_surface;
    if (focused_surface) {
        struct north_view *v;
        wl_list_for_each(v, &server->views, link)
        {
            if (v->surface == focused_surface) {
                focused = v;
                break;
            }
        }
    }

    if (!focused) {
        view_focus_top_visible(server);
        return;
    }

    struct wl_list *curr = focused->link.next;
    while (curr != &focused->link) {
        if (curr == &server->views) {
            curr = curr->next;
            continue;
        }
        struct north_view *v = wl_container_of(curr, v, link);
        if (workspace_view_is_visible(v)) {
            view_focus(v);
            return;
        }
        curr = curr->next;
    }
}

void view_focus_prev(struct north_server *server)
{
    if (!server || wl_list_empty(&server->views)) {
        return;
    }

    struct north_view *focused = NULL;
    struct wlr_surface *focused_surface = server->seat.wlr_seat->keyboard_state.focused_surface;
    if (focused_surface) {
        struct north_view *v;
        wl_list_for_each(v, &server->views, link)
        {
            if (v->surface == focused_surface) {
                focused = v;
                break;
            }
        }
    }

    if (!focused) {
        view_focus_top_visible(server);
        return;
    }

    struct wl_list *curr = focused->link.prev;
    while (curr != &focused->link) {
        if (curr == &server->views) {
            curr = curr->prev;
            continue;
        }
        struct north_view *v = wl_container_of(curr, v, link);
        if (workspace_view_is_visible(v)) {
            view_focus(v);
            return;
        }
        curr = curr->prev;
    }
}

void view_set_position(struct north_view *view, int x, int y)
{
    view->geometry.x = x;
    view->geometry.y = y;
    wlr_scene_node_set_position(&view->tree->node, x, y);
    if (view->type == NORTH_VIEW_XWAYLAND && view->xwayland_surface) {
        wlr_xwayland_surface_configure(view->xwayland_surface, x, y, view->geometry.width,
                                       view->geometry.height);
    }
}

void view_set_size(struct north_view *view, int width, int height)
{
    if (width < 50)
        width = 50;
    if (height < 50)
        height = 50;
    view->geometry.width = width;
    view->geometry.height = height;
    if (view->impl && view->impl->set_size) {
        view->impl->set_size(view, width, height);
    }
}

void view_set_maximized(struct north_view *view, bool maximized)
{
    if (!maximized && view->state != VIEW_MAXIMIZED) {
        return;
    }

    if (maximized) {
        if (view->state == VIEW_NORMAL) {
            view->saved_geometry = view->geometry;
        }
        double cx = view->geometry.x + view->geometry.width / 2.0;
        double cy = view->geometry.y + view->geometry.height / 2.0;
        struct north_output *out = output_at(view->server, cx, cy);
        if (!out && view->workspace && view->workspace->output) {
            out = view->workspace->output;
        }
        if (!out && !wl_list_empty(&view->server->outputs)) {
            out = wl_container_of(view->server->outputs.next, out, link);
        }
        NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};

        int tb_h = view->decor.enabled ? view->server->config->decor.titlebar_height : 0;
        NorthBox from = view->geometry;
        NorthBox to = (NorthBox){usable.x, usable.y + tb_h, usable.width, usable.height - tb_h};

        view->state = VIEW_MAXIMIZED;
        view_set_position(view, usable.x, usable.y + tb_h);
        view->geometry.width = usable.width;
        view->geometry.height = usable.height - tb_h;
        if (view->impl && view->impl->set_size) {
            view->impl->set_size(view, usable.width, usable.height - tb_h);
        }
        if (view->impl && view->impl->set_maximized) {
            view->impl->set_maximized(view, true);
        }
        if (view->toplevel_handle) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->toplevel_handle, true);
        }

        float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
        decoration_update(&view->decor, usable.width, usable.height - tb_h, scale);
        anim_view_maximize(view->server, view, from, to);
    } else {
        NorthBox from = view->geometry;
        NorthBox to = view->saved_geometry;

        view->state = VIEW_NORMAL;
        view_set_position(view, view->saved_geometry.x, view->saved_geometry.y);
        view->geometry.width = view->saved_geometry.width;
        view->geometry.height = view->saved_geometry.height;
        if (view->impl && view->impl->set_size) {
            view->impl->set_size(view, view->saved_geometry.width, view->saved_geometry.height);
        }
        if (view->impl && view->impl->set_maximized) {
            view->impl->set_maximized(view, false);
        }
        if (view->toplevel_handle) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->toplevel_handle, false);
        }

        double cx = view->saved_geometry.x + view->saved_geometry.width / 2.0;
        double cy = view->saved_geometry.y + view->saved_geometry.height / 2.0;
        struct north_output *out = output_at(view->server, cx, cy);
        if (!out && !wl_list_empty(&view->server->outputs)) {
            out = wl_container_of(view->server->outputs.next, out, link);
        }
        float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
        decoration_update(&view->decor, view->saved_geometry.width, view->saved_geometry.height,
                          scale);
        anim_view_maximize(view->server, view, from, to);
    }
}

void view_set_fullscreen(struct north_view *view, bool fullscreen)
{
    if (fullscreen == (view->state == VIEW_FULLSCREEN)) {
        return;
    }

    if (fullscreen) {
        if (view->state == VIEW_NORMAL) {
            view->saved_geometry = view->geometry;
        }
        struct north_output *out = output_at(view->server, view->geometry.x, view->geometry.y);
        if (!out && view->workspace && view->workspace->output) {
            out = view->workspace->output;
        }
        if (!out && !wl_list_empty(&view->server->outputs)) {
            out = wl_container_of(view->server->outputs.next, out, link);
        }
        NorthBox box;
        if (out && out->wlr_output) {
            struct wlr_box lbox;
            wlr_output_layout_get_box(view->server->output_layout, out->wlr_output, &lbox);
            box = (NorthBox){lbox.x, lbox.y, lbox.width, lbox.height};
        } else {
            box = (NorthBox){0, 0, 1920, 1080};
        }

        wlr_scene_node_raise_to_top(&view->tree->node);
        decoration_set_enabled(&view->decor, false);

        view_set_position(view, box.x, box.y);
        view_set_size(view, box.width, box.height);
        if (view->impl && view->impl->set_fullscreen) {
            view->impl->set_fullscreen(view, true);
        }
        view->state = VIEW_FULLSCREEN;
        if (view->toplevel_handle) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->toplevel_handle, true);
        }
    } else {
        if (view->rule_ssd != 0) {
            decoration_set_enabled(&view->decor, true);
        }

        view_set_position(view, view->saved_geometry.x, view->saved_geometry.y);
        view_set_size(view, view->saved_geometry.width, view->saved_geometry.height);
        if (view->impl && view->impl->set_fullscreen) {
            view->impl->set_fullscreen(view, false);
        }
        view->state = VIEW_NORMAL;
        if (view->toplevel_handle) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->toplevel_handle, false);
        }
    }
}

void view_set_snapped(struct north_view *view, enum north_snap_zone zone)
{
    if (!view || zone == SNAP_NONE) {
        return;
    }
    if (zone == SNAP_TOP) {
        view_set_maximized(view, true);
        return;
    }

    if (view->state == VIEW_NORMAL) {
        view->saved_geometry = view->geometry;
    }

    double cx = view->geometry.x + view->geometry.width / 2.0;
    double cy = view->geometry.y + view->geometry.height / 2.0;
    struct north_output *out = output_at(view->server, cx, cy);
    if (!out && view->workspace && view->workspace->output) {
        out = view->workspace->output;
    }
    if (!out && !wl_list_empty(&view->server->outputs)) {
        out = wl_container_of(view->server->outputs.next, out, link);
    }
    NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};

    int tb_h = view->decor.enabled ? view->server->config->decor.titlebar_height : 0;
    NorthBox snap_box = placement_calculate_snap_box(usable, zone, tb_h);

    if (view->state == VIEW_FULLSCREEN) {
        if (view->rule_ssd != 0) {
            decoration_set_enabled(&view->decor, true);
        }
        if (view->impl && view->impl->set_fullscreen) {
            view->impl->set_fullscreen(view, false);
        }
        if (view->toplevel_handle) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->toplevel_handle, false);
        }
    } else if (view->state == VIEW_MAXIMIZED) {
        if (view->impl && view->impl->set_maximized) {
            view->impl->set_maximized(view, false);
        }
        if (view->toplevel_handle) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->toplevel_handle, false);
        }
    }

    view->state = VIEW_SNAPPED;
    view->snap_zone = zone;

    view_set_position(view, snap_box.x, snap_box.y);
    view_set_size(view, snap_box.width, snap_box.height);

    float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
    decoration_update(&view->decor, snap_box.width, snap_box.height, scale);
}

void view_restore(struct north_view *view)
{
    if (!view)
        return;

    if (view->state == VIEW_MAXIMIZED) {
        view_set_maximized(view, false);
    } else if (view->state == VIEW_FULLSCREEN) {
        view_set_fullscreen(view, false);
    } else if (view->state == VIEW_SNAPPED) {
        NorthBox from = view->geometry;
        NorthBox to = view->saved_geometry;

        view->state = VIEW_NORMAL;
        view->snap_zone = SNAP_NONE;

        view_set_position(view, view->saved_geometry.x, view->saved_geometry.y);
        view_set_size(view, view->saved_geometry.width, view->saved_geometry.height);

        double cx = view->saved_geometry.x + view->saved_geometry.width / 2.0;
        double cy = view->saved_geometry.y + view->saved_geometry.height / 2.0;
        struct north_output *out = output_at(view->server, cx, cy);
        if (!out && !wl_list_empty(&view->server->outputs)) {
            out = wl_container_of(view->server->outputs.next, out, link);
        }
        float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
        decoration_update(&view->decor, view->saved_geometry.width, view->saved_geometry.height,
                          scale);
        anim_view_maximize(view->server, view, from, to);
    }
}

static void view_close_on_anim_finish(void *data)
{
    struct north_view *view = data;
    if (view && view->impl && view->impl->send_close) {
        view->impl->send_close(view);
    }
}

void view_close(struct north_view *view)
{
    if (!view || !view->impl || !view->impl->send_close) {
        return;
    }
    if (view->server && view->server->config && view->server->config->anim.enabled &&
        view->server->config->anim.duration_ms > 0) {
        anim_view_close(view->server, view, view_close_on_anim_finish, view);
    } else {
        view->impl->send_close(view);
    }
}

void view_apply_rules(struct north_view *view)
{
    if (!view || !view->surface || !view->surface->mapped || !view->server ||
        !view->server->config || view->rules_applied) {
        return;
    }

    const NorthConfig *config = view->server->config;
    if (!config->rules || config->rule_count == 0) {
        return;
    }

    const char *app_id = NULL;
    const char *title = NULL;

    if (view->type == NORTH_VIEW_XDG && view->xdg_toplevel) {
        app_id = view->xdg_toplevel->app_id;
        title = view->xdg_toplevel->title;
    } else if (view->type == NORTH_VIEW_XWAYLAND && view->xwayland_surface) {
        app_id = view->xwayland_surface->class;
        title = view->xwayland_surface->title;
    }

    for (size_t i = 0; i < config->rule_count; ++i) {
        const NorthWindowRule *rule = &config->rules[i];

        if (!rule->app_id && !rule->title) {
            continue;
        }
        if (rule->app_id && (!app_id || !pattern_match(rule->app_id, app_id))) {
            continue;
        }
        if (rule->title && (!title || !pattern_match(rule->title, title))) {
            continue;
        }

        view->rules_applied = true;
        LOG_INFO("Applying window rule '%s' to view (app_id='%s', title='%s')",
                 rule->name ? rule->name : "(unnamed)", app_id ? app_id : "", title ? title : "");

        if (rule->workspace >= 1 && rule->workspace <= 32) {
            workspace_move_view(view, rule->workspace - 1);
        }

        if (rule->ssd != -1) {
            view->rule_ssd = rule->ssd;
            decoration_set_enabled(&view->decor, rule->ssd == 1);
            if (view->type == NORTH_VIEW_XDG && view->server->xdg_decoration_manager) {
                struct wlr_xdg_toplevel_decoration_v1 *decor;
                wl_list_for_each(decor, &view->server->xdg_decoration_manager->decorations, link)
                {
                    if (decor->toplevel == view->xdg_toplevel) {
                        enum wlr_xdg_toplevel_decoration_v1_mode mode =
                            (rule->ssd == 1) ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                             : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
                        wlr_xdg_toplevel_decoration_v1_set_mode(decor, mode);
                        break;
                    }
                }
            }
        }

        if (view->state == VIEW_FULLSCREEN &&
            (rule->fullscreen == 0 || rule->maximize == 1 || rule->snap_zone != SNAP_NONE ||
             (rule->width > 0 && rule->fullscreen != 1))) {
            view_set_fullscreen(view, false);
        }

        if (view->state == VIEW_MAXIMIZED &&
            (rule->maximize == 0 || rule->fullscreen == 1 || rule->snap_zone != SNAP_NONE ||
             (rule->width > 0 && rule->maximize != 1))) {
            view_set_maximized(view, false);
        }

        if (rule->width > 0 && rule->height > 0) {
            view_set_size(view, rule->width, rule->height);
            struct north_output *out = view->workspace ? view->workspace->output : NULL;
            if (!out && !wl_list_empty(&view->server->outputs)) {
                out = wl_container_of(view->server->outputs.next, out, link);
            }
            if (view->state == VIEW_NORMAL) {
                NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};
                NorthBox initial = placement_initial(usable, usable, NULL, 0, view->geometry.width,
                                                     view->geometry.height);
                view_set_position(view, initial.x, initial.y);
            }
            view->saved_geometry = view->geometry;
            float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
            decoration_update(&view->decor, view->geometry.width, view->geometry.height, scale);
        }

        if (rule->snap_zone != SNAP_NONE) {
            view_set_snapped(view, rule->snap_zone);
        }

        if (rule->maximize == 1) {
            view_set_maximized(view, true);
        } else if (rule->maximize == 0 && view->state == VIEW_MAXIMIZED) {
            view_set_maximized(view, false);
        }

        if (rule->fullscreen == 1) {
            view_set_fullscreen(view, true);
        } else if (rule->fullscreen == 0 && view->state == VIEW_FULLSCREEN) {
            view_set_fullscreen(view, false);
        }

        break;
    }
}

struct north_view *view_from_node(struct wlr_scene_node *node)
{
    while (node) {
        if (node->data) {
            uintptr_t val = (uintptr_t)node->data;
            if (val > (uintptr_t)NORTH_DECOR_PART_BORDER_RIGHT) {
                return (struct north_view *)node->data;
            }
        }
        if (!node->parent) {
            break;
        }
        node = &node->parent->node;
    }
    return NULL;
}
