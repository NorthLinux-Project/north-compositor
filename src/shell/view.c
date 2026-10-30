#include "shell/view.h"

#include "core/server.h"
#include "output/output.h"
#include "util/log.h"

#include <stdlib.h>
#include <string.h>

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

    if (!wl_list_empty(&view->server->views)) {
        struct north_view *next_view = wl_container_of(view->server->views.next, next_view, link);
        view_focus(next_view);
    } else {
        wlr_seat_keyboard_clear_focus(view->server->seat.wlr_seat);
    }
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
    if (maximized == (view->state == VIEW_MAXIMIZED)) {
        return;
    }

    if (maximized) {
        if (view->state == VIEW_NORMAL) {
            view->saved_geometry = view->geometry;
        }
        double cx = view->geometry.x + view->geometry.width / 2.0;
        double cy = view->geometry.y + view->geometry.height / 2.0;
        struct north_output *out = output_at(view->server, cx, cy);
        if (!out && !wl_list_empty(&view->server->outputs)) {
            out = wl_container_of(view->server->outputs.next, out, link);
        }
        NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};

        int tb_h = view->decor.enabled ? view->server->config->decor.titlebar_height : 0;

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
    } else {
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
        NorthBox box;
        if (out && out->wlr_output) {
            struct wlr_box lbox;
            wlr_output_layout_get_box(view->server->output_layout, out->wlr_output, &lbox);
            box = (NorthBox){lbox.x, lbox.y, lbox.width, lbox.height};
        } else {
            box = (NorthBox){0, 0, 1920, 1080};
        }

        wlr_scene_node_reparent(&view->tree->node, view->server->layers.fullscreen);
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
        wlr_scene_node_reparent(&view->tree->node, view->server->layers.views);
        decoration_set_enabled(&view->decor, true);

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
    if (!out && !wl_list_empty(&view->server->outputs)) {
        out = wl_container_of(view->server->outputs.next, out, link);
    }
    NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};

    int tb_h = view->decor.enabled ? view->server->config->decor.titlebar_height : 0;
    NorthBox snap_box = placement_calculate_snap_box(usable, zone, tb_h);

    if (view->state == VIEW_FULLSCREEN) {
        wlr_scene_node_reparent(&view->tree->node, view->server->layers.views);
        decoration_set_enabled(&view->decor, true);
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
    }
}

void view_close(struct north_view *view)
{
    if (view && view->impl && view->impl->send_close) {
        view->impl->send_close(view);
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
