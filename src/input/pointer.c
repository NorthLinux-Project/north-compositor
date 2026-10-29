#include "input/seat.h"
#include "core/server.h"
#include "shell/view.h"
#include "shell/placement.h"
#include "output/output.h"
#include "util/log.h"
#include <wlr/util/edges.h>
#include <linux/input-event-codes.h>

static void process_cursor_motion(struct north_seat *seat, uint32_t time_msec) {
    struct north_server *server = seat->server;
    double cx = seat->cursor->x;
    double cy = seat->cursor->y;

    if (seat->cursor_mode == NORTH_CURSOR_MOVE) {
        if (!seat->grabbed_view) {
            seat->cursor_mode = NORTH_CURSOR_PASSTHROUGH;
            return;
        }
        int new_x = seat->grab_box.x + (int)(cx - seat->grab_x);
        int new_y = seat->grab_box.y + (int)(cy - seat->grab_y);

        struct north_output *out = output_at(server, cx, cy);
        NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0};

        NorthBox moving = { .x = new_x, .y = new_y, .width = seat->grab_box.width, .height = seat->grab_box.height };
        NorthBox snapped = placement_snap(moving, usable, NULL, 0, 16);

        view_set_position(seat->grabbed_view, snapped.x, snapped.y);
        return;
    }

    if (seat->cursor_mode == NORTH_CURSOR_RESIZE) {
        if (!seat->grabbed_view) {
            seat->cursor_mode = NORTH_CURSOR_PASSTHROUGH;
            return;
        }

        int dx = (int)(cx - seat->grab_x);
        int dy = (int)(cy - seat->grab_y);

        NorthBox box = seat->grab_box;

        if (seat->resize_edges & WLR_EDGE_TOP) {
            box.y += dy;
            box.height -= dy;
        }
        if (seat->resize_edges & WLR_EDGE_BOTTOM) {
            box.height += dy;
        }
        if (seat->resize_edges & WLR_EDGE_LEFT) {
            box.x += dx;
            box.width -= dx;
        }
        if (seat->resize_edges & WLR_EDGE_RIGHT) {
            box.width += dx;
        }

        if (box.width < 100) box.width = 100;
        if (box.height < 60) box.height = 60;

        view_set_position(seat->grabbed_view, box.x, box.y);
        view_set_size(seat->grabbed_view, box.width, box.height);
        return;
    }

    double sx, sy;
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, cx, cy, &sx, &sy);

    if (!node) {
        wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "default");
        wlr_seat_pointer_clear_focus(seat->wlr_seat);
        return;
    }

    struct north_view *view = view_from_node(node);
    if (view && node->parent == view->decor.tree) {
        enum north_decor_part part = decoration_find_part(&view->decor, node, sx, sy);
        switch (part) {
            case NORTH_DECOR_PART_BORDER_TOP:
                wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "n-resize");
                break;
            case NORTH_DECOR_PART_BORDER_BOTTOM:
                wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "s-resize");
                break;
            case NORTH_DECOR_PART_BORDER_LEFT:
                wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "w-resize");
                break;
            case NORTH_DECOR_PART_BORDER_RIGHT:
                wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "e-resize");
                break;
            default:
                wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "default");
                break;
        }
        wlr_seat_pointer_clear_focus(seat->wlr_seat);
        return;
    }

    struct wlr_scene_buffer *scene_buf = wlr_scene_buffer_from_node(node);
    if (scene_buf) {
        struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buf);
        if (scene_surface) {
            wlr_seat_pointer_notify_enter(seat->wlr_seat, scene_surface->surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
            return;
        }
    }

    wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "default");
    wlr_seat_pointer_clear_focus(seat->wlr_seat);
}

