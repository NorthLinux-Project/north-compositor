#ifndef SERVER_H
#define SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>

#include "config/config.h"
#include "util/error.h"
#include "input/seat.h"

struct north_layers {
    struct wlr_scene_tree *background;
    struct wlr_scene_tree *bottom;
    struct wlr_scene_tree *views;
    struct wlr_scene_tree *top;
    struct wlr_scene_tree *fullscreen;
    struct wlr_scene_tree *overlay;
    struct wlr_scene_tree *popups;
    struct wlr_scene_tree *lock;
};

struct north_server {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor;
    struct wlr_subcompositor *subcompositor;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_output_layout *output_layout;

    struct north_layers layers;
    struct wl_list outputs;
    struct wl_list views;
    struct north_seat seat;
    NorthConfig *config;

    struct wlr_xdg_shell *xdg_shell;
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wlr_server_decoration_manager *server_decoration_manager;
    struct wlr_cursor_shape_manager_v1 *cursor_shape_manager;
    struct wlr_output_manager_v1 *output_manager;

    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_decoration;
    struct wl_listener new_server_decoration;
    struct wl_listener backend_destroy;
    struct wl_listener output_mgr_apply;
    struct wl_listener output_mgr_test;
};

BoolResult server_init(struct north_server *server, NorthConfig *config);
BoolResult server_start(struct north_server *server, const char **socket_out);
void server_finish(struct north_server *server);

#endif
