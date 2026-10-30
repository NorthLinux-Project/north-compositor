#ifndef LAYER_H
#define LAYER_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>

struct north_server;
struct north_output;

struct north_layer_surface {
    struct wl_list link;
    struct north_server *server;
    struct north_output *output;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer;
    struct wlr_scene_tree *popups;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener new_popup;
};

void layer_surface_handle_new(struct north_server *server,
                              struct wlr_layer_surface_v1 *wlr_layer_surface);

void layer_arrange(struct north_output *output);

#endif
