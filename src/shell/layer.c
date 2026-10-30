#include "shell/layer.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/view.h"
#include "shell/xdg.h"
#include "util/log.h"

#include <stdlib.h>

static struct wlr_scene_tree *layer_get_parent_tree(struct north_server *server,
                                                    enum zwlr_layer_shell_v1_layer layer)
{
    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return server->layers.background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return server->layers.bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return server->layers.top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return server->layers.overlay;
    }
    return server->layers.top;
}

void layer_arrange(struct north_output *output)
{
    if (!output || !output->wlr_output) {
        return;
    }
    struct wlr_box full_area;
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &full_area);
    struct wlr_box usable = full_area;

    static const enum zwlr_layer_shell_v1_layer order[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    };

    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        struct north_layer_surface *ls;
        wl_list_for_each(ls, &output->layer_surfaces, link)
        {
            if (!ls->wlr_layer_surface->initialized) {
                continue;
            }
            if (ls->wlr_layer_surface->current.layer != order[i]) {
                continue;
            }
            wlr_scene_layer_surface_v1_configure(ls->scene_layer, &full_area, &usable);
        }
    }

    NorthBox old_usable = output->usable_area;
    output->usable_area =
        (NorthBox){.x = usable.x, .y = usable.y, .width = usable.width, .height = usable.height};

    if (old_usable.x != output->usable_area.x || old_usable.y != output->usable_area.y ||
        old_usable.width != output->usable_area.width ||
        old_usable.height != output->usable_area.height) {
        struct north_view *view;
        wl_list_for_each(view, &output->server->views, link)
        {
            if (view->state == VIEW_MAXIMIZED) {
                double cx = view->geometry.x + view->geometry.width / 2.0;
                double cy = view->geometry.y + view->geometry.height / 2.0;
                struct north_output *out = output_at(output->server, cx, cy);
                if (out == output) {
                    view_set_maximized(view, true);
                }
            }
        }
    }
}

static void layer_handle_map(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_layer_surface *surface = wl_container_of(listener, surface, map);
    layer_arrange(surface->output);

    if (surface->wlr_layer_surface->current.keyboard_interactive ==
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
        wlr_seat_set_keyboard(surface->server->seat.wlr_seat,
                              surface->server->seat.wlr_seat->keyboard_state.keyboard);
        wlr_seat_keyboard_notify_enter(surface->server->seat.wlr_seat,
                                       surface->wlr_layer_surface->surface, NULL, 0, NULL);
    }
}

static void layer_handle_unmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_layer_surface *surface = wl_container_of(listener, surface, unmap);
    layer_arrange(surface->output);

    if (surface->server->seat.wlr_seat->keyboard_state.focused_surface ==
        surface->wlr_layer_surface->surface) {
        if (!wl_list_empty(&surface->server->views)) {
            struct north_view *top = wl_container_of(surface->server->views.next, top, link);
            view_focus(top);
        } else {
            wlr_seat_keyboard_clear_focus(surface->server->seat.wlr_seat);
        }
    }
}

static void layer_handle_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_layer_surface *surface = wl_container_of(listener, surface, commit);
    struct wlr_layer_surface_v1 *wlr_ls = surface->wlr_layer_surface;

    if (!surface->scene_layer) {
        return;
    }

    struct wlr_scene_tree *target_parent =
        layer_get_parent_tree(surface->server, wlr_ls->current.layer);
    if (surface->scene_layer->tree->node.parent != target_parent) {
        wlr_scene_node_reparent(&surface->scene_layer->tree->node, target_parent);
    }

    if (wlr_ls->initial_commit || wlr_ls->current.committed != 0) {
        layer_arrange(surface->output);
    }
}

static void layer_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_layer_surface *surface = wl_container_of(listener, surface, destroy);

    wl_list_remove(&surface->map.link);
    wl_list_remove(&surface->unmap.link);
    wl_list_remove(&surface->commit.link);
    wl_list_remove(&surface->destroy.link);
    wl_list_remove(&surface->new_popup.link);
    wl_list_remove(&surface->link);

    if (surface->popups) {
        wlr_scene_node_destroy(&surface->popups->node);
        surface->popups = NULL;
    }

    struct north_output *output = surface->output;
    free(surface);

    layer_arrange(output);
}

static void layer_handle_new_popup(struct wl_listener *listener, void *data)
{
    struct north_layer_surface *surface = wl_container_of(listener, surface, new_popup);
    struct wlr_xdg_popup *popup = data;
    popup_create(surface->server, popup, surface->popups);
}

void layer_surface_handle_new(struct north_server *server,
                              struct wlr_layer_surface_v1 *wlr_layer_surface)
{
    if (!wlr_layer_surface->output) {
        struct north_output *out = NULL;
        if (!wl_list_empty(&server->outputs)) {
            out = output_at(server, server->seat.cursor->x, server->seat.cursor->y);
            if (!out) {
                out = wl_container_of(server->outputs.next, out, link);
            }
        }
        if (!out) {
            LOG_ERROR("No output available for layer surface");
            wlr_layer_surface_v1_destroy(wlr_layer_surface);
            return;
        }
        wlr_layer_surface->output = out->wlr_output;
    }

    struct north_output *output = NULL;
    struct north_output *o;
    wl_list_for_each(o, &server->outputs, link)
    {
        if (o->wlr_output == wlr_layer_surface->output) {
            output = o;
            break;
        }
    }
    if (!output) {
        LOG_ERROR("Could not match north_output for layer surface");
        wlr_layer_surface_v1_destroy(wlr_layer_surface);
        return;
    }

    struct north_layer_surface *surface = calloc(1, sizeof(*surface));
    if (!surface) {
        LOG_ERROR("Failed allocating north_layer_surface");
        return;
    }
    surface->server = server;
    surface->output = output;
    surface->wlr_layer_surface = wlr_layer_surface;

    struct wlr_scene_tree *parent_tree =
        layer_get_parent_tree(server, wlr_layer_surface->current.layer);
    surface->scene_layer = wlr_scene_layer_surface_v1_create(parent_tree, wlr_layer_surface);
    if (!surface->scene_layer) {
        LOG_ERROR("Failed creating wlr_scene_layer_surface_v1");
        free(surface);
        return;
    }

    surface->popups = wlr_scene_tree_create(&server->scene->tree);
    wlr_layer_surface->data = surface;

    surface->map.notify = layer_handle_map;
    wl_signal_add(&wlr_layer_surface->surface->events.map, &surface->map);

    surface->unmap.notify = layer_handle_unmap;
    wl_signal_add(&wlr_layer_surface->surface->events.unmap, &surface->unmap);

    surface->commit.notify = layer_handle_commit;
    wl_signal_add(&wlr_layer_surface->surface->events.commit, &surface->commit);

    surface->destroy.notify = layer_handle_destroy;
    wl_signal_add(&wlr_layer_surface->events.destroy, &surface->destroy);

    surface->new_popup.notify = layer_handle_new_popup;
    wl_signal_add(&wlr_layer_surface->events.new_popup, &surface->new_popup);

    wl_list_insert(&output->layer_surfaces, &surface->link);

    layer_arrange(output);
}
