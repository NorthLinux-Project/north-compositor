#include "shell/xdg.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/placement.h"
#include "shell/view.h"
#include "shell/workspace.h"
#include "util/log.h"

#include <stdlib.h>

static void xdg_get_geometry(struct north_view *view, struct wlr_box *box)
{
    struct wlr_box geom = view->xdg_toplevel->base->geometry;
    if (geom.width == 0 && view->surface) {
        geom.width = view->surface->current.width;
        geom.height = view->surface->current.height;
    }
    *box = geom;
}

static void xdg_set_size(struct north_view *view, int width, int height)
{
    if (!view->xdg_toplevel->base->initialized)
        return;
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, width, height);
}

static void xdg_set_activated(struct north_view *view, bool activated)
{
    if (!view->xdg_toplevel->base->initialized)
        return;
    wlr_xdg_toplevel_set_activated(view->xdg_toplevel, activated);
}

static void xdg_set_maximized(struct north_view *view, bool maximized)
{
    if (!view->xdg_toplevel->base->initialized)
        return;
    wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, maximized);
}

static void xdg_set_fullscreen(struct north_view *view, bool fullscreen)
{
    if (!view->xdg_toplevel->base->initialized)
        return;
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, fullscreen);
}

static void xdg_send_close(struct north_view *view)
{
    wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

static const struct north_view_impl xdg_view_impl = {
    .get_geometry = xdg_get_geometry,
    .set_size = xdg_set_size,
    .set_activated = xdg_set_activated,
    .set_maximized = xdg_set_maximized,
    .set_fullscreen = xdg_set_fullscreen,
    .send_close = xdg_send_close,
};

static void xdg_handle_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, commit);
    struct wlr_xdg_surface *xdg_surface = view->xdg_toplevel->base;

    if (xdg_surface->initial_commit) {
        if (view->xdg_toplevel->requested.fullscreen) {
            view_set_fullscreen(view, true);
        } else if (view->xdg_toplevel->requested.maximized) {
            view_set_maximized(view, true);
        } else {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
        }
        return;
    }

    if (!xdg_surface->surface->mapped) {
        return;
    }

    struct wlr_box geom;
    view->impl->get_geometry(view, &geom);

    if (geom.width != view->geometry.width || geom.height != view->geometry.height) {
        view->geometry.width = geom.width;
        view->geometry.height = geom.height;

        float scale = 1.0f;
        double cx = view->geometry.x + view->geometry.width / 2.0;
        double cy = view->geometry.y + view->geometry.height / 2.0;
        struct north_output *out = output_at(view->server, cx, cy);
        if (!out && !wl_list_empty(&view->server->outputs)) {
            out = wl_container_of(view->server->outputs.next, out, link);
        }
        if (out && out->wlr_output) {
            scale = out->wlr_output->scale;
        }

        decoration_update(&view->decor, geom.width, geom.height, scale);
    }

    if (view->state == VIEW_FULLSCREEN && view->server->tearing_control && view->surface) {
        enum wp_tearing_control_v1_presentation_hint hint =
            wlr_tearing_control_manager_v1_surface_hint_from_surface(view->server->tearing_control,
                                                                     view->surface);
        if (hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC) {
            double cx = view->geometry.x + view->geometry.width / 2.0;
            double cy = view->geometry.y + view->geometry.height / 2.0;
            struct north_output *out = output_at(view->server, cx, cy);
            if (out && out->wlr_output) {
                wlr_output_schedule_frame(out->wlr_output);
            }
        }
    }
}

static void xdg_handle_map(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, map);
    LOG_INFO("View mapped: title='%s', app_id='%s'",
             view->xdg_toplevel->title ? view->xdg_toplevel->title : "(null)",
             view->xdg_toplevel->app_id ? view->xdg_toplevel->app_id : "(null)");

    struct wlr_box geom;
    view->impl->get_geometry(view, &geom);
    view->geometry.width = geom.width > 0 ? geom.width : 640;
    view->geometry.height = geom.height > 0 ? geom.height : 480;

    struct north_server *server = view->server;
    struct north_output *out = output_at(server, server->seat.cursor->x, server->seat.cursor->y);
    if (!out && !wl_list_empty(&server->outputs)) {
        out = wl_container_of(server->outputs.next, out, link);
    }

    if (out && out->active_workspace) {
        view->workspace = out->active_workspace;
        wlr_scene_node_reparent(&view->tree->node, out->active_workspace->tree);
    }

    NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};
    NorthBox initial =
        placement_initial(usable, usable, NULL, 0, view->geometry.width, view->geometry.height);
    view_set_position(view, initial.x, initial.y);

    wl_list_insert(&server->views, &view->link);

    if (view->xdg_toplevel->title) {
        decoration_set_title(&view->decor, view->xdg_toplevel->title);
    }

    if (view->toplevel_handle) {
        if (view->xdg_toplevel->title) {
            wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle,
                                                     view->xdg_toplevel->title);
        }
        if (view->xdg_toplevel->app_id) {
            wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle,
                                                      view->xdg_toplevel->app_id);
        }
        if (out && out->wlr_output) {
            wlr_foreign_toplevel_handle_v1_output_enter(view->toplevel_handle, out->wlr_output);
        }
    }

    float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
    decoration_update(&view->decor, view->geometry.width, view->geometry.height, scale);

    if (view->xdg_toplevel->requested.fullscreen) {
        view_set_fullscreen(view, true);
    } else if (view->xdg_toplevel->requested.maximized) {
        view_set_maximized(view, true);
    }

    view_focus(view);
}

