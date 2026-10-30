#ifndef KEYBIND_H
#define KEYBIND_H

#include "util/arena.h"
#include "util/error.h"

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#define NORTH_MOD_SHIFT (1 << 0)
#define NORTH_MOD_CTRL (1 << 1)
#define NORTH_MOD_ALT (1 << 2)
#define NORTH_MOD_SUPER (1 << 3)

enum north_keybind_action {
    NORTH_KEYBIND_ACTION_NONE = 0,
    NORTH_KEYBIND_ACTION_EXEC,
    NORTH_KEYBIND_ACTION_CLOSE,
    NORTH_KEYBIND_ACTION_MAXIMIZE,
    NORTH_KEYBIND_ACTION_FULLSCREEN,
    NORTH_KEYBIND_ACTION_MINIMIZE,
    NORTH_KEYBIND_ACTION_FOCUS_NEXT,
    NORTH_KEYBIND_ACTION_FOCUS_PREV,
    NORTH_KEYBIND_ACTION_MOVE_TO_OUTPUT,
    NORTH_KEYBIND_ACTION_SNAP_LEFT,
    NORTH_KEYBIND_ACTION_SNAP_RIGHT,
    NORTH_KEYBIND_ACTION_SNAP_TOP_LEFT,
    NORTH_KEYBIND_ACTION_SNAP_TOP_RIGHT,
    NORTH_KEYBIND_ACTION_SNAP_BOTTOM_LEFT,
    NORTH_KEYBIND_ACTION_SNAP_BOTTOM_RIGHT,
    NORTH_KEYBIND_ACTION_RESTORE,
    NORTH_KEYBIND_ACTION_RELOAD,
    NORTH_KEYBIND_ACTION_EXIT
};

typedef struct {
    uint32_t modifiers;
    xkb_keysym_t keysym;
    enum north_keybind_action action;
    const char *argument;
} NorthKeybind;

typedef struct {
    NorthKeybind *binds;
    size_t count;
} NorthKeybindList;

DEFINE_RESULT(NorthKeybind, NorthKeybind)

NorthKeybindResult keybind_parse(Arena *arena, const char *spec, const char *action_str);

#endif
