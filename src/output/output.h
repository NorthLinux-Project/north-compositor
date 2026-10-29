#ifndef OUTPUT_H
#define OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include "util/geom.h"

struct north_server;

struct north_output {
    struct wl_list link;
    struct north_server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

void output_handle_new(struct north_server *server, struct wlr_output *wlr_output);
NorthBox output_get_usable_box(struct north_output *output);
struct north_output *output_at(struct north_server *server, double lx, double ly);

#endif