static void cursor_handle_motion(struct wl_listener *listener, void *data) {
    struct north_seat *seat = wl_container_of(listener, seat, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(seat->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(seat, event->time_msec);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener, void *data) {
    struct north_seat *seat = wl_container_of(listener, seat, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(seat->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(seat, event->time_msec);
}

static void cursor_handle_button(struct wl_listener *listener, void *data) {
    struct north_seat *seat = wl_container_of(listener, seat, cursor_button);
    struct north_server *server = seat->server;
    struct wlr_pointer_button_event *event = data;

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (seat->cursor_mode != NORTH_CURSOR_PASSTHROUGH) {
            seat->cursor_mode = NORTH_CURSOR_PASSTHROUGH;
            seat->grabbed_view = NULL;
            wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "default");
            return;
        }
        wlr_seat_pointer_notify_button(seat->wlr_seat, event->time_msec, event->button, event->state);
        return;
    }

    double sx, sy;
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
                                                    seat->cursor->x, seat->cursor->y, &sx, &sy);
    struct north_view *view = view_from_node(node);

    if (view) {
        view_focus(view);

        uint32_t wlr_mods = 0;
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat->wlr_seat);
        if (kb) {
            wlr_mods = wlr_keyboard_get_modifiers(kb);
        }

        if (wlr_mods & WLR_MODIFIER_LOGO) {
            if (event->button == BTN_LEFT) {
                seat_begin_move(seat, view);
                return;
            } else if (event->button == BTN_RIGHT) {
                seat_begin_resize(seat, view, WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
                return;
            }
        }

        if (node && node->parent == view->decor.tree) {
            enum north_decor_part part = decoration_find_part(&view->decor, node, sx, sy);
            if (event->button == BTN_LEFT) {
                bool is_double_click = (seat->last_click_view == view &&
                                        seat->last_click_part == (uint32_t)part &&
                                        event->time_msec - seat->last_click_time < 350);
                seat->last_click_time = event->time_msec;
                seat->last_click_view = view;
                seat->last_click_part = (uint32_t)part;

                if (part == NORTH_DECOR_PART_TITLEBAR && is_double_click) {
                    view_set_maximized(view, view->state != VIEW_MAXIMIZED);
                    return;
                }

                switch (part) {
                    case NORTH_DECOR_PART_BUTTON_CLOSE:
                        wlr_xdg_toplevel_send_close(view->toplevel);
                        return;
                    case NORTH_DECOR_PART_BUTTON_MAXIMIZE:
                        view_set_maximized(view, view->state != VIEW_MAXIMIZED);
                        return;
                    case NORTH_DECOR_PART_BUTTON_MINIMIZE:
                        view_set_maximized(view, false);
                        return;
                    case NORTH_DECOR_PART_TITLEBAR:
                        seat_begin_move(seat, view);
                        return;
                    case NORTH_DECOR_PART_BORDER_TOP:
                        seat_begin_resize(seat, view, WLR_EDGE_TOP);
                        return;
                    case NORTH_DECOR_PART_BORDER_BOTTOM:
                        seat_begin_resize(seat, view, WLR_EDGE_BOTTOM);
                        return;
                    case NORTH_DECOR_PART_BORDER_LEFT:
                        seat_begin_resize(seat, view, WLR_EDGE_LEFT);
                        return;
                    case NORTH_DECOR_PART_BORDER_RIGHT:
                        seat_begin_resize(seat, view, WLR_EDGE_RIGHT);
                        return;
                    default:
                        break;
                }
            }
            return;
        }
    }

    wlr_seat_pointer_notify_button(seat->wlr_seat, event->time_msec, event->button, event->state);
}

static void cursor_handle_axis(struct wl_listener *listener, void *data) {
    struct north_seat *seat = wl_container_of(listener, seat, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(seat->wlr_seat, event->time_msec, event->orientation,
                                 event->delta, event->delta_discrete, event->source,
                                 event->relative_direction);
}

static void cursor_handle_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_seat *seat = wl_container_of(listener, seat, cursor_frame);
    wlr_seat_pointer_notify_frame(seat->wlr_seat);
}

void seat_init_pointer(struct north_seat *seat) {
    seat->cursor_motion.notify = cursor_handle_motion;
    wl_signal_add(&seat->cursor->events.motion, &seat->cursor_motion);

    seat->cursor_motion_absolute.notify = cursor_handle_motion_absolute;
    wl_signal_add(&seat->cursor->events.motion_absolute, &seat->cursor_motion_absolute);

    seat->cursor_button.notify = cursor_handle_button;
    wl_signal_add(&seat->cursor->events.button, &seat->cursor_button);

    seat->cursor_axis.notify = cursor_handle_axis;
    wl_signal_add(&seat->cursor->events.axis, &seat->cursor_axis);

    seat->cursor_frame.notify = cursor_handle_frame;
    wl_signal_add(&seat->cursor->events.frame, &seat->cursor_frame);
}
