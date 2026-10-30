#include "shell/placement.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_placement_initial_centers_window(void)
{
    NorthBox output = {.x = 0, .y = 0, .width = 1920, .height = 1080};
    NorthBox initial = placement_initial(output, output, NULL, 0, 800, 600);

    TEST_ASSERT_EQUAL_INT((1920 - 800) / 2, initial.x);
    TEST_ASSERT_EQUAL_INT((1080 - 600) / 2, initial.y);
    TEST_ASSERT_EQUAL_INT(800, initial.width);
    TEST_ASSERT_EQUAL_INT(600, initial.height);
}

void test_placement_initial_cascades_on_collision(void)
{
    NorthBox output = {.x = 0, .y = 0, .width = 1920, .height = 1080};
    NorthBox existing[1] = {{.x = 560, .y = 240, .width = 800, .height = 600}};

    NorthBox initial = placement_initial(output, output, existing, 1, 800, 600);
    TEST_ASSERT_EQUAL_INT(560 + 32, initial.x);
    TEST_ASSERT_EQUAL_INT(240 + 32, initial.y);
}

void test_placement_snap_edges(void)
{
    NorthBox output = {.x = 0, .y = 0, .width = 1920, .height = 1080};
    NorthBox moving = {.x = 10, .y = 10, .width = 800, .height = 600};

    NorthBox snapped = placement_snap(moving, output, NULL, 0, 16);
    TEST_ASSERT_EQUAL_INT(0, snapped.x);
    TEST_ASSERT_EQUAL_INT(0, snapped.y);

    NorthBox moving_far = {.x = 50, .y = 50, .width = 800, .height = 600};
    NorthBox unsnapped = placement_snap(moving_far, output, NULL, 0, 16);
    TEST_ASSERT_EQUAL_INT(50, unsnapped.x);
    TEST_ASSERT_EQUAL_INT(50, unsnapped.y);
}

void test_placement_clamp_visible(void)
{
    NorthBox output = {.x = 0, .y = 0, .width = 1920, .height = 1080};
    NorthBox off_left = {.x = -900, .y = 100, .width = 800, .height = 600};

    NorthBox clamped = placement_clamp_visible(off_left, output, 64);
    TEST_ASSERT_TRUE(clamped.x + clamped.width >= 64);
}

void test_placement_default_scale_dpi_heuristics(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0f, placement_default_scale(500, 1920));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, placement_default_scale(300, 3840));
}

void test_placement_detect_snap_zone(void)
{
    NorthBox usable = {.x = 0, .y = 0, .width = 1920, .height = 1080};
    int threshold = 48;

    TEST_ASSERT_EQUAL_INT(SNAP_TOP_LEFT, placement_detect_snap_zone(usable, 1, 1, threshold));
    TEST_ASSERT_EQUAL_INT(SNAP_TOP_RIGHT, placement_detect_snap_zone(usable, 1919, 1, threshold));
    TEST_ASSERT_EQUAL_INT(SNAP_BOTTOM_LEFT, placement_detect_snap_zone(usable, 1, 1079, threshold));
    TEST_ASSERT_EQUAL_INT(SNAP_BOTTOM_RIGHT,
                          placement_detect_snap_zone(usable, 1919, 1079, threshold));

    TEST_ASSERT_EQUAL_INT(SNAP_TOP, placement_detect_snap_zone(usable, 960, 1, threshold));
    TEST_ASSERT_EQUAL_INT(SNAP_LEFT, placement_detect_snap_zone(usable, 1, 500, threshold));
    TEST_ASSERT_EQUAL_INT(SNAP_RIGHT, placement_detect_snap_zone(usable, 1919, 500, threshold));

    // When titlebar reaches top but cursor is in the titlebar, no snap zone is triggered
    TEST_ASSERT_EQUAL_INT(SNAP_NONE, placement_detect_snap_zone(usable, 960, 15, threshold));
    TEST_ASSERT_EQUAL_INT(SNAP_NONE, placement_detect_snap_zone(usable, 500, 500, threshold));
}

void test_placement_calculate_snap_box(void)
{
    NorthBox usable = {.x = 0, .y = 0, .width = 1920, .height = 1080};
    int tb = 28;

    NorthBox left = placement_calculate_snap_box(usable, SNAP_LEFT, tb);
    TEST_ASSERT_EQUAL_INT(0, left.x);
    TEST_ASSERT_EQUAL_INT(28, left.y);
    TEST_ASSERT_EQUAL_INT(960, left.width);
    TEST_ASSERT_EQUAL_INT(1052, left.height);

    NorthBox right = placement_calculate_snap_box(usable, SNAP_RIGHT, tb);
    TEST_ASSERT_EQUAL_INT(960, right.x);
    TEST_ASSERT_EQUAL_INT(28, right.y);
    TEST_ASSERT_EQUAL_INT(960, right.width);
    TEST_ASSERT_EQUAL_INT(1052, right.height);

    NorthBox top = placement_calculate_snap_box(usable, SNAP_TOP, tb);
    TEST_ASSERT_EQUAL_INT(0, top.x);
    TEST_ASSERT_EQUAL_INT(28, top.y);
    TEST_ASSERT_EQUAL_INT(1920, top.width);
    TEST_ASSERT_EQUAL_INT(1052, top.height);

    NorthBox tl = placement_calculate_snap_box(usable, SNAP_TOP_LEFT, tb);
    TEST_ASSERT_EQUAL_INT(0, tl.x);
    TEST_ASSERT_EQUAL_INT(28, tl.y);
    TEST_ASSERT_EQUAL_INT(960, tl.width);
    TEST_ASSERT_EQUAL_INT(512, tl.height);

    NorthBox br = placement_calculate_snap_box(usable, SNAP_BOTTOM_RIGHT, tb);
    TEST_ASSERT_EQUAL_INT(960, br.x);
    TEST_ASSERT_EQUAL_INT(568, br.y);
    TEST_ASSERT_EQUAL_INT(960, br.width);
    TEST_ASSERT_EQUAL_INT(512, br.height);

    NorthBox preview = placement_calculate_snap_box(usable, SNAP_LEFT, 0);
    TEST_ASSERT_EQUAL_INT(0, preview.x);
    TEST_ASSERT_EQUAL_INT(0, preview.y);
    TEST_ASSERT_EQUAL_INT(960, preview.width);
    TEST_ASSERT_EQUAL_INT(1080, preview.height);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_placement_initial_centers_window);
    RUN_TEST(test_placement_initial_cascades_on_collision);
    RUN_TEST(test_placement_snap_edges);
    RUN_TEST(test_placement_clamp_visible);
    RUN_TEST(test_placement_default_scale_dpi_heuristics);
    RUN_TEST(test_placement_detect_snap_zone);
    RUN_TEST(test_placement_calculate_snap_box);
    return UNITY_END();
}
