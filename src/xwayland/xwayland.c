#include "xwayland/xwayland.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/placement.h"
#include "shell/view.h"
#include "shell/workspace.h"
#include "util/log.h"

#include <stdlib.h>

struct north_xwayland_unmanaged {
    struct north_server *server;
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree *scene_tree;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener set_geometry;
    struct wl_listener associate;
    struct wl_listener dissociate;
};

static void unmanaged_handle_map(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, map);
    struct wlr_xwayland_surface *xsurface = unmanaged->xsurface;

    LOG_INFO("Xwayland unmanaged surface mapped at (%d, %d)", xsurface->x, xsurface->y);
    unmanaged->scene_tree =
        wlr_scene_subsurface_tree_create(unmanaged->server->layers.popups, xsurface->surface);
    if (unmanaged->scene_tree) {
        wlr_scene_node_set_position(&unmanaged->scene_tree->node, xsurface->x, xsurface->y);
    }
}

static void unmanaged_handle_unmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, unmap);
    if (unmanaged->scene_tree) {
        wlr_scene_node_destroy(&unmanaged->scene_tree->node);
        unmanaged->scene_tree = NULL;
    }
}

static void unmanaged_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, destroy);
    wl_list_remove(&unmanaged->map.link);
    wl_list_remove(&unmanaged->unmap.link);
    wl_list_remove(&unmanaged->destroy.link);
    wl_list_remove(&unmanaged->set_geometry.link);
    wl_list_remove(&unmanaged->associate.link);
    wl_list_remove(&unmanaged->dissociate.link);
    if (unmanaged->scene_tree) {
        wlr_scene_node_destroy(&unmanaged->scene_tree->node);
    }
    free(unmanaged);
}

static void unmanaged_handle_set_geometry(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, set_geometry);
    if (unmanaged->scene_tree) {
        wlr_scene_node_set_position(&unmanaged->scene_tree->node, unmanaged->xsurface->x,
                                    unmanaged->xsurface->y);
    }
}

static void unmanaged_handle_associate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, associate);
    if (!unmanaged->xsurface->surface)
        return;
    if (!unmanaged->map.link.prev) {
        wl_signal_add(&unmanaged->xsurface->surface->events.map, &unmanaged->map);
    }
    if (!unmanaged->unmap.link.prev) {
        wl_signal_add(&unmanaged->xsurface->surface->events.unmap, &unmanaged->unmap);
    }
}

static void unmanaged_handle_dissociate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, dissociate);
    if (unmanaged->map.link.prev && unmanaged->map.link.next) {
        wl_list_remove(&unmanaged->map.link);
        wl_list_init(&unmanaged->map.link);
    }
    if (unmanaged->unmap.link.prev && unmanaged->unmap.link.next) {
        wl_list_remove(&unmanaged->unmap.link);
        wl_list_init(&unmanaged->unmap.link);
    }
    if (unmanaged->scene_tree) {
        wlr_scene_node_destroy(&unmanaged->scene_tree->node);
        unmanaged->scene_tree = NULL;
    }
}

static void xwayland_create_unmanaged(struct north_server *server,
                                      struct wlr_xwayland_surface *xsurface)
{
    struct north_xwayland_unmanaged *unmanaged = calloc(1, sizeof(*unmanaged));
    if (!unmanaged)
        return;

    unmanaged->server = server;
    unmanaged->xsurface = xsurface;
    xsurface->data = unmanaged;

    unmanaged->map.notify = unmanaged_handle_map;
    unmanaged->unmap.notify = unmanaged_handle_unmap;
    wl_list_init(&unmanaged->map.link);
    wl_list_init(&unmanaged->unmap.link);

    if (xsurface->surface) {
        wl_signal_add(&xsurface->surface->events.map, &unmanaged->map);
        wl_signal_add(&xsurface->surface->events.unmap, &unmanaged->unmap);
    }

    unmanaged->destroy.notify = unmanaged_handle_destroy;
    wl_signal_add(&xsurface->events.destroy, &unmanaged->destroy);

    unmanaged->set_geometry.notify = unmanaged_handle_set_geometry;
    wl_signal_add(&xsurface->events.set_geometry, &unmanaged->set_geometry);

    unmanaged->associate.notify = unmanaged_handle_associate;
    wl_signal_add(&xsurface->events.associate, &unmanaged->associate);

    unmanaged->dissociate.notify = unmanaged_handle_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &unmanaged->dissociate);
}

static void xwayland_get_geometry(struct north_view *view, struct wlr_box *box)
{
    box->x = view->xwayland_surface->x;
    box->y = view->xwayland_surface->y;
    box->width = view->xwayland_surface->width;
    box->height = view->xwayland_surface->height;
    if (box->width == 0 && view->surface) {
        box->width = view->surface->current.width;
        box->height = view->surface->current.height;
    }
}

static void xwayland_set_size(struct north_view *view, int width, int height)
{
    wlr_xwayland_surface_configure(view->xwayland_surface, view->geometry.x, view->geometry.y,
                                   width, height);
}

