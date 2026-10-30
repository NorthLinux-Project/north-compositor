#ifndef VIEW_H
#define VIEW_H

#include "decor/decoration.h"
#include "shell/placement.h"
#include "util/geom.h"

#include <stdbool.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland/xwayland.h>

enum north_view_state {
    VIEW_NORMAL = 0,
    VIEW_MAXIMIZED,
    VIEW_FULLSCREEN,
    VIEW_MINIMIZED,
    VIEW_SNAPPED,
};

enum north_view_type {
    NORTH_VIEW_XDG,
    NORTH_VIEW_XWAYLAND,
};

struct north_server;
struct north_view;

struct north_view_impl {
    void (*get_geometry)(struct north_view *view, struct wlr_box *box);
    void (*set_size)(struct north_view *view, int width, int height);
    void (*set_activated)(struct north_view *view, bool activated);
    void (*set_maximized)(struct north_view *view, bool maximized);
    void (*set_fullscreen)(struct north_view *view, bool fullscreen);
    void (*send_close)(struct north_view *view);
};

struct north_view {
    struct wl_list link;
    struct north_server *server;
    enum north_view_type type;
    const struct north_view_impl *impl;

    union {
        struct wlr_xdg_toplevel *xdg_toplevel;
        struct wlr_xwayland_surface *xwayland_surface;
    };

    struct wlr_surface *surface;
    struct wlr_scene_tree *tree;
    struct wlr_scene_tree *surface_tree;
    struct north_decoration decor;

    enum north_view_state state;
    enum north_snap_zone snap_zone;
    NorthBox geometry;
    NorthBox saved_geometry;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;

    struct wl_listener associate;
    struct wl_listener dissociate;

    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_minimize;
    struct wl_listener request_activate;
    struct wl_listener request_configure;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
    struct wl_listener set_decorations;

    struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
    struct wl_listener foreign_request_maximize;
    struct wl_listener foreign_request_minimize;
    struct wl_listener foreign_request_activate;
    struct wl_listener foreign_request_fullscreen;
    struct wl_listener foreign_request_close;
};

struct north_view *view_init_base(struct north_server *server, enum north_view_type type,
                                  const struct north_view_impl *impl);
void view_setup_foreign_toplevel(struct north_view *view);
void view_handle_unmap(struct wl_listener *listener, void *data);
void view_handle_destroy(struct wl_listener *listener, void *data);
void view_handle_request_move(struct wl_listener *listener, void *data);

struct north_view *view_create_xdg(struct north_server *server, struct wlr_xdg_toplevel *toplevel);
struct north_view *view_create_xwayland(struct north_server *server,
                                        struct wlr_xwayland_surface *xsurface);
void view_destroy(struct north_view *view);

void view_focus(struct north_view *view);
void view_set_position(struct north_view *view, int x, int y);
void view_set_size(struct north_view *view, int width, int height);
void view_set_maximized(struct north_view *view, bool maximized);
void view_set_fullscreen(struct north_view *view, bool fullscreen);
void view_set_snapped(struct north_view *view, enum north_snap_zone zone);
void view_restore(struct north_view *view);
void view_close(struct north_view *view);

struct north_view *view_from_node(struct wlr_scene_node *node);

#endif
