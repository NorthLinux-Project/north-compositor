#include "input/keybind.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

static const char *skip_whitespace(const char *s)
{
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r')) {
        s++;
    }
    return s;
}

static char *arena_strdup(Arena *arena, const char *src)
{
    if (!src)
        return NULL;
    size_t len = strlen(src);
    char *dst = (char *)arena_alloc(arena, len + 1, sizeof(char));
    if (!dst)
        return NULL;
    memcpy(dst, src, len + 1);
    return dst;
}

NorthKeybindResult keybind_parse(Arena *arena, const char *spec, const char *action_str)
{
    if (!spec || !action_str) {
        return RESULT_ERR(NorthKeybind,
                          ERROR_CREATE(ERR_INVALID_ARGUMENT, "NULL spec or action_str"));
    }

    NorthKeybind bind = {0};
    const char *p = spec;

    while (*p) {
        p = skip_whitespace(p);
        const char *token_start = p;
        while (*p && *p != '+' && *p != ' ' && *p != '\t') {
            p++;
        }
        size_t token_len = (size_t)(p - token_start);
        p = skip_whitespace(p);

        bool is_last = (*p != '+');
        if (*p == '+') {
            p++;
        }

        if (token_len == 0) {
            continue;
        }

        if (!is_last) {
            if (strncasecmp(token_start, "Super", token_len) == 0 ||
                strncasecmp(token_start, "Mod4", token_len) == 0 ||
                strncasecmp(token_start, "Logo", token_len) == 0) {
                bind.modifiers |= NORTH_MOD_SUPER;
            } else if (strncasecmp(token_start, "Shift", token_len) == 0) {
                bind.modifiers |= NORTH_MOD_SHIFT;
            } else if (strncasecmp(token_start, "Ctrl", token_len) == 0 ||
                       strncasecmp(token_start, "Control", token_len) == 0) {
                bind.modifiers |= NORTH_MOD_CTRL;
            } else if (strncasecmp(token_start, "Alt", token_len) == 0 ||
                       strncasecmp(token_start, "Mod1", token_len) == 0) {
                bind.modifiers |= NORTH_MOD_ALT;
            } else {
                return RESULT_ERR(NorthKeybind, ERROR_CREATE(ERR_INVALID_ARGUMENT,
                                                             "unknown modifier in keybind"));
            }
        } else {
            char name_buf[64];
            if (token_len >= sizeof(name_buf)) {
                return RESULT_ERR(NorthKeybind,
                                  ERROR_CREATE(ERR_INVALID_ARGUMENT, "keysym name too long"));
            }
            memcpy(name_buf, token_start, token_len);
            name_buf[token_len] = '\0';

            xkb_keysym_t sym = xkb_keysym_from_name(name_buf, XKB_KEYSYM_CASE_INSENSITIVE);
            if (sym == XKB_KEY_NoSymbol) {
                return RESULT_ERR(NorthKeybind,
                                  ERROR_CREATE(ERR_NOT_FOUND, "unrecognized keysym name"));
            }
            bind.keysym = sym;
            break;
        }
    }

    if (bind.keysym == XKB_KEY_NoSymbol) {
        return RESULT_ERR(NorthKeybind, ERROR_CREATE(ERR_INVALID_ARGUMENT, "no keysym specified"));
    }

    const char *act = skip_whitespace(action_str);
    if (strncmp(act, "exec", 4) == 0 && (act[4] == ' ' || act[4] == '\t' || act[4] == '\0')) {
        bind.action = NORTH_KEYBIND_ACTION_EXEC;
        const char *arg = skip_whitespace(act + 4);
        bind.argument = arena_strdup(arena, arg);
    } else if (strcasecmp(act, "close") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_CLOSE;
    } else if (strcasecmp(act, "maximize") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_MAXIMIZE;
    } else if (strcasecmp(act, "fullscreen") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_FULLSCREEN;
    } else if (strcasecmp(act, "minimize") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_MINIMIZE;
    } else if (strcasecmp(act, "snap_left") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_SNAP_LEFT;
    } else if (strcasecmp(act, "snap_right") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_SNAP_RIGHT;
    } else if (strcasecmp(act, "snap_top") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_MAXIMIZE;
    } else if (strcasecmp(act, "snap_top_left") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_SNAP_TOP_LEFT;
    } else if (strcasecmp(act, "snap_top_right") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_SNAP_TOP_RIGHT;
    } else if (strcasecmp(act, "snap_bottom_left") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_SNAP_BOTTOM_LEFT;
    } else if (strcasecmp(act, "snap_bottom_right") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_SNAP_BOTTOM_RIGHT;
    } else if (strcasecmp(act, "restore") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_RESTORE;
    } else if (strcasecmp(act, "focus_next") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_FOCUS_NEXT;
    } else if (strcasecmp(act, "focus_prev") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_FOCUS_PREV;
    } else if (strcasecmp(act, "reload") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_RELOAD;
    } else if (strcasecmp(act, "exit") == 0) {
        bind.action = NORTH_KEYBIND_ACTION_EXIT;
    } else {
        return RESULT_ERR(NorthKeybind,
                          ERROR_CREATE(ERR_INVALID_ARGUMENT, "unknown keybind action"));
    }

    return RESULT_OK(NorthKeybind, bind);
}
