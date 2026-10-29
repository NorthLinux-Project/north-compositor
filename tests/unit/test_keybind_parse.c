#include "unity.h"
#include "input/keybind.h"

static Arena test_arena;

void setUp(void) {
    test_arena = arena_create(1024 * 32);
}

void tearDown(void) {
    arena_destroy(&test_arena);
}

void test_keybind_parse_modifiers_and_keysym(void) {
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

void test_keybind_parse_invalid(void) {
    NorthKeybindResult r1 = keybind_parse(&test_arena, "UnknownMod+Q", "close");
    TEST_ASSERT_FALSE(r1.ok);

    NorthKeybindResult r2 = keybind_parse(&test_arena, "Super+UnknownKeySymbol12345", "close");
    TEST_ASSERT_FALSE(r2.ok);

    NorthKeybindResult r3 = keybind_parse(&test_arena, "Super+Q", "invalid_action");
    TEST_ASSERT_FALSE(r3.ok);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_keybind_parse_modifiers_and_keysym);
    RUN_TEST(test_keybind_parse_invalid);
    return UNITY_END();
}
