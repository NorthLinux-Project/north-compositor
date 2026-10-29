#include "unity.h"
#include "shell/placement.h"

void setUp(void) {}
void tearDown(void) {}

void test_placement_initial_centers_window(void) {
    NorthBox output = { .x = 0, .y = 0, .width = 1920, .height = 1080 };
    NorthBox initial = placement_initial(output, output, NULL, 0, 800, 600);

    TEST_ASSERT_EQUAL_INT((1920 - 800) / 2, initial.x);
    TEST_ASSERT_EQUAL_INT((1080 - 600) / 2, initial.y);
    TEST_ASSERT_EQUAL_INT(800, initial.width);
    TEST_ASSERT_EQUAL_INT(600, initial.height);
}

void test_placement_initial_cascades_on_collision(void) {
    NorthBox output = { .x = 0, .y = 0, .width = 1920, .height = 1080 };
    NorthBox existing[1] = {
        { .x = 560, .y = 240, .width = 800, .height = 600 }
    };

    NorthBox initial = placement_initial(output, output, existing, 1, 800, 600);
    TEST_ASSERT_EQUAL_INT(560 + 32, initial.x);
    TEST_ASSERT_EQUAL_INT(240 + 32, initial.y);
}

void test_placement_snap_edges(void) {
    NorthBox output = { .x = 0, .y = 0, .width = 1920, .height = 1080 };
    NorthBox moving = { .x = 10, .y = 10, .width = 800, .height = 600 };

    NorthBox snapped = placement_snap(moving, output, NULL, 0, 16);
    TEST_ASSERT_EQUAL_INT(0, snapped.x);
    TEST_ASSERT_EQUAL_INT(0, snapped.y);

    NorthBox moving_far = { .x = 50, .y = 50, .width = 800, .height = 600 };
    NorthBox unsnapped = placement_snap(moving_far, output, NULL, 0, 16);
    TEST_ASSERT_EQUAL_INT(50, unsnapped.x);
    TEST_ASSERT_EQUAL_INT(50, unsnapped.y);
}

void test_placement_clamp_visible(void) {
    NorthBox output = { .x = 0, .y = 0, .width = 1920, .height = 1080 };
    NorthBox off_left = { .x = -900, .y = 100, .width = 800, .height = 600 };

    NorthBox clamped = placement_clamp_visible(off_left, output, 64);
    TEST_ASSERT_TRUE(clamped.x + clamped.width >= 64);
}

void test_placement_default_scale_dpi_heuristics(void) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, placement_default_scale(500, 1920));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, placement_default_scale(300, 3840));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placement_initial_centers_window);
    RUN_TEST(test_placement_initial_cascades_on_collision);
    RUN_TEST(test_placement_snap_edges);
    RUN_TEST(test_placement_clamp_visible);
    RUN_TEST(test_placement_default_scale_dpi_heuristics);
    return UNITY_END();
}
