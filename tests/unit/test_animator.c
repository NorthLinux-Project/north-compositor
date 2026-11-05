#include "anim/animator.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_ease_out_cubic_boundary_zero(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, anim_ease_out_cubic(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, anim_ease_out_cubic(-0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, anim_ease_out_cubic(-100.0f));
}

void test_ease_out_cubic_boundary_one(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, anim_ease_out_cubic(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, anim_ease_out_cubic(1.1f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, anim_ease_out_cubic(100.0f));
}

void test_ease_out_cubic_midpoint(void)
{
    // (1 - (1 - 0.5)^3) = 1 - 0.125 = 0.875
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.875f, anim_ease_out_cubic(0.5f));
}

void test_ease_out_cubic_monotonic(void)
{
    float prev = 0.0f;
    for (int i = 0; i <= 100; i++) {
        float t = (float)i / 100.0f;
        float val = anim_ease_out_cubic(t);
        TEST_ASSERT_TRUE(val >= prev);
        TEST_ASSERT_TRUE(val >= 0.0f && val <= 1.0f);
        prev = val;
    }
}

void test_anim_cancel_null(void)
{
    anim_cancel(NULL);
    TEST_ASSERT_FALSE(anim_is_running(NULL));
}

void test_anim_cancel_running(void)
{
    struct north_animator anim = {0};
    wl_list_init(&anim.link);
    anim.running = true;

    struct wl_list active_list;
    wl_list_init(&active_list);
    wl_list_insert(&active_list, &anim.link);

    TEST_ASSERT_TRUE(anim_is_running(&anim));
    anim_cancel(&anim);
    TEST_ASSERT_FALSE(anim_is_running(&anim));
    TEST_ASSERT_TRUE(wl_list_empty(&active_list));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ease_out_cubic_boundary_zero);
    RUN_TEST(test_ease_out_cubic_boundary_one);
    RUN_TEST(test_ease_out_cubic_midpoint);
    RUN_TEST(test_ease_out_cubic_monotonic);
    RUN_TEST(test_anim_cancel_null);
    RUN_TEST(test_anim_cancel_running);
    return UNITY_END();
}
