#include "config/config.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

static NorthConfig *load_config_from_string(const char *content)
{
    char tmp_path[] = "/tmp/north_test_cfg_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        return NULL;
    }
    size_t len = strlen(content);
    if (write(fd, content, len) != (ssize_t)len) {
        close(fd);
        unlink(tmp_path);
        return NULL;
    }
    close(fd);

    NorthConfigPtrResult res = config_load(tmp_path);
    unlink(tmp_path);

    return res.ok ? res.value : NULL;
}

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
    TEST_ASSERT_EQUAL_INT(9, cfg->workspaces.count);
    TEST_ASSERT_TRUE(cfg->bind_count > 0);

    config_destroy(cfg);
}

void test_workspace_config_parsing(void)
{
    const char *content = "[workspaces]\n"
                          "count = 5\n";
    NorthConfig *cfg = load_config_from_string(content);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_INT(5, cfg->workspaces.count);
    config_destroy(cfg);
}

void test_workspace_config_clamping(void)
{
    /* count = 0 clamps to 1 */
    NorthConfig *cfg_zero = load_config_from_string("[workspaces]\ncount = 0\n");
    TEST_ASSERT_NOT_NULL(cfg_zero);
    TEST_ASSERT_EQUAL_INT(1, cfg_zero->workspaces.count);
    config_destroy(cfg_zero);

    /* negative count clamps to 1 */
    NorthConfig *cfg_neg = load_config_from_string("[workspaces]\ncount = -5\n");
    TEST_ASSERT_NOT_NULL(cfg_neg);
    TEST_ASSERT_EQUAL_INT(1, cfg_neg->workspaces.count);
    config_destroy(cfg_neg);

    /* count = 50 clamps to 32 */
    NorthConfig *cfg_over = load_config_from_string("[workspaces]\ncount = 50\n");
    TEST_ASSERT_NOT_NULL(cfg_over);
    TEST_ASSERT_EQUAL_INT(32, cfg_over->workspaces.count);
    config_destroy(cfg_over);

    /* valid boundary values 1 and 32 are preserved */
    NorthConfig *cfg_one = load_config_from_string("[workspaces]\ncount = 1\n");
    TEST_ASSERT_NOT_NULL(cfg_one);
    TEST_ASSERT_EQUAL_INT(1, cfg_one->workspaces.count);
    config_destroy(cfg_one);

    NorthConfig *cfg_thirtytwo = load_config_from_string("[workspaces]\ncount = 32\n");
    TEST_ASSERT_NOT_NULL(cfg_thirtytwo);
    TEST_ASSERT_EQUAL_INT(32, cfg_thirtytwo->workspaces.count);
    config_destroy(cfg_thirtytwo);
}

void test_workspace_config_invalid_fallback(void)
{
    /* non-numeric count falls back to default 9 */
    NorthConfig *cfg_alpha = load_config_from_string("[workspaces]\ncount = abc\n");
    TEST_ASSERT_NOT_NULL(cfg_alpha);
    TEST_ASSERT_EQUAL_INT(9, cfg_alpha->workspaces.count);
    config_destroy(cfg_alpha);

    /* non-numeric trailing garbage falls back to default 9 */
    NorthConfig *cfg_garbage = load_config_from_string("[workspaces]\ncount = 5xyz\n");
    TEST_ASSERT_NOT_NULL(cfg_garbage);
    TEST_ASSERT_EQUAL_INT(9, cfg_garbage->workspaces.count);
    config_destroy(cfg_garbage);

    /* empty key value falls back to default 9 */
    NorthConfig *cfg_empty = load_config_from_string("[workspaces]\ncount =\n");
    TEST_ASSERT_NOT_NULL(cfg_empty);
    TEST_ASSERT_EQUAL_INT(9, cfg_empty->workspaces.count);
    config_destroy(cfg_empty);

    /* missing key in section falls back to default 9 */
    NorthConfig *cfg_sec_only = load_config_from_string("[workspaces]\n");
    TEST_ASSERT_NOT_NULL(cfg_sec_only);
    TEST_ASSERT_EQUAL_INT(9, cfg_sec_only->workspaces.count);
    config_destroy(cfg_sec_only);
}

void test_compositor_conf_default_file(void)
{
    const char *paths[] = {
        "compositor.conf",
        "../compositor.conf",
        "../../compositor.conf",
        "../../../compositor.conf",
    };
    const char *found_path = NULL;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (access(paths[i], R_OK) == 0) {
            found_path = paths[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(found_path);

    NorthConfigPtrResult res = config_load(found_path);
    TEST_ASSERT_TRUE(res.ok);
    NorthConfig *cfg = res.value;
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_INT(9, cfg->workspaces.count);

    int ws_binds = 0;
    int move_binds = 0;
    int cycle_binds = 0;
    for (size_t i = 0; i < cfg->bind_count; i++) {
        if (cfg->binds[i].action == NORTH_KEYBIND_ACTION_WORKSPACE) {
            ws_binds++;
        } else if (cfg->binds[i].action == NORTH_KEYBIND_ACTION_MOVE_TO_WORKSPACE) {
            move_binds++;
        } else if (cfg->binds[i].action == NORTH_KEYBIND_ACTION_WORKSPACE_PREV ||
                   cfg->binds[i].action == NORTH_KEYBIND_ACTION_WORKSPACE_NEXT) {
            cycle_binds++;
        }
    }
    TEST_ASSERT_EQUAL_INT(9, ws_binds);
    TEST_ASSERT_EQUAL_INT(9, move_binds);
    TEST_ASSERT_EQUAL_INT(2, cycle_binds);

    config_destroy(cfg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_color_parsing_valid);
    RUN_TEST(test_color_parsing_invalid);
    RUN_TEST(test_default_config_creation);
    RUN_TEST(test_workspace_config_parsing);
    RUN_TEST(test_workspace_config_clamping);
    RUN_TEST(test_workspace_config_invalid_fallback);
    RUN_TEST(test_compositor_conf_default_file);
    return UNITY_END();
}
