#include "input/seat.h"

#include "core/server.h"
#include "shell/view.h"
#include "util/log.h"

#include <wlr/backend/libinput.h>

void seat_init(struct north_seat *seat, struct north_server *server)
{
    seat->server = server;
    seat->cursor_mode = NORTH_CURSOR_PASSTHROUGH;

    wl_list_init(&seat->keyboards);
    wl_list_init(&seat->pointers);

    seat->wlr_seat = wlr_seat_create(server->display, "seat0");
    if (!seat->wlr_seat) {
        LOG_ERROR("Failed to create wlr_seat");
        return;
    }

    seat->cursor = wlr_cursor_create();
    if (!seat->cursor) {
        LOG_ERROR("Failed to create wlr_cursor");
        return;
    }
    wlr_cursor_attach_output_layout(seat->cursor, server->output_layout);

    const char *xcursor_theme = getenv("XCURSOR_THEME");
    int xcursor_size = 24;
    const char *size_str = getenv("XCURSOR_SIZE");
    if (size_str) {
        int sz = atoi(size_str);
        if (sz > 0)
            xcursor_size = sz;
    }

    seat->cursor_mgr = wlr_xcursor_manager_create(xcursor_theme, xcursor_size);
    if (seat->cursor_mgr) {
        wlr_xcursor_manager_load(seat->cursor_mgr, 1.0f);
    }

    seat_init_pointer(seat);
}

void seat_finish(struct north_seat *seat)
{
    wl_list_remove(&seat->cursor_motion.link);
    wl_list_remove(&seat->cursor_motion_absolute.link);
    wl_list_remove(&seat->cursor_button.link);
    wl_list_remove(&seat->cursor_axis.link);
    wl_list_remove(&seat->cursor_frame.link);

    if (seat->cursor_mgr) {
        wlr_xcursor_manager_destroy(seat->cursor_mgr);
        seat->cursor_mgr = NULL;
    }
    if (seat->cursor) {
        wlr_cursor_destroy(seat->cursor);
        seat->cursor = NULL;
    }
}

void seat_add_device(struct north_seat *seat, struct wlr_input_device *device)
{
    uint32_t caps = 0;
    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
        seat_add_keyboard(seat, kb);
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(seat->cursor, device);
        caps |= WL_SEAT_CAPABILITY_POINTER;

        if (wlr_input_device_is_libinput(device)) {
            struct libinput_device *libinput_dev = wlr_libinput_get_device_handle(device);
            if (libinput_device_config_tap_get_finger_count(libinput_dev) > 0) {
                libinput_device_config_tap_set_enabled(libinput_dev,
                                                       seat->server->config->touchpad.tap
                                                           ? LIBINPUT_CONFIG_TAP_ENABLED
                                                           : LIBINPUT_CONFIG_TAP_DISABLED);
                libinput_device_config_scroll_set_natural_scroll_enabled(
                    libinput_dev, seat->server->config->touchpad.natural_scroll ? 1 : 0);
            } else {
                libinput_device_config_scroll_set_natural_scroll_enabled(
                    libinput_dev, seat->server->config->pointer.natural_scroll ? 1 : 0);
            }
        }
    } else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
        wlr_cursor_attach_input_device(seat->cursor, device);
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    }

    uint32_t current_caps = seat->wlr_seat->capabilities;
    wlr_seat_set_capabilities(seat->wlr_seat, current_caps | caps);
}

void seat_begin_move(struct north_seat *seat, struct north_view *view)
{
    if (!view || view->state == VIEW_FULLSCREEN)
        return;

    seat->cursor_mode = NORTH_CURSOR_MOVE;
    seat->grabbed_view = view;
    seat->grab_x = seat->cursor->x;
    seat->grab_y = seat->cursor->y;
    seat->grab_box = view->geometry;
    seat->snap_suppressed = (view->state == VIEW_MAXIMIZED || view->state == VIEW_SNAPPED);

    wlr_cursor_set_xcursor(seat->cursor, seat->cursor_mgr, "grabbing");
}

void seat_begin_resize(struct north_seat *seat, struct north_view *view, uint32_t edges)
{
    if (!view || view->state == VIEW_FULLSCREEN || view->state == VIEW_MAXIMIZED)
        return;

    seat->cursor_mode = NORTH_CURSOR_RESIZE;
    seat->grabbed_view = view;
    seat->grab_x = seat->cursor->x;
    seat->grab_y = seat->cursor->y;
    seat->grab_box = view->geometry;
    seat->resize_edges = edges;
}
