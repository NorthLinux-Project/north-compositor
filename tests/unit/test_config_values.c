#include "config/config.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_color_parsing_valid(void)
{
    NorthColorResult c1 = config_parse_color("#ffffff");
    TEST_ASSERT_TRUE(c1.ok);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, c1.value.r);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, c1.value.g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, c1.value.b);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, c1.value.a);

    NorthColorResult c2 = config_parse_color("ff000080");
    TEST_ASSERT_TRUE(c2.ok);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, c2.value.r);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, c2.value.g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, c2.value.b);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, c2.value.a);
}

void test_color_parsing_invalid(void)
{
    NorthColorResult c1 = config_parse_color("#fff");
    TEST_ASSERT_FALSE(c1.ok);

    NorthColorResult c2 = config_parse_color("not_a_color");
    TEST_ASSERT_FALSE(c2.ok);

    NorthColorResult c3 = config_parse_color(NULL);
    TEST_ASSERT_FALSE(c3.ok);
}

void test_default_config_creation(void)
{
    NorthConfig *cfg = config_create_default();
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_STRING("us", cfg->keyboard.layout);
    TEST_ASSERT_EQUAL_INT(25, cfg->keyboard.repeat_rate);
    TEST_ASSERT_EQUAL_INT(600, cfg->keyboard.repeat_delay);
    TEST_ASSERT_EQUAL_INT(28, cfg->decor.titlebar_height);
    TEST_ASSERT_TRUE(cfg->bind_count > 0);

    config_destroy(cfg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_color_parsing_valid);
    RUN_TEST(test_color_parsing_invalid);
    RUN_TEST(test_default_config_creation);
    return UNITY_END();
}