static void xdg_handle_request_resize(struct wl_listener *listener, void *data)
{
    struct north_view *view = wl_container_of(listener, view, request_resize);
    const struct wlr_xdg_toplevel_resize_event *event = data;
    seat_begin_resize(&view->server->seat, view, event->edges);
}

static void xdg_handle_request_maximize(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_maximize);
    if (!view->xdg_toplevel->base->initialized) {
        return;
    }
    view_set_maximized(view, view->xdg_toplevel->requested.maximized);
}

static void xdg_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_fullscreen);
    if (!view->xdg_toplevel->base->initialized) {
        return;
    }
    view_set_fullscreen(view, view->xdg_toplevel->requested.fullscreen);
}

static void xdg_handle_set_title(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, set_title);
    decoration_set_title(&view->decor, view->xdg_toplevel->title);
    if (view->toplevel_handle && view->xdg_toplevel->title) {
        wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle, view->xdg_toplevel->title);
    }
}

static void xdg_handle_set_app_id(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, set_app_id);
    if (view->toplevel_handle && view->xdg_toplevel->app_id) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle,
                                                  view->xdg_toplevel->app_id);
    }
}

struct north_view *view_create_xdg(struct north_server *server, struct wlr_xdg_toplevel *toplevel)
{
    struct north_view *view = view_init_base(server, NORTH_VIEW_XDG, &xdg_view_impl);
    if (!view) {
        return NULL;
    }
    view->xdg_toplevel = toplevel;
    view->surface = toplevel->base->surface;

    view->surface_tree = wlr_scene_xdg_surface_create(view->tree, toplevel->base);
    view->surface_tree->node.data = view;

    view->map.notify = xdg_handle_map;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);

    view->unmap.notify = view_handle_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);

    view->commit.notify = xdg_handle_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);

    view->destroy.notify = view_handle_destroy;
    wl_signal_add(&toplevel->events.destroy, &view->destroy);

    view->request_move.notify = view_handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &view->request_move);

    view->request_resize.notify = xdg_handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);

    view->request_maximize.notify = xdg_handle_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);

    view->request_fullscreen.notify = xdg_handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);

    view->set_title.notify = xdg_handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &view->set_title);

    view->set_app_id.notify = xdg_handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &view->set_app_id);

    view_setup_foreign_toplevel(view);
    return view;
}

struct north_popup {
    struct north_server *server;
    struct wlr_xdg_popup *popup;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener reposition;
};

static void popup_handle_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_popup *popup = wl_container_of(listener, popup, commit);
    if (popup->popup->base->initial_commit) {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(
            popup->server->output_layout, popup->popup->base->surface->current.dx,
            popup->popup->base->surface->current.dy);
        if (wlr_out) {
            struct wlr_box box;
            wlr_output_layout_get_box(popup->server->output_layout, wlr_out, &box);
            wlr_xdg_popup_unconstrain_from_box(popup->popup, &box);
        }
    }
}

static void popup_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_popup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->reposition.link);
    free(popup);
}

static void popup_handle_reposition(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_popup *popup = wl_container_of(listener, popup, reposition);
    if (!popup->popup->base->initialized) {
        return;
    }
    struct wlr_output *wlr_out = wlr_output_layout_output_at(
        popup->server->output_layout, popup->popup->base->surface->current.dx,
        popup->popup->base->surface->current.dy);
    if (wlr_out) {
        struct wlr_box box;
        wlr_output_layout_get_box(popup->server->output_layout, wlr_out, &box);
        wlr_xdg_popup_unconstrain_from_box(popup->popup, &box);
    }
}

void server_handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    struct north_view *view = view_create_xdg(server, toplevel);
    if (!view) {
        LOG_ERROR("Failed to create north_view for new xdg_toplevel");
    }
}

void popup_create(struct north_server *server, struct wlr_xdg_popup *popup,
                  struct wlr_scene_tree *fallback_tree)
{
    struct north_popup *north_pop = calloc(1, sizeof(*north_pop));
    if (!north_pop) {
        LOG_ERROR("Failed to allocate north_popup");
        return;
    }
    north_pop->server = server;
    north_pop->popup = popup;

    struct wlr_scene_tree *parent_tree = fallback_tree ? fallback_tree : server->layers.popups;
    struct wlr_xdg_surface *parent_surface = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (parent_surface && parent_surface->data) {
        parent_tree = parent_surface->data;
    }

    north_pop->scene_tree = wlr_scene_xdg_surface_create(parent_tree, popup->base);
    popup->base->data = north_pop->scene_tree;

    north_pop->commit.notify = popup_handle_commit;
    wl_signal_add(&popup->base->surface->events.commit, &north_pop->commit);

    north_pop->destroy.notify = popup_handle_destroy;
    wl_signal_add(&popup->events.destroy, &north_pop->destroy);

    north_pop->reposition.notify = popup_handle_reposition;
    wl_signal_add(&popup->events.reposition, &north_pop->reposition);
}

void server_handle_new_xdg_popup(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;
    popup_create(server, popup, server->layers.popups);
}