static void xwayland_set_activated(struct north_view *view, bool activated)
{
    wlr_xwayland_surface_activate(view->xwayland_surface, activated);
}

static void xwayland_set_maximized(struct north_view *view, bool maximized)
{
    wlr_xwayland_surface_set_maximized(view->xwayland_surface, maximized, maximized);
}

static void xwayland_set_fullscreen(struct north_view *view, bool fullscreen)
{
    wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, fullscreen);
}

static void xwayland_send_close(struct north_view *view)
{
    wlr_xwayland_surface_close(view->xwayland_surface);
}

static const struct north_view_impl xwayland_view_impl = {
    .get_geometry = xwayland_get_geometry,
    .set_size = xwayland_set_size,
    .set_activated = xwayland_set_activated,
    .set_maximized = xwayland_set_maximized,
    .set_fullscreen = xwayland_set_fullscreen,
    .send_close = xwayland_send_close,
};

static void xwayland_handle_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, commit);
    if (!view->surface || !view->surface->mapped)
        return;

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

static void xwayland_handle_map(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, map);
    struct wlr_xwayland_surface *xsurface = view->xwayland_surface;

    LOG_INFO("Xwayland View mapped: title='%s', class='%s'",
             xsurface->title ? xsurface->title : "(null)",
             xsurface->class ? xsurface->class : "(null)");

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

    if (xsurface->title) {
        decoration_set_title(&view->decor, xsurface->title);
    }

    if (xsurface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER) {
        decoration_set_enabled(&view->decor, false);
    }

    if (view->toplevel_handle) {
        if (xsurface->title) {
            wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle, xsurface->title);
        }
        if (xsurface->class) {
            wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle, xsurface->class);
        }
        if (out && out->wlr_output) {
            wlr_foreign_toplevel_handle_v1_output_enter(view->toplevel_handle, out->wlr_output);
        }
    }

    float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
    decoration_update(&view->decor, view->geometry.width, view->geometry.height, scale);

    if (xsurface->maximized_horz && xsurface->maximized_vert) {
        view_set_maximized(view, true);
    }

    view_focus(view);
}

static void xwayland_handle_request_configure(struct wl_listener *listener, void *data)
{
    struct north_view *view = wl_container_of(listener, view, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;

    if (view->state != VIEW_NORMAL) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->geometry.x, view->geometry.y,
                                       view->geometry.width, view->geometry.height);
        return;
    }

    if (!view->surface || !view->surface->mapped) {
        view->geometry.width = event->width;
        view->geometry.height = event->height;
        wlr_xwayland_surface_configure(view->xwayland_surface, event->x, event->y, event->width,
                                       event->height);
    } else {
        view_set_size(view, event->width, event->height);
        wlr_xwayland_surface_configure(view->xwayland_surface, view->geometry.x, view->geometry.y,
                                       view->geometry.width, view->geometry.height);
    }
}

static void xwayland_handle_request_resize(struct wl_listener *listener, void *data)
{
    struct north_view *view = wl_container_of(listener, view, request_resize);
    struct wlr_xwayland_resize_event *event = data;
    seat_begin_resize(&view->server->seat, view, event->edges);
}

static void xwayland_handle_request_maximize(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_maximize);
    bool should_maximize =
        view->xwayland_surface->maximized_horz && view->xwayland_surface->maximized_vert;
    view_set_maximized(view, should_maximize);
}

static void xwayland_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_fullscreen);
    view_set_fullscreen(view, view->xwayland_surface->fullscreen);
}

static void xwayland_handle_request_activate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_activate);
    view_focus(view);
}

static void xwayland_handle_set_title(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, set_title);
    if (view->xwayland_surface->title) {
        decoration_set_title(&view->decor, view->xwayland_surface->title);
        if (view->toplevel_handle) {
            wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle,
                                                     view->xwayland_surface->title);
        }
    }
}

static void xwayland_handle_set_class(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, set_app_id);
    if (view->toplevel_handle && view->xwayland_surface->class) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle,
                                                  view->xwayland_surface->class);
    }
}

static void xwayland_handle_set_decorations(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, set_decorations);
    bool enabled =
        (view->xwayland_surface->decorations != WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER);
    decoration_set_enabled(&view->decor, enabled);
}

static void xwayland_handle_associate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, associate);
    struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
    LOG_DEBUG("Xwayland associate: id=0x%x, surface=%p", xsurface->window_id,
              (void *)xsurface->surface);
    if (!xsurface->surface)
        return;

    view->surface = xsurface->surface;
    if (!view->surface_tree) {
        view->surface_tree = wlr_scene_subsurface_tree_create(view->tree, xsurface->surface);
        if (view->surface_tree) {
            view->surface_tree->node.data = view;
        }
    }

    if (wl_list_empty(&view->map.link)) {
        wl_signal_add(&xsurface->surface->events.map, &view->map);
    }
    if (wl_list_empty(&view->unmap.link)) {
        wl_signal_add(&xsurface->surface->events.unmap, &view->unmap);
    }
    if (wl_list_empty(&view->commit.link)) {
        wl_signal_add(&xsurface->surface->events.commit, &view->commit);
    }

    if (xsurface->surface->mapped) {
        xwayland_handle_map(&view->map, NULL);
    }
}

