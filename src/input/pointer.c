#include "core/server.h"
#include "input/seat.h"
#include "output/output.h"
#include "shell/placement.h"
#include "shell/view.h"
#include "util/log.h"

#include <linux/input-event-codes.h>
#include <wlr/util/edges.h>

static void process_cursor_motion(struct north_seat *seat, uint32_t time_msec)
{
    struct north_server *server = seat->server;
    double cx = seat->cursor->x;
    double cy = seat->cursor->y;

    if (seat->server->idle_notifier) {
        wlr_idle_notifier_v1_notify_activity(seat->server->idle_notifier, seat->wlr_seat);
    }

    if (seat->cursor_mode == NORTH_CURSOR_MOVE) {
        if (!seat->grabbed_view) {
            seat->cursor_mode = NORTH_CURSOR_PASSTHROUGH;
            return;
        }

        if (seat->grabbed_view->state == VIEW_SNAPPED ||
            seat->grabbed_view->state == VIEW_MAXIMIZED) {
            double dx = cx - seat->grab_x;
            double dy = cy - seat->grab_y;
            if (dx * dx + dy * dy > 225.0) {
                NorthBox saved = seat->grabbed_view->saved_geometry;
                view_restore(seat->grabbed_view);
                int tb_h = seat->grabbed_view->server->config->decor.titlebar_height;
                seat->grab_box = (NorthBox){.x = (int)(cx - saved.width / 2),
                                            .y = (int)(cy + tb_h / 2),
                                            .width = saved.width,
                                            .height = saved.height};
                seat->grab_x = cx;
                seat->grab_y = cy;
            } else {
                return;
            }
        }

        int new_x = seat->grab_box.x + (int)(cx - seat->grab_x);
        int new_y = seat->grab_box.y + (int)(cy - seat->grab_y);

        struct north_output *out = output_at(server, cx, cy);
        NorthBox usable = out ? output_get_usable_box(out) : (NorthBox){0};

        NorthBox moving = {
            .x = new_x, .y = new_y, .width = seat->grab_box.width, .height = seat->grab_box.height};
        NorthBox snapped = placement_snap(moving, usable, NULL, 0, 16);

        view_set_position(seat->grabbed_view, snapped.x, snapped.y);

        if (server->config->snap.enabled && usable.width > 0 && usable.height > 0) {
            enum north_snap_zone zone =
                placement_detect_snap_zone(usable, cx, cy, server->config->snap.threshold);
            if (seat->snap_suppressed) {
                if (zone == SNAP_NONE) {
                    seat->snap_suppressed = false;
                } else {
                    zone = SNAP_NONE;
                }
            }

            server->active_snap_zone = zone;
            if (zone != SNAP_NONE) {
                NorthBox pbox = placement_calculate_snap_box(usable, zone, 0);
                int bw = server->config->snap.preview_border_width;

                wlr_scene_node_set_position(&server->snap_preview_tree->node, pbox.x, pbox.y);
                wlr_scene_rect_set_size(server->snap_preview_fill, pbox.width, pbox.height);

                wlr_scene_node_set_position(&server->snap_preview_borders[0]->node, 0, 0);
                wlr_scene_rect_set_size(server->snap_preview_borders[0], pbox.width, bw);

                wlr_scene_node_set_position(&server->snap_preview_borders[1]->node, 0,
                                            pbox.height - bw);
                wlr_scene_rect_set_size(server->snap_preview_borders[1], pbox.width, bw);

                wlr_scene_node_set_position(&server->snap_preview_borders[2]->node, 0, 0);
                wlr_scene_rect_set_size(server->snap_preview_borders[2], bw, pbox.height);

                wlr_scene_node_set_position(&server->snap_preview_borders[3]->node, pbox.width - bw,
                                            0);
                wlr_scene_rect_set_size(server->snap_preview_borders[3], bw, pbox.height);

                wlr_scene_node_set_enabled(&server->snap_preview_tree->node, true);
            } else {
                wlr_scene_node_set_enabled(&server->snap_preview_tree->node, false);
            }
        } else {
            server->active_snap_zone = SNAP_NONE;
            wlr_scene_node_set_enabled(&server->snap_preview_tree->node, false);
        }
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

        if (box.width < 100)
            box.width = 100;
        if (box.height < 60)
            box.height = 60;

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
        server_set_active_constraint(server, NULL);
        return;
    }

    struct wlr_scene_buffer *scene_buf = wlr_scene_buffer_from_node(node);
    if (scene_buf) {
        struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buf);
        if (scene_surface) {
            wlr_seat_pointer_notify_enter(seat->wlr_seat, scene_surface->surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
            if (server->pointer_constraints) {
                struct wlr_pointer_constraint_v1 *constraint =
                    wlr_pointer_constraints_v1_constraint_for_surface(
                        server->pointer_constraints, scene_surface->surface, seat->wlr_seat);
                server_set_active_constraint(server, constraint);
            }
            return;
        }
    }

    server_set_active_constraint(server, NULL);
    wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "default");
    wlr_seat_pointer_clear_focus(seat->wlr_seat);
}

