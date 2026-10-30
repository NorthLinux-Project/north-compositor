#include "config/parser.h"
#include "unity.h"

#include <string.h>

static Arena test_arena;

void setUp(void) { test_arena = arena_create(1024 * 64); }

void tearDown(void) { arena_destroy(&test_arena); }

void test_parser_basic_sections_and_keys(void)
{
    const char *text = "[input:keyboard]\n"
                       "layout = us\n"
                       "repeat_rate = 25\n"
                       "\n"
                       "[decoration]\n"
                       "font = Inter 10\n";

    ConfigDocResult res = config_parse(&test_arena, text, strlen(text));
    TEST_ASSERT_TRUE(res.ok);
    TEST_ASSERT_EQUAL_size_t(3, res.value.count);

    TEST_ASSERT_EQUAL_STRING("input:keyboard", res.value.entries[0].section);
    TEST_ASSERT_EQUAL_STRING("layout", res.value.entries[0].key);
    TEST_ASSERT_EQUAL_STRING("us", res.value.entries[0].value);

    TEST_ASSERT_EQUAL_STRING("input:keyboard", res.value.entries[1].section);
    TEST_ASSERT_EQUAL_STRING("repeat_rate", res.value.entries[1].key);
    TEST_ASSERT_EQUAL_STRING("25", res.value.entries[1].value);

    TEST_ASSERT_EQUAL_STRING("decoration", res.value.entries[2].section);
    TEST_ASSERT_EQUAL_STRING("font", res.value.entries[2].key);
    TEST_ASSERT_EQUAL_STRING("Inter 10", res.value.entries[2].value);
}

void test_parser_comments_and_whitespace(void)
{
    const char *text = "# Top comment\n"
                       "  ; Another comment\n"
                       "   [section]   \n"
                       "   key   =    value   \n"
                       "\n"
                       "# Mid comment\n"
                       "quoted = \"hello world\"\n";

    ConfigDocResult res = config_parse(&test_arena, text, strlen(text));
    TEST_ASSERT_TRUE(res.ok);
    TEST_ASSERT_EQUAL_size_t(2, res.value.count);

    TEST_ASSERT_EQUAL_STRING("section", res.value.entries[0].section);
    TEST_ASSERT_EQUAL_STRING("key", res.value.entries[0].key);
    TEST_ASSERT_EQUAL_STRING("value", res.value.entries[0].value);

    TEST_ASSERT_EQUAL_STRING("quoted", res.value.entries[1].key);
    TEST_ASSERT_EQUAL_STRING("hello world", res.value.entries[1].value);
}

void test_parser_invalid_syntax_errors(void)
{
    const char *unclosed = "[unclosed_section\nkey = val\n";
    ConfigDocResult res1 = config_parse(&test_arena, unclosed, strlen(unclosed));
    TEST_ASSERT_FALSE(res1.ok);

    const char *no_eq = "[sec]\nmissing_equal_sign\n";
    ConfigDocResult res2 = config_parse(&test_arena, no_eq, strlen(no_eq));
    TEST_ASSERT_FALSE(res2.ok);

    const char *no_key = "[sec]\n = value\n";
    ConfigDocResult res3 = config_parse(&test_arena, no_key, strlen(no_key));
    TEST_ASSERT_FALSE(res3.ok);
}

void test_parser_empty_input(void)
{
    ConfigDocResult res = config_parse(&test_arena, "", 0);
    TEST_ASSERT_TRUE(res.ok);
    TEST_ASSERT_EQUAL_size_t(0, res.value.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parser_basic_sections_and_keys);
    RUN_TEST(test_parser_comments_and_whitespace);
    RUN_TEST(test_parser_invalid_syntax_errors);
    RUN_TEST(test_parser_empty_input);
    return UNITY_END();
}
