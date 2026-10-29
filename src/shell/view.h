#ifndef VIEW_H
#define VIEW_H

#include <stdbool.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include "decor/decoration.h"
#include "util/geom.h"

enum north_view_state {
    VIEW_NORMAL = 0,
    VIEW_MAXIMIZED,
    VIEW_FULLSCREEN,
    VIEW_MINIMIZED
};

struct north_server;

struct north_view {
    struct wl_list link;
    struct north_server *server;
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *tree;
    struct wlr_scene_tree *surface_tree;
    struct north_decoration decor;

    enum north_view_state state;
    NorthBox geometry;
    NorthBox saved_geometry;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;

    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_minimize;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
};

struct north_view *view_create(struct north_server *server, struct wlr_xdg_toplevel *toplevel);
void view_destroy(struct north_view *view);

void view_focus(struct north_view *view);
void view_set_position(struct north_view *view, int x, int y);
void view_set_size(struct north_view *view, int width, int height);
void view_set_maximized(struct north_view *view, bool maximized);
void view_set_fullscreen(struct north_view *view, bool fullscreen);

struct north_view *view_from_node(struct wlr_scene_node *node);

#endif
