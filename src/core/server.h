#ifndef SERVER_H
#define SERVER_H

#include "config/config.h"
#include "input/seat.h"
#include "shell/placement.h"
#include "util/error.h"

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland/xwayland.h>

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
    struct wlr_layer_shell_v1 *layer_shell;
    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;
    struct wlr_idle_notifier_v1 *idle_notifier;
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit_manager;
    int idle_inhibit_count;

    struct wlr_session_lock_manager_v1 *session_lock_manager;
    struct north_session_lock *session_lock;
    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
    struct wlr_screencopy_manager_v1 *screencopy_manager;
    struct wlr_ext_image_copy_capture_manager_v1 *image_copy_capture_manager;
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager;
    struct wlr_pointer_constraint_v1 *active_constraint;
    struct wlr_xwayland *xwayland;

    struct wlr_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wlr_viewporter *viewporter;
    struct wlr_presentation *presentation;
    struct wlr_tearing_control_manager_v1 *tearing_control;
    struct wlr_linux_drm_syncobj_manager_v1 *syncobj_manager;

    struct wlr_scene_tree *snap_preview_tree;
    struct wlr_scene_rect *snap_preview_fill;
    struct wlr_scene_rect *snap_preview_borders[4];
    enum north_snap_zone active_snap_zone;

    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_decoration;
    struct wl_listener new_server_decoration;
    struct wl_listener new_layer_surface;
    struct wl_listener new_idle_inhibitor;
    struct wl_listener new_session_lock;
    struct wl_listener new_pointer_constraint;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener constraint_destroy;
    struct wl_listener xwayland_ready;
    struct wl_listener new_xwayland_surface;
    struct wl_listener backend_destroy;
    struct wl_listener output_mgr_apply;
    struct wl_listener output_mgr_test;
};

BoolResult server_init(struct north_server *server, NorthConfig *config);
BoolResult server_start(struct north_server *server, const char **socket_out);
void server_finish(struct north_server *server);
void server_set_session_locked(struct north_server *server, bool locked);
void server_set_active_constraint(struct north_server *server,
                                  struct wlr_pointer_constraint_v1 *constraint);
void server_break_pointer_constraint(struct north_server *server);

#endif
