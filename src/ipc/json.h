#ifndef NORTH_IPC_JSON_H
#define NORTH_IPC_JSON_H

#include "util/arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JSON_MAX_DEPTH 32

typedef struct {
    Arena *arena;
    char *buffer;
    size_t capacity;
    size_t length;
    int depth;
    bool needs_comma[JSON_MAX_DEPTH];
} JsonBuilder;

enum json_type {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
};

typedef struct JsonValue JsonValue;

struct JsonValue {
    enum json_type type;
    union {
        bool bool_val;
        double number_val;
        const char *string_val;
        struct {
            JsonValue **items;
            size_t count;
        } array_val;
        struct {
            const char **keys;
            JsonValue **values;
            size_t count;
        } object_val;
    };
};

void json_builder_init(JsonBuilder *jb, Arena *arena);
void json_builder_object_begin(JsonBuilder *jb);
void json_builder_object_end(JsonBuilder *jb);
void json_builder_array_begin(JsonBuilder *jb);
void json_builder_array_end(JsonBuilder *jb);

void json_builder_key(JsonBuilder *jb, const char *key);
void json_builder_key_string(JsonBuilder *jb, const char *key, const char *val);
void json_builder_key_int(JsonBuilder *jb, const char *key, int64_t val);
void json_builder_key_double(JsonBuilder *jb, const char *key, double val);
void json_builder_key_bool(JsonBuilder *jb, const char *key, bool val);
void json_builder_key_null(JsonBuilder *jb, const char *key);

void json_builder_string(JsonBuilder *jb, const char *val);
void json_builder_int(JsonBuilder *jb, int64_t val);
void json_builder_double(JsonBuilder *jb, double val);
void json_builder_bool(JsonBuilder *jb, bool val);
void json_builder_null(JsonBuilder *jb);

const char *json_builder_finish(JsonBuilder *jb, size_t *out_len);

JsonValue *json_parse(Arena *arena, const char *text, size_t len);
JsonValue *json_object_get(const JsonValue *obj, const char *key);
const char *json_string_value(const JsonValue *val);
double json_number_value(const JsonValue *val);
bool json_bool_value(const JsonValue *val);

#endif
