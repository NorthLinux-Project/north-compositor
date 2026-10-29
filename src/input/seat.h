#ifndef SEAT_H
#define SEAT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include "util/geom.h"

enum north_cursor_mode {
    NORTH_CURSOR_PASSTHROUGH = 0,
    NORTH_CURSOR_MOVE,
    NORTH_CURSOR_RESIZE
};

struct north_server;
struct north_view;

struct north_seat {
    struct north_server *server;
    struct wlr_seat *wlr_seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;

    enum north_cursor_mode cursor_mode;
    struct north_view *grabbed_view;
    double grab_x;
    double grab_y;
    NorthBox grab_box;
    uint32_t resize_edges;

    uint32_t last_click_time;
    struct north_view *last_click_view;
    uint32_t last_click_part;

    struct wl_list keyboards;
    struct wl_list pointers;

    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_set_cursor;
    struct wl_listener request_set_shape;
};

void seat_init(struct north_seat *seat, struct north_server *server);
void seat_finish(struct north_seat *seat);
void seat_add_device(struct north_seat *seat, struct wlr_input_device *device);
void seat_add_keyboard(struct north_seat *seat, struct wlr_keyboard *wlr_keyboard);
void seat_init_pointer(struct north_seat *seat);

void seat_begin_move(struct north_seat *seat, struct north_view *view);
void seat_begin_resize(struct north_seat *seat, struct north_view *view, uint32_t edges);

#endif
