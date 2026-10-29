#ifndef XDG_H
#define XDG_H

#include <wayland-server-core.h>

void server_handle_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_handle_new_xdg_popup(struct wl_listener *listener, void *data);

#endif