static void xwayland_handle_dissociate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_view *view = wl_container_of(listener, view, dissociate);

    if (!wl_list_empty(&view->map.link)) {
        wl_list_remove(&view->map.link);
        wl_list_init(&view->map.link);
    }
    if (!wl_list_empty(&view->unmap.link)) {
        wl_list_remove(&view->unmap.link);
        wl_list_init(&view->unmap.link);
    }
    if (!wl_list_empty(&view->commit.link)) {
        wl_list_remove(&view->commit.link);
        wl_list_init(&view->commit.link);
    }

    if (view->surface_tree) {
        wlr_scene_node_destroy(&view->surface_tree->node);
        view->surface_tree = NULL;
    }
    view->surface = NULL;
}

struct north_view *view_create_xwayland(struct north_server *server,
                                        struct wlr_xwayland_surface *xsurface)
{
    struct north_view *view = view_init_base(server, NORTH_VIEW_XWAYLAND, &xwayland_view_impl);
    if (!view)
        return NULL;

    view->xwayland_surface = xsurface;
    view->surface = xsurface->surface;

    if (xsurface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER) {
        decoration_set_enabled(&view->decor, false);
    }

    view->map.notify = xwayland_handle_map;
    view->unmap.notify = view_handle_unmap;
    view->commit.notify = xwayland_handle_commit;
    wl_list_init(&view->map.link);
    wl_list_init(&view->unmap.link);
    wl_list_init(&view->commit.link);

    view->associate.notify = xwayland_handle_associate;
    wl_signal_add(&xsurface->events.associate, &view->associate);

    view->dissociate.notify = xwayland_handle_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &view->dissociate);

    view->destroy.notify = view_handle_destroy;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);

    view->request_configure.notify = xwayland_handle_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &view->request_configure);

    view->request_move.notify = view_handle_request_move;
    wl_signal_add(&xsurface->events.request_move, &view->request_move);

    view->request_resize.notify = xwayland_handle_request_resize;
    wl_signal_add(&xsurface->events.request_resize, &view->request_resize);

    view->request_maximize.notify = xwayland_handle_request_maximize;
    wl_signal_add(&xsurface->events.request_maximize, &view->request_maximize);

    view->request_fullscreen.notify = xwayland_handle_request_fullscreen;
    wl_signal_add(&xsurface->events.request_fullscreen, &view->request_fullscreen);

    view->request_activate.notify = xwayland_handle_request_activate;
    wl_signal_add(&xsurface->events.request_activate, &view->request_activate);

    view->set_title.notify = xwayland_handle_set_title;
    wl_signal_add(&xsurface->events.set_title, &view->set_title);

    view->set_app_id.notify = xwayland_handle_set_class;
    wl_signal_add(&xsurface->events.set_class, &view->set_app_id);

    view->set_decorations.notify = xwayland_handle_set_decorations;
    wl_signal_add(&xsurface->events.set_decorations, &view->set_decorations);

    if (xsurface->surface) {
        xwayland_handle_associate(&view->associate, NULL);
    }

    view_setup_foreign_toplevel(view);
    return view;
}

static void xwayland_handle_new_surface(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;
    LOG_DEBUG("Xwayland new_surface: id=0x%x, override_redirect=%d", xsurface->window_id,
              xsurface->override_redirect);

    if (xsurface->override_redirect) {
        xwayland_create_unmanaged(server, xsurface);
        return;
    }

    struct north_view *view = view_create_xwayland(server, xsurface);
    if (!view) {
        LOG_ERROR("Failed to create north_view for new Xwayland surface");
    }
}

static void xwayland_handle_ready(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_server *server = wl_container_of(listener, server, xwayland_ready);
    LOG_INFO("Xwayland server is ready on DISPLAY=%s", server->xwayland->display_name);

    setenv("DISPLAY", server->xwayland->display_name, 1);
    wlr_xwayland_set_seat(server->xwayland, server->seat.wlr_seat);
}

void xwayland_init(struct north_server *server)
{
    server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
    if (!server->xwayland) {
        LOG_WARN("Failed to create Xwayland server (is Xwayland installed?)");
        return;
    }

    server->xwayland_ready.notify = xwayland_handle_ready;
    wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);

    server->new_xwayland_surface.notify = xwayland_handle_new_surface;
    wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);

    setenv("DISPLAY", server->xwayland->display_name, 1);
    LOG_INFO("Xwayland initialized in lazy mode on DISPLAY=%s", server->xwayland->display_name);
}

void xwayland_finish(struct north_server *server)
{
    if (!server->xwayland)
        return;

    wl_list_remove(&server->xwayland_ready.link);
    wl_list_remove(&server->new_xwayland_surface.link);
    wlr_xwayland_destroy(server->xwayland);
    server->xwayland = NULL;
}
