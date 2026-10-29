#include "shell/view.h"
#include "core/server.h"
#include "output/output.h"
#include "shell/placement.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

static void view_handle_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, commit);
    struct wlr_xdg_surface *xdg_surface = view->toplevel->base;

    if (xdg_surface->initial_commit) {
        wlr_xdg_toplevel_set_size(view->toplevel, 0, 0);
        return;
    }

    if (!xdg_surface->surface->mapped) {
        return;
    }

    struct wlr_box geom = xdg_surface->geometry;
    if (geom.width == 0 && xdg_surface->surface) {
        geom.width = xdg_surface->surface->current.width;
        geom.height = xdg_surface->surface->current.height;
    }

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
}

static void view_handle_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, map);
    LOG_INFO("View mapped: title='%s', app_id='%s'",
             view->toplevel->title ? view->toplevel->title : "(null)",
             view->toplevel->app_id ? view->toplevel->app_id : "(null)");

    struct wlr_box geom = view->toplevel->base->geometry;
    if (geom.width == 0 && view->toplevel->base->surface) {
        geom.width = view->toplevel->base->surface->current.width;
        geom.height = view->toplevel->base->surface->current.height;
    }
    view->geometry.width = geom.width > 0 ? geom.width : 640;
    view->geometry.height = geom.height > 0 ? geom.height : 480;

    struct north_server *server = view->server;
    struct north_output *out = output_at(server, server->seat.cursor->x, server->seat.cursor->y);
    if (!out && !wl_list_empty(&server->outputs)) {
        out = wl_container_of(server->outputs.next, out, link);
    }

    NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};
    NorthBox initial = placement_initial(usable, usable, NULL, 0, view->geometry.width, view->geometry.height);
    view_set_position(view, initial.x, initial.y);

    wl_list_insert(&server->views, &view->link);

    if (view->toplevel->title) {
        decoration_set_title(&view->decor, view->toplevel->title);
    }

    float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
    decoration_update(&view->decor, view->geometry.width, view->geometry.height, scale);

    if (view->toplevel->requested.maximized) {
        view_set_maximized(view, true);
    }

    view_focus(view);
}

static void view_handle_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, unmap);
    LOG_INFO("View unmapped: title='%s'", view->toplevel->title ? view->toplevel->title : "(null)");

    wl_list_remove(&view->link);
    wl_list_init(&view->link);

    if (!wl_list_empty(&view->server->views)) {
        struct north_view *next_view = wl_container_of(view->server->views.next, next_view, link);
        view_focus(next_view);
    } else {
        wlr_seat_keyboard_clear_focus(view->server->seat.wlr_seat);
    }
}

static void view_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, destroy);
    view_destroy(view);
}

static void view_handle_request_move(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_move);
    seat_begin_move(&view->server->seat, view);
}

static void view_handle_request_resize(struct wl_listener *listener, void *data) {
    struct north_view *view = wl_container_of(listener, view, request_resize);
    const struct wlr_xdg_toplevel_resize_event *event = data;
    seat_begin_resize(&view->server->seat, view, event->edges);
}

static void view_handle_request_maximize(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_maximize);
    view_set_maximized(view, view->toplevel->requested.maximized);
}

static void view_handle_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, request_fullscreen);
    view_set_fullscreen(view, view->toplevel->requested.fullscreen);
}

static void view_handle_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_view *view = wl_container_of(listener, view, set_title);
    decoration_set_title(&view->decor, view->toplevel->title);
}

struct north_view *view_create(struct north_server *server, struct wlr_xdg_toplevel *toplevel) {
    struct north_view *view = calloc(1, sizeof(*view));
    if (!view) {
        return NULL;
    }
    view->server = server;
    view->toplevel = toplevel;
    wl_list_init(&view->link);

    view->tree = wlr_scene_tree_create(server->layers.views);
    view->tree->node.data = view;

    view->surface_tree = wlr_scene_xdg_surface_create(view->tree, toplevel->base);
    view->surface_tree->node.data = view;

    decoration_init(&view->decor, view);

    view->map.notify = view_handle_map;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);

    view->unmap.notify = view_handle_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);

    view->commit.notify = view_handle_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);

    view->destroy.notify = view_handle_destroy;
    wl_signal_add(&toplevel->events.destroy, &view->destroy);

    view->request_move.notify = view_handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &view->request_move);

    view->request_resize.notify = view_handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);

    view->request_maximize.notify = view_handle_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);

    view->request_fullscreen.notify = view_handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);

    view->set_title.notify = view_handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &view->set_title);

    return view;
}

