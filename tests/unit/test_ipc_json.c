#include "ipc/json.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_json_builder_empty_object(void)
{
    Arena arena = arena_create(4096);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);
    json_builder_object_begin(&jb);
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    TEST_ASSERT_EQUAL_STRING("{}", json);
    TEST_ASSERT_EQUAL_size_t(2, len);

    arena_destroy(&arena);
}

void test_json_builder_simple_types(void)
{
    Arena arena = arena_create(4096);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);
    json_builder_object_begin(&jb);
    json_builder_key_string(&jb, "name", "north");
    json_builder_key_int(&jb, "version", 1);
    json_builder_key_bool(&jb, "active", true);
    json_builder_key_null(&jb, "empty");
    json_builder_object_end(&jb);

    size_t len = 0;
    const char *json = json_builder_finish(&jb, &len);
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"north\",\"version\":1,\"active\":true,\"empty\":null}",
                             json);

    arena_destroy(&arena);
}

void test_json_builder_nested_array_and_escaping(void)
{
    Arena arena = arena_create(4096);
    JsonBuilder jb;
    json_builder_init(&jb, &arena);
    json_builder_object_begin(&jb);
    json_builder_key(&jb, "list");
    json_builder_array_begin(&jb);
    json_builder_string(&jb, "line1\nline2");
    json_builder_string(&jb, "quote: \"hello\"");
    json_builder_int(&jb, 42);
    json_builder_array_end(&jb);
    json_builder_object_end(&jb);

    const char *json = json_builder_finish(&jb, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"list\":[\"line1\\nline2\",\"quote: \\\"hello\\\"\",42]}", json);

    arena_destroy(&arena);
}

void test_json_parse_object_and_values(void)
{
    Arena arena = arena_create(4096);
    const char *input = "{\"title\": \"Firefox\", \"count\": 3, \"fullscreen\": false}";
    JsonValue *root = json_parse(&arena, input, strlen(input));

    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(JSON_OBJECT, root->type);

    JsonValue *title = json_object_get(root, "title");
    TEST_ASSERT_NOT_NULL(title);
    TEST_ASSERT_EQUAL_STRING("Firefox", json_string_value(title));

    JsonValue *count = json_object_get(root, "count");
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_EQUAL_FLOAT(3.0, json_number_value(count));

    JsonValue *fs = json_object_get(root, "fullscreen");
    TEST_ASSERT_NOT_NULL(fs);
    TEST_ASSERT_FALSE(json_bool_value(fs));

    arena_destroy(&arena);
}

void test_json_parse_array(void)
{
    Arena arena = arena_create(4096);
    const char *input = "[\"workspace\", \"window\", \"output\"]";
    JsonValue *root = json_parse(&arena, input, strlen(input));

    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(JSON_ARRAY, root->type);
    TEST_ASSERT_EQUAL_size_t(3, root->array_val.count);
    TEST_ASSERT_EQUAL_STRING("workspace", json_string_value(root->array_val.items[0]));
    TEST_ASSERT_EQUAL_STRING("window", json_string_value(root->array_val.items[1]));
    TEST_ASSERT_EQUAL_STRING("output", json_string_value(root->array_val.items[2]));

    arena_destroy(&arena);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_json_builder_empty_object);
    RUN_TEST(test_json_builder_simple_types);
    RUN_TEST(test_json_builder_nested_array_and_escaping);
    RUN_TEST(test_json_parse_object_and_values);
    RUN_TEST(test_json_parse_array);
    return UNITY_END();
}
