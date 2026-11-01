#include "unity.h"
#include "util/match.h"

void setUp(void) {}
void tearDown(void) {}

void test_pattern_match_exact(void)
{
    TEST_ASSERT_TRUE(pattern_match("foot", "foot"));
    TEST_ASSERT_TRUE(pattern_match("foot", "Foot"));
    TEST_ASSERT_TRUE(pattern_match("FOOT", "foot"));
    TEST_ASSERT_TRUE(pattern_match("foot", "FOOT"));
    TEST_ASSERT_FALSE(pattern_match("foot", "alacritty"));
    TEST_ASSERT_FALSE(pattern_match("foot", "fo"));
    TEST_ASSERT_FALSE(pattern_match("foot", "foott"));
}

void test_pattern_match_wildcards(void)
{
    /* Suffix wildcard (prefix match) */
    TEST_ASSERT_TRUE(pattern_match("foo*", "foot"));
    TEST_ASSERT_TRUE(pattern_match("foo*", "football"));
    TEST_ASSERT_TRUE(pattern_match("foo*", "FOO"));
    TEST_ASSERT_FALSE(pattern_match("foo*", "barfoot"));

    /* Prefix wildcard (suffix match) */
    TEST_ASSERT_TRUE(pattern_match("*term", "xterm"));
    TEST_ASSERT_TRUE(pattern_match("*term", "XTERM"));
    TEST_ASSERT_TRUE(pattern_match("*term", "term"));
    TEST_ASSERT_FALSE(pattern_match("*term", "terminal"));

    /* Substring wildcard */
    TEST_ASSERT_TRUE(pattern_match("*browser*", "firefox-browser-bin"));
    TEST_ASSERT_TRUE(pattern_match("*browser*", "browser"));
    TEST_ASSERT_TRUE(pattern_match("*browser*", "my-BROWSER"));
    TEST_ASSERT_FALSE(pattern_match("*browser*", "editor"));

    /* Multiple wildcards */
    TEST_ASSERT_TRUE(pattern_match("*foo*bar*", "prefix_foo_middle_bar_suffix"));
    TEST_ASSERT_TRUE(pattern_match("foo*bar", "foobar"));
    TEST_ASSERT_TRUE(pattern_match("foo*bar", "foo_123_bar"));
    TEST_ASSERT_FALSE(pattern_match("foo*bar", "foo_123_baz"));

    /* Single wildcard matching everything */
    TEST_ASSERT_TRUE(pattern_match("*", "anything"));
    TEST_ASSERT_TRUE(pattern_match("*", ""));

    /* Question mark single-character wildcard */
    TEST_ASSERT_TRUE(pattern_match("f?o", "foo"));
    TEST_ASSERT_TRUE(pattern_match("f?o", "FRO"));
    TEST_ASSERT_FALSE(pattern_match("f?o", "foobar"));
    /* Consecutive wildcards */
    TEST_ASSERT_TRUE(pattern_match("**", "anything"));
    TEST_ASSERT_TRUE(pattern_match("**a", "a"));
    TEST_ASSERT_TRUE(pattern_match("**a", "ba"));
    TEST_ASSERT_TRUE(pattern_match("a**", "a"));
    TEST_ASSERT_TRUE(pattern_match("a**b", "a_b"));
    TEST_ASSERT_FALSE(pattern_match("a**b", "a_c"));

    /* Case-insensitive wildcards */
    TEST_ASSERT_TRUE(pattern_match("*Foo*Bar*", "prefix_foo_middle_bar_suffix"));
    TEST_ASSERT_TRUE(pattern_match("*FOO*BAR*", "PREFIX_foo_MIDDLE_bar_SUFFIX"));

    /* Special characters */
    TEST_ASSERT_TRUE(pattern_match("org.mozilla.*", "org.mozilla.Firefox"));
    TEST_ASSERT_TRUE(pattern_match("*-app", "test-demo-app"));
    TEST_ASSERT_FALSE(pattern_match("org.mozilla.*", "org.gnome.Terminal"));
}

void test_pattern_match_edge_cases(void)
{
    TEST_ASSERT_TRUE(pattern_match("", ""));
    TEST_ASSERT_FALSE(pattern_match("", "abc"));
    TEST_ASSERT_FALSE(pattern_match("abc", ""));

    TEST_ASSERT_FALSE(pattern_match(NULL, "abc"));
    TEST_ASSERT_FALSE(pattern_match("abc", NULL));
    TEST_ASSERT_FALSE(pattern_match(NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pattern_match_exact);
    RUN_TEST(test_pattern_match_wildcards);
    RUN_TEST(test_pattern_match_edge_cases);
    return UNITY_END();
}
