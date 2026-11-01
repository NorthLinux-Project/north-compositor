#include "core/server.h"
#include "output/output.h"
#include "shell/view.h"
#include "shell/workspace.h"
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

static struct north_output *get_focused_output(struct north_server *server)
{
    struct north_output *out = NULL;
    if (server->seat.cursor) {
        out = output_at(server, server->seat.cursor->x, server->seat.cursor->y);
    }
    if (!out && !wl_list_empty(&server->views)) {
        struct north_view *v;
        wl_list_for_each(v, &server->views, link)
        {
            if (v->workspace && v->workspace->output) {
                out = v->workspace->output;
                break;
            }
        }
    }
    if (!out && !wl_list_empty(&server->outputs)) {
        out = wl_container_of(server->outputs.next, out, link);
    }
    return out;
}

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

static bool handle_keybinding(struct north_server *server, uint32_t modifiers, xkb_keysym_t sym,
                              xkb_keysym_t raw_sym)
{
    NorthConfig *cfg = server->config;
    for (size_t i = 0; i < cfg->bind_count; ++i) {
        NorthKeybind *b = &cfg->binds[i];
        bool sym_match = (b->keysym == sym);
        if (!sym_match) {
            if (xkb_keysym_to_lower(b->keysym) == xkb_keysym_to_lower(sym)) {
                sym_match = true;
            } else if (raw_sym != XKB_KEY_NoSymbol &&
                       (b->keysym == raw_sym ||
                        xkb_keysym_to_lower(b->keysym) == xkb_keysym_to_lower(raw_sym))) {
                sym_match = true;
            } else if (modifiers & NORTH_MOD_SHIFT) {
                if ((b->keysym == XKB_KEY_Tab && sym == XKB_KEY_ISO_Left_Tab) ||
                    (b->keysym == XKB_KEY_ISO_Left_Tab && sym == XKB_KEY_Tab)) {
                    sym_match = true;
                }
            } else if ((b->keysym == XKB_KEY_Return && sym == XKB_KEY_KP_Enter) ||
                       (b->keysym == XKB_KEY_KP_Enter && sym == XKB_KEY_Return)) {
                sym_match = true;
            }
        }
        if (b->modifiers == modifiers && sym_match) {
            struct north_view *focused = NULL;
            struct wlr_surface *focused_surface =
                server->seat.wlr_seat->keyboard_state.focused_surface;
            if (focused_surface) {
                struct north_view *v;
                wl_list_for_each(v, &server->views, link)
                {
                    if (v->surface == focused_surface && workspace_view_is_visible(v)) {
                        focused = v;
                        break;
                    }
                }
            }
            if (!focused) {
                struct north_view *v;
                wl_list_for_each(v, &server->views, link)
                {
                    if (workspace_view_is_visible(v)) {
                        focused = v;
                        break;
                    }
                }
            }

            switch (b->action) {
            case NORTH_KEYBIND_ACTION_EXEC:
                execute_command(b->argument);
                return true;
            case NORTH_KEYBIND_ACTION_CLOSE:
                if (focused) {
                    LOG_INFO("Closing focused window (view=%p)", (void *)focused);
                    view_close(focused);
                } else {
                    LOG_INFO("Super+Q (close) pressed, but no window is open or focused");
                }
                return true;
            case NORTH_KEYBIND_ACTION_MAXIMIZE:
                if (focused) {
                    view_set_maximized(focused, focused->state != VIEW_MAXIMIZED);
                }
                return true;
            case NORTH_KEYBIND_ACTION_FULLSCREEN:
                if (focused) {
                    LOG_INFO("Toggling fullscreen on window (view=%p)", (void *)focused);
                    view_set_fullscreen(focused, focused->state != VIEW_FULLSCREEN);
                } else {
                    LOG_INFO("Super+F (fullscreen) pressed, but no window is open or focused");
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
                server->switcher.trigger_mods = modifiers;
                if (server->switcher.active) {
                    switcher_cycle(server, 1);
                } else {
                    switcher_show(server, 1);
                }
                return true;
            case NORTH_KEYBIND_ACTION_FOCUS_PREV:
                server->switcher.trigger_mods = modifiers;
                if (server->switcher.active) {
                    switcher_cycle(server, -1);
                } else {
                    switcher_show(server, -1);
                }
                return true;
            case NORTH_KEYBIND_ACTION_WORKSPACE: {
                if (!b->argument) {
                    return true;
                }
                int target_idx = atoi(b->argument) - 1;
                if (target_idx < 0) {
                    return true;
                }
                struct north_output *out = get_focused_output(server);
                if (out) {
                    workspace_switch(out, target_idx);
                }
                return true;
            }
            case NORTH_KEYBIND_ACTION_MOVE_TO_WORKSPACE: {
                if (!b->argument) {
                    return true;
                }
                int target_idx = atoi(b->argument) - 1;
                if (target_idx < 0) {
                    return true;
                }
                if (focused) {
                    workspace_move_view(focused, target_idx);
                }
                return true;
            }
            case NORTH_KEYBIND_ACTION_WORKSPACE_NEXT: {
                struct north_output *out = get_focused_output(server);
                if (out) {
                    workspace_cycle(out, 1);
                }
                return true;
            }
            case NORTH_KEYBIND_ACTION_WORKSPACE_PREV: {
                struct north_output *out = get_focused_output(server);
                if (out) {
                    workspace_cycle(out, -1);
                }
                return true;
            }
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

    if (kb->server->switcher.active) {
        uint32_t wlr_mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);
        uint32_t trigger = kb->server->switcher.trigger_mods;
        if (!trigger) {
            trigger = NORTH_MOD_ALT;
        }
        bool held = false;
        if ((trigger & NORTH_MOD_ALT) && (wlr_mods & WLR_MODIFIER_ALT)) {
            held = true;
        }
        if ((trigger & NORTH_MOD_SUPER) && (wlr_mods & WLR_MODIFIER_LOGO)) {
            held = true;
        }
        if ((trigger & NORTH_MOD_CTRL) && (wlr_mods & WLR_MODIFIER_CTRL)) {
            held = true;
        }
        if (!held) {
            switcher_commit(kb->server);
        }
    }
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

    xkb_keysym_t raw_sym = XKB_KEY_NoSymbol;
    if (kb->wlr_keyboard->keymap) {
        xkb_layout_index_t layout = xkb_state_key_get_layout(kb->wlr_keyboard->xkb_state, keycode);
        const xkb_keysym_t *raw_syms = NULL;
        int nraw = xkb_keymap_key_get_syms_by_level(kb->wlr_keyboard->keymap, keycode, layout, 0,
                                                    &raw_syms);
        if (nraw > 0 && raw_syms) {
            raw_sym = raw_syms[0];
        }
    }

    if (server->switcher.active) {
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            for (int i = 0; i < nsyms; ++i) {
                if (syms[i] == XKB_KEY_Escape) {
                    server->switcher_escape_pending = true;
                    switcher_cancel(server);
                    return;
                }
            }
        }
    }

    if (server->switcher_escape_pending && event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        for (int i = 0; i < nsyms; ++i) {
            if (syms[i] == XKB_KEY_Escape) {
                server->switcher_escape_pending = false;
                return;
            }
        }
    }

    if (server->switcher_just_committed && event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        for (int i = 0; i < nsyms; ++i) {
            if (syms[i] == XKB_KEY_Alt_L || syms[i] == XKB_KEY_Alt_R || syms[i] == XKB_KEY_Meta_L ||
                syms[i] == XKB_KEY_Meta_R || syms[i] == XKB_KEY_Super_L ||
                syms[i] == XKB_KEY_Super_R) {
                server->switcher_just_committed = false;
                return;
            }
        }
    }

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
            unsigned vt = 0;
            if (syms[i] >= XKB_KEY_XF86Switch_VT_1 && syms[i] <= XKB_KEY_XF86Switch_VT_12) {
                vt = (unsigned)(syms[i] - XKB_KEY_XF86Switch_VT_1 + 1);
            } else if ((mods & (NORTH_MOD_CTRL | NORTH_MOD_ALT)) ==
                       (NORTH_MOD_CTRL | NORTH_MOD_ALT)) {
                if (syms[i] >= XKB_KEY_F1 && syms[i] <= XKB_KEY_F12) {
                    vt = (unsigned)(syms[i] - XKB_KEY_F1 + 1);
                } else if (raw_sym >= XKB_KEY_F1 && raw_sym <= XKB_KEY_F12) {
                    vt = (unsigned)(raw_sym - XKB_KEY_F1 + 1);
                }
            }

            if (vt > 0) {
                if (server->session) {
                    LOG_INFO("Switching to VT %u", vt);
                    wlr_session_change_vt(server->session, vt);
                } else {
                    LOG_DEBUG("VT switch to %u requested, but not running with session", vt);
                }
                return;
            }

            if (mods == NORTH_MOD_SUPER && syms[i] == XKB_KEY_Escape) {
                server_break_pointer_constraint(server);
                handled = true;
                break;
            }
            handled = handle_keybinding(server, mods, syms[i], raw_sym);
            if (handled)
                break;
        }
    }

    if (server->switcher.active) {
        handled = true;
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