void view_destroy(struct north_view *view) {
    if (!view) return;

    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->set_title.link);

    if (view->link.next && view->link.prev) {
        wl_list_remove(&view->link);
    }

    decoration_finish(&view->decor);
    wlr_scene_node_destroy(&view->tree->node);
    free(view);
}

void view_focus(struct north_view *view) {
    if (!view) return;

    struct north_server *server = view->server;
    struct wlr_seat *seat = server->seat.wlr_seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;

    if (prev_surface) {
        struct wlr_xdg_toplevel *prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel) {
            struct north_view *prev_view;
            wl_list_for_each(prev_view, &server->views, link) {
                if (prev_view->toplevel == prev_toplevel) {
                    wlr_xdg_toplevel_set_activated(prev_toplevel, false);
                    decoration_set_active(&prev_view->decor, false);
                    break;
                }
            }
        }
    }

    wlr_scene_node_raise_to_top(&view->tree->node);

    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);

    wlr_xdg_toplevel_set_activated(view->toplevel, true);
    decoration_set_active(&view->decor, true);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, view->toplevel->base->surface,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
}

void view_set_position(struct north_view *view, int x, int y) {
    view->geometry.x = x;
    view->geometry.y = y;
    wlr_scene_node_set_position(&view->tree->node, x, y);
}

void view_set_size(struct north_view *view, int width, int height) {
    if (width < 50) width = 50;
    if (height < 50) height = 50;
    view->geometry.width = width;
    view->geometry.height = height;
    wlr_xdg_toplevel_set_size(view->toplevel, width, height);
}

void view_set_maximized(struct north_view *view, bool maximized) {
    if (maximized == (view->state == VIEW_MAXIMIZED)) {
        return;
    }

    if (maximized) {
        view->saved_geometry = view->geometry;
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
        wlr_xdg_toplevel_set_size(view->toplevel, usable.width, usable.height - tb_h);
        wlr_xdg_toplevel_set_maximized(view->toplevel, true);

        float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
        decoration_update(&view->decor, usable.width, usable.height - tb_h, scale);
    } else {
        view->state = VIEW_NORMAL;
        view_set_position(view, view->saved_geometry.x, view->saved_geometry.y);
        view->geometry.width = view->saved_geometry.width;
        view->geometry.height = view->saved_geometry.height;
        wlr_xdg_toplevel_set_size(view->toplevel, view->saved_geometry.width, view->saved_geometry.height);
        wlr_xdg_toplevel_set_maximized(view->toplevel, false);

        double cx = view->saved_geometry.x + view->saved_geometry.width / 2.0;
        double cy = view->saved_geometry.y + view->saved_geometry.height / 2.0;
        struct north_output *out = output_at(view->server, cx, cy);
        if (!out && !wl_list_empty(&view->server->outputs)) {
            out = wl_container_of(view->server->outputs.next, out, link);
        }
        float scale = out && out->wlr_output ? out->wlr_output->scale : 1.0f;
        decoration_update(&view->decor, view->saved_geometry.width, view->saved_geometry.height, scale);
    }
}

void view_set_fullscreen(struct north_view *view, bool fullscreen) {
    if (fullscreen == (view->state == VIEW_FULLSCREEN)) {
        return;
    }

    if (fullscreen) {
        view->saved_geometry = view->geometry;
        struct north_output *out = output_at(view->server, view->geometry.x, view->geometry.y);
        NorthBox box = out ? output_get_usable_box(out) : (NorthBox){0, 0, 1920, 1080};

        wlr_scene_node_reparent(&view->tree->node, view->server->layers.fullscreen);
        decoration_set_enabled(&view->decor, false);

        view_set_position(view, box.x, box.y);
        view_set_size(view, box.width, box.height);
        wlr_xdg_toplevel_set_fullscreen(view->toplevel, true);
        view->state = VIEW_FULLSCREEN;
    } else {
        wlr_scene_node_reparent(&view->tree->node, view->server->layers.views);
        decoration_set_enabled(&view->decor, true);

        view_set_position(view, view->saved_geometry.x, view->saved_geometry.y);
        view_set_size(view, view->saved_geometry.width, view->saved_geometry.height);
        wlr_xdg_toplevel_set_fullscreen(view->toplevel, false);
        view->state = VIEW_NORMAL;
    }
}

struct north_view *view_from_node(struct wlr_scene_node *node) {
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
