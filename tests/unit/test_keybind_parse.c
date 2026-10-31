#include "input/keybind.h"
#include "unity.h"

#include <stdlib.h>

static Arena test_arena;

void setUp(void) { test_arena = arena_create(1024 * 32); }

void tearDown(void) { arena_destroy(&test_arena); }

void test_keybind_parse_modifiers_and_keysym(void)
{
    NorthKeybindResult r1 = keybind_parse(&test_arena, "Super+Return", "exec foot");
    TEST_ASSERT_TRUE(r1.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER, r1.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_Return, r1.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_EXEC, r1.value.action);
    TEST_ASSERT_EQUAL_STRING("foot", r1.value.argument);

    NorthKeybindResult r2 = keybind_parse(&test_arena, "Super+Shift+Q", "close");
    TEST_ASSERT_TRUE(r2.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER | NORTH_MOD_SHIFT, r2.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_q, r2.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_CLOSE, r2.value.action);

    NorthKeybindResult r3 = keybind_parse(&test_arena, "Alt+Tab", "focus_next");
    TEST_ASSERT_TRUE(r3.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_ALT, r3.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_Tab, r3.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_FOCUS_NEXT, r3.value.action);
}

void test_keybind_parse_invalid(void)
{
    NorthKeybindResult r1 = keybind_parse(&test_arena, "UnknownMod+Q", "close");
    TEST_ASSERT_FALSE(r1.ok);

    NorthKeybindResult r2 = keybind_parse(&test_arena, "Super+UnknownKeySymbol12345", "close");
    TEST_ASSERT_FALSE(r2.ok);

    NorthKeybindResult r3 = keybind_parse(&test_arena, "Super+Q", "invalid_action");
    TEST_ASSERT_FALSE(r3.ok);
}

void test_keybind_parse_workspace_switch(void)
{
    NorthKeybindResult r1 = keybind_parse(&test_arena, "Super+1", "workspace 1");
    TEST_ASSERT_TRUE(r1.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER, r1.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_1, r1.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_WORKSPACE, r1.value.action);
    TEST_ASSERT_NOT_NULL(r1.value.argument);
    TEST_ASSERT_EQUAL_STRING("1", r1.value.argument);
    TEST_ASSERT_EQUAL_INT(1, atoi(r1.value.argument));

    NorthKeybindResult r2 = keybind_parse(&test_arena, "Super+9", "workspace 9");
    TEST_ASSERT_TRUE(r2.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER, r2.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_9, r2.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_WORKSPACE, r2.value.action);
    TEST_ASSERT_NOT_NULL(r2.value.argument);
    TEST_ASSERT_EQUAL_STRING("9", r2.value.argument);
    TEST_ASSERT_EQUAL_INT(9, atoi(r2.value.argument));
}

void test_keybind_parse_move_to_workspace(void)
{
    NorthKeybindResult r1 = keybind_parse(&test_arena, "Super+Shift+1", "move_to_workspace 1");
    TEST_ASSERT_TRUE(r1.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER | NORTH_MOD_SHIFT, r1.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_1, r1.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_MOVE_TO_WORKSPACE, r1.value.action);
    TEST_ASSERT_NOT_NULL(r1.value.argument);
    TEST_ASSERT_EQUAL_STRING("1", r1.value.argument);
    TEST_ASSERT_EQUAL_INT(1, atoi(r1.value.argument));

    NorthKeybindResult r2 = keybind_parse(&test_arena, "Super+Shift+9", "move_to_workspace 9");
    TEST_ASSERT_TRUE(r2.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER | NORTH_MOD_SHIFT, r2.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_9, r2.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_MOVE_TO_WORKSPACE, r2.value.action);
    TEST_ASSERT_NOT_NULL(r2.value.argument);
    TEST_ASSERT_EQUAL_STRING("9", r2.value.argument);
    TEST_ASSERT_EQUAL_INT(9, atoi(r2.value.argument));
}

void test_keybind_parse_workspace_cycling(void)
{
    NorthKeybindResult r1 = keybind_parse(&test_arena, "Super+Ctrl+Left", "workspace_prev");
    TEST_ASSERT_TRUE(r1.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER | NORTH_MOD_CTRL, r1.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_Left, r1.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_WORKSPACE_PREV, r1.value.action);
    TEST_ASSERT_NULL(r1.value.argument);

    NorthKeybindResult r2 = keybind_parse(&test_arena, "Super+Ctrl+Right", "workspace_next");
    TEST_ASSERT_TRUE(r2.ok);
    TEST_ASSERT_EQUAL_UINT32(NORTH_MOD_SUPER | NORTH_MOD_CTRL, r2.value.modifiers);
    TEST_ASSERT_EQUAL_UINT32(XKB_KEY_Right, r2.value.keysym);
    TEST_ASSERT_EQUAL_INT(NORTH_KEYBIND_ACTION_WORKSPACE_NEXT, r2.value.action);
    TEST_ASSERT_NULL(r2.value.argument);
}

void test_keybind_parse_workspace_invalid_arguments(void)
{
    NorthKeybindResult r1 = keybind_parse(&test_arena, "Super+0", "workspace 0");
    TEST_ASSERT_FALSE(r1.ok);

    NorthKeybindResult r2 = keybind_parse(&test_arena, "Super+1", "workspace -5");
    TEST_ASSERT_FALSE(r2.ok);

    NorthKeybindResult r3 = keybind_parse(&test_arena, "Super+1", "workspace abc");
    TEST_ASSERT_FALSE(r3.ok);

    NorthKeybindResult r4 = keybind_parse(&test_arena, "Super+1", "workspace");
    TEST_ASSERT_FALSE(r4.ok);

    NorthKeybindResult r5 = keybind_parse(&test_arena, "Super+1", "workspace 1abc");
    TEST_ASSERT_FALSE(r5.ok);

    NorthKeybindResult r6 = keybind_parse(&test_arena, "Super+Shift+0", "move_to_workspace 0");
    TEST_ASSERT_FALSE(r6.ok);

    NorthKeybindResult r7 = keybind_parse(&test_arena, "Super+Shift+1", "move_to_workspace -1");
    TEST_ASSERT_FALSE(r7.ok);

    NorthKeybindResult r8 = keybind_parse(&test_arena, "Super+Shift+1", "move_to_workspace xyz");
    TEST_ASSERT_FALSE(r8.ok);

    NorthKeybindResult r9 = keybind_parse(&test_arena, "Super+Shift+1", "move_to_workspace");
    TEST_ASSERT_FALSE(r9.ok);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_keybind_parse_modifiers_and_keysym);
    RUN_TEST(test_keybind_parse_invalid);
    RUN_TEST(test_keybind_parse_workspace_switch);
    RUN_TEST(test_keybind_parse_move_to_workspace);
    RUN_TEST(test_keybind_parse_workspace_cycling);
    RUN_TEST(test_keybind_parse_workspace_invalid_arguments);
    return UNITY_END();
}