static void cursor_handle_motion(struct wl_listener *listener, void *data)
{
    struct north_seat *seat = wl_container_of(listener, seat, cursor_motion);
    struct north_server *server = seat->server;
    struct wlr_pointer_motion_event *event = data;

    if (server->relative_pointer_manager) {
        wlr_relative_pointer_manager_v1_send_relative_motion(
            server->relative_pointer_manager, seat->wlr_seat, (uint64_t)event->time_msec * 1000,
            event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);
    }

    if (server->active_constraint &&
        server->active_constraint->surface == seat->wlr_seat->pointer_state.focused_surface) {
        if (server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
            process_cursor_motion(seat, event->time_msec);
            return;
        }
    }

    wlr_cursor_move(seat->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(seat, event->time_msec);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener, void *data)
{
    struct north_seat *seat = wl_container_of(listener, seat, cursor_motion_absolute);
    struct north_server *server = seat->server;
    struct wlr_pointer_motion_absolute_event *event = data;

    if (server->active_constraint &&
        server->active_constraint->surface == seat->wlr_seat->pointer_state.focused_surface) {
        if (server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
            process_cursor_motion(seat, event->time_msec);
            return;
        }
    }

    wlr_cursor_warp_absolute(seat->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(seat, event->time_msec);
}

static void cursor_handle_button(struct wl_listener *listener, void *data)
{
    struct north_seat *seat = wl_container_of(listener, seat, cursor_button);
    struct north_server *server = seat->server;
    struct wlr_pointer_button_event *event = data;

    if (server->idle_notifier) {
        wlr_idle_notifier_v1_notify_activity(server->idle_notifier, seat->wlr_seat);
    }

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (seat->cursor_mode != NORTH_CURSOR_PASSTHROUGH) {
            if (seat->cursor_mode == NORTH_CURSOR_MOVE && seat->grabbed_view &&
                server->active_snap_zone != SNAP_NONE) {
                view_set_snapped(seat->grabbed_view, server->active_snap_zone);
            }
            server->active_snap_zone = SNAP_NONE;
            seat->snap_suppressed = false;
            if (server->snap_preview_tree) {
                wlr_scene_node_set_enabled(&server->snap_preview_tree->node, false);
            }
            seat->cursor_mode = NORTH_CURSOR_PASSTHROUGH;
            seat->grabbed_view = NULL;
            wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "default");
            return;
        }
        wlr_seat_pointer_notify_button(seat->wlr_seat, event->time_msec, event->button,
                                       event->state);
        return;
    }

    if (server->session_lock != NULL) {
        double sx, sy;
        struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, seat->cursor->x,
                                                        seat->cursor->y, &sx, &sy);
        if (node) {
            struct wlr_scene_buffer *scene_buf = wlr_scene_buffer_from_node(node);
            if (scene_buf) {
                struct wlr_scene_surface *scene_surface =
                    wlr_scene_surface_try_from_buffer(scene_buf);
                if (scene_surface) {
                    wlr_seat_keyboard_notify_enter(seat->wlr_seat, scene_surface->surface, NULL, 0,
                                                   NULL);
                }
            }
        }
        wlr_seat_pointer_notify_button(seat->wlr_seat, event->time_msec, event->button,
                                       event->state);
        return;
    }

    double sx, sy;
    struct wlr_scene_node *node =
        wlr_scene_node_at(&server->scene->tree.node, seat->cursor->x, seat->cursor->y, &sx, &sy);
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
                bool is_double_click =
                    (seat->last_click_view == view && seat->last_click_part == (uint32_t)part &&
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
                    view_close(view);
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
    } else if (node) {
        struct wlr_scene_buffer *scene_buf = wlr_scene_buffer_from_node(node);
        if (scene_buf) {
            struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buf);
            if (scene_surface) {
                struct wlr_layer_surface_v1 *layer_surface =
                    wlr_layer_surface_v1_try_from_wlr_surface(scene_surface->surface);
                if (layer_surface && layer_surface->current.keyboard_interactive !=
                                         ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
                    wlr_seat_keyboard_notify_enter(seat->wlr_seat, scene_surface->surface, NULL, 0,
                                                   NULL);
                }
            }
        }
    }

    wlr_seat_pointer_notify_button(seat->wlr_seat, event->time_msec, event->button, event->state);
}

static void cursor_handle_axis(struct wl_listener *listener, void *data)
{
    struct north_seat *seat = wl_container_of(listener, seat, cursor_axis);
    if (seat->server->idle_notifier) {
        wlr_idle_notifier_v1_notify_activity(seat->server->idle_notifier, seat->wlr_seat);
    }
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(seat->wlr_seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

static void cursor_handle_frame(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_seat *seat = wl_container_of(listener, seat, cursor_frame);
    wlr_seat_pointer_notify_frame(seat->wlr_seat);
}

void seat_init_pointer(struct north_seat *seat)
{
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
