#ifndef NORTH_XWAYLAND_H
#define NORTH_XWAYLAND_H

#include <stdbool.h>
#include <wlr/xwayland/xwayland.h>

struct north_server;

void xwayland_init(struct north_server *server);
void xwayland_finish(struct north_server *server);

#endif
