#ifndef XDG_H
#define XDG_H

#include <wayland-server-core.h>

struct north_server;
struct wlr_xdg_popup;
struct wlr_scene_tree;

void server_handle_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_handle_new_xdg_popup(struct wl_listener *listener, void *data);
void popup_create(struct north_server *server, struct wlr_xdg_popup *popup,
                  struct wlr_scene_tree *fallback_tree);

#endif
