#ifndef NORTH_IPC_H
#define NORTH_IPC_H

#include "ipc/protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct north_server;
struct north_workspace;
struct north_view;
struct north_output;

struct north_ipc_server;

struct north_ipc_client {
    struct wl_list link;
    struct north_ipc_server *ipc;
    int fd;
    struct wl_event_source *event_source;

    uint32_t subscribed_events;

    char *read_buf;
    size_t read_len;
    size_t read_cap;
};

struct north_ipc_server {
    struct north_server *server;
    int socket_fd;
    char *socket_path;
    struct wl_event_source *event_source;
    struct wl_list clients;
};

bool north_ipc_init(struct north_ipc_server *ipc, struct north_server *server);
void north_ipc_finish(struct north_ipc_server *ipc);

void ipc_broadcast_workspace_event(struct north_server *server, struct north_workspace *ws,
                                   const char *change);
void ipc_broadcast_window_event(struct north_server *server, struct north_view *view,
                                const char *change);
void ipc_broadcast_output_event(struct north_server *server, struct north_output *output,
                                const char *change);
void ipc_broadcast_config_event(struct north_server *server, const char *change);

#endif
