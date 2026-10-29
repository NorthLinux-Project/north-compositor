#include "core/server.h"
#include "shell/view.h"
#include "output/output.h"
#include "util/log.h"
#include <stdlib.h>

struct north_popup {
    struct north_server *server;
    struct wlr_xdg_popup *popup;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener reposition;
};

static void popup_handle_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_popup *popup = wl_container_of(listener, popup, commit);
    if (popup->popup->base->initial_commit) {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(
            popup->server->output_layout,
            popup->popup->base->surface->current.dx,
            popup->popup->base->surface->current.dy);
        if (wlr_out) {
            struct wlr_box box;
            wlr_output_layout_get_box(popup->server->output_layout, wlr_out, &box);
            wlr_xdg_popup_unconstrain_from_box(popup->popup, &box);
        }
    }
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_popup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->reposition.link);
    free(popup);
}

static void popup_handle_reposition(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_popup *popup = wl_container_of(listener, popup, reposition);
    struct wlr_output *wlr_out = wlr_output_layout_output_at(
        popup->server->output_layout,
        popup->popup->base->surface->current.dx,
        popup->popup->base->surface->current.dy);
    if (wlr_out) {
        struct wlr_box box;
        wlr_output_layout_get_box(popup->server->output_layout, wlr_out, &box);
        wlr_xdg_popup_unconstrain_from_box(popup->popup, &box);
    }
}

void server_handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct north_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    struct north_view *view = view_create(server, toplevel);
    if (!view) {
        LOG_ERROR("Failed to create north_view for new xdg_toplevel");
    }
}

void server_handle_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct north_server *server = wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;

    struct north_popup *north_pop = calloc(1, sizeof(*north_pop));
    if (!north_pop) {
        LOG_ERROR("Failed to allocate north_popup");
        return;
    }
    north_pop->server = server;
    north_pop->popup = popup;

    struct wlr_scene_tree *parent_tree = server->layers.popups;
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
