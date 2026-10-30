#include "core/server.h"
#include "shell/view.h"
#include "util/log.h"

#include <stdlib.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

struct north_keyboard {
    struct wl_list link;
    struct north_server *server;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

static void execute_command(const char *cmd)
{
    if (!cmd || !*cmd)
        return;
    LOG_INFO("Executing command: %s", cmd);
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
}

static bool handle_keybinding(struct north_server *server, uint32_t modifiers, xkb_keysym_t sym)
{
    NorthConfig *cfg = server->config;
    for (size_t i = 0; i < cfg->bind_count; ++i) {
        NorthKeybind *b = &cfg->binds[i];
        if (b->modifiers == modifiers && b->keysym == sym) {
            struct north_view *focused = NULL;
            if (!wl_list_empty(&server->views)) {
                focused = wl_container_of(server->views.next, focused, link);
            }

            switch (b->action) {
            case NORTH_KEYBIND_ACTION_EXEC:
                execute_command(b->argument);
                return true;
            case NORTH_KEYBIND_ACTION_CLOSE:
                if (focused) {
                    view_close(focused);
                }
                return true;
            case NORTH_KEYBIND_ACTION_MAXIMIZE:
                if (focused) {
                    view_set_maximized(focused, focused->state != VIEW_MAXIMIZED);
                }
                return true;
            case NORTH_KEYBIND_ACTION_FULLSCREEN:
                if (focused) {
                    view_set_fullscreen(focused, focused->state != VIEW_FULLSCREEN);
                }
                return true;
            case NORTH_KEYBIND_ACTION_SNAP_LEFT:
                if (focused) {
                    view_set_snapped(focused, SNAP_LEFT);
                }
                return true;
            case NORTH_KEYBIND_ACTION_SNAP_RIGHT:
                if (focused) {
                    view_set_snapped(focused, SNAP_RIGHT);
                }
                return true;
            case NORTH_KEYBIND_ACTION_SNAP_TOP_LEFT:
                if (focused) {
                    view_set_snapped(focused, SNAP_TOP_LEFT);
                }
                return true;
            case NORTH_KEYBIND_ACTION_SNAP_TOP_RIGHT:
                if (focused) {
                    view_set_snapped(focused, SNAP_TOP_RIGHT);
                }
                return true;
            case NORTH_KEYBIND_ACTION_SNAP_BOTTOM_LEFT:
                if (focused) {
                    view_set_snapped(focused, SNAP_BOTTOM_LEFT);
                }
                return true;
            case NORTH_KEYBIND_ACTION_SNAP_BOTTOM_RIGHT:
                if (focused) {
                    view_set_snapped(focused, SNAP_BOTTOM_RIGHT);
                }
                return true;
            case NORTH_KEYBIND_ACTION_RESTORE:
                if (focused) {
                    view_restore(focused);
                }
                return true;
            case NORTH_KEYBIND_ACTION_FOCUS_NEXT:
                if (focused && focused->link.next != &server->views) {
                    struct north_view *next = wl_container_of(focused->link.next, next, link);
                    view_focus(next);
                } else if (!wl_list_empty(&server->views)) {
                    struct north_view *first = wl_container_of(server->views.next, first, link);
                    view_focus(first);
                }
                return true;
            case NORTH_KEYBIND_ACTION_FOCUS_PREV:
                if (focused && focused->link.prev != &server->views) {
                    struct north_view *prev = wl_container_of(focused->link.prev, prev, link);
                    view_focus(prev);
                } else if (!wl_list_empty(&server->views)) {
                    struct north_view *last = wl_container_of(server->views.prev, last, link);
                    view_focus(last);
                }
                return true;
            case NORTH_KEYBIND_ACTION_EXIT:
                LOG_INFO("Exit requested via keybinding");
                wl_display_terminate(server->display);
                return true;
            default:
                break;
            }
        }
    }
    return false;
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_keyboard *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat.wlr_seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat.wlr_seat, &kb->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    struct north_keyboard *kb = wl_container_of(listener, kb, key);
    struct north_server *server = kb->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_seat *seat = server->seat.wlr_seat;

    if (server->idle_notifier) {
        wlr_idle_notifier_v1_notify_activity(server->idle_notifier, seat);
    }

    if (server->session_lock != NULL) {
        wlr_seat_set_keyboard(seat, kb->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
        return;
    }

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(kb->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t wlr_mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);
    uint32_t mods = 0;
    if (wlr_mods & WLR_MODIFIER_SHIFT)
        mods |= NORTH_MOD_SHIFT;
    if (wlr_mods & WLR_MODIFIER_CTRL)
        mods |= NORTH_MOD_CTRL;
    if (wlr_mods & WLR_MODIFIER_ALT)
        mods |= NORTH_MOD_ALT;
    if (wlr_mods & WLR_MODIFIER_LOGO)
        mods |= NORTH_MOD_SUPER;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; ++i) {
            if (mods == NORTH_MOD_SUPER && syms[i] == XKB_KEY_Escape) {
                server_break_pointer_constraint(server);
                handled = true;
                break;
            }
            handled = handle_keybinding(server, mods, syms[i]);
            if (handled)
                break;
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(seat, kb->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_keyboard *kb = wl_container_of(listener, kb, destroy);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->link);
    free(kb);
}

void seat_add_keyboard(struct north_seat *seat, struct wlr_keyboard *wlr_keyboard)
{
    struct north_keyboard *kb = calloc(1, sizeof(*kb));
    if (!kb) {
        LOG_ERROR("Failed allocating north_keyboard");
        return;
    }
    kb->server = seat->server;
    kb->wlr_keyboard = wlr_keyboard;

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names rules = {
        .layout = seat->server->config->keyboard.layout,
        .variant = seat->server->config->keyboard.variant,
        .options = seat->server->config->keyboard.options,
    };
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap) {
        wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        xkb_keymap_unref(keymap);
    }
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(wlr_keyboard, seat->server->config->keyboard.repeat_rate,
                                 seat->server->config->keyboard.repeat_delay);

    kb->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &kb->modifiers);

    kb->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &kb->key);

    kb->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&wlr_keyboard->base.events.destroy, &kb->destroy);

    wlr_seat_set_keyboard(seat->wlr_seat, wlr_keyboard);
    wl_list_insert(&seat->keyboards, &kb->link);
}
