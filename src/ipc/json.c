#include "ipc/json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_INITIAL_CAPACITY 512

static void json_builder_ensure_capacity(JsonBuilder *jb, size_t additional)
{
    if (jb->length + additional + 1 <= jb->capacity) {
        return;
    }

    size_t new_cap = jb->capacity ? jb->capacity * 2 : JSON_INITIAL_CAPACITY;
    while (new_cap < jb->length + additional + 1) {
        new_cap *= 2;
    }

    char *new_buf = arena_alloc(jb->arena, new_cap, 1);
    if (!new_buf) {
        return;
    }

    if (jb->buffer && jb->length > 0) {
        memcpy(new_buf, jb->buffer, jb->length);
    }
    jb->buffer = new_buf;
    jb->capacity = new_cap;
}

static void json_builder_append_char(JsonBuilder *jb, char c)
{
    json_builder_ensure_capacity(jb, 1);
    if (jb->length + 1 < jb->capacity) {
        jb->buffer[jb->length++] = c;
        jb->buffer[jb->length] = '\0';
    }
}

static void json_builder_append_raw(JsonBuilder *jb, const char *str, size_t len)
{
    if (!str || len == 0) {
        return;
    }
    json_builder_ensure_capacity(jb, len);
    if (jb->length + len < jb->capacity) {
        memcpy(jb->buffer + jb->length, str, len);
        jb->length += len;
        jb->buffer[jb->length] = '\0';
    }
}

static void json_builder_write_comma_if_needed(JsonBuilder *jb)
{
    if (jb->depth > 0 && jb->depth < JSON_MAX_DEPTH) {
        if (jb->needs_comma[jb->depth]) {
            json_builder_append_char(jb, ',');
        }
        jb->needs_comma[jb->depth] = true;
    }
}

void json_builder_init(JsonBuilder *jb, Arena *arena)
{
    memset(jb, 0, sizeof(*jb));
    jb->arena = arena;
    jb->capacity = JSON_INITIAL_CAPACITY;
    jb->buffer = arena_alloc(arena, jb->capacity, 1);
    if (jb->buffer) {
        jb->buffer[0] = '\0';
    }
}

void json_builder_object_begin(JsonBuilder *jb)
{
    json_builder_write_comma_if_needed(jb);
    json_builder_append_char(jb, '{');
    if (jb->depth + 1 < JSON_MAX_DEPTH) {
        jb->depth++;
        jb->needs_comma[jb->depth] = false;
    }
}

void json_builder_object_end(JsonBuilder *jb)
{
    if (jb->depth > 0) {
        jb->depth--;
    }
    json_builder_append_char(jb, '}');
}

void json_builder_array_begin(JsonBuilder *jb)
{
    json_builder_write_comma_if_needed(jb);
    json_builder_append_char(jb, '[');
    if (jb->depth + 1 < JSON_MAX_DEPTH) {
        jb->depth++;
        jb->needs_comma[jb->depth] = false;
    }
}

void json_builder_array_end(JsonBuilder *jb)
{
    if (jb->depth > 0) {
        jb->depth--;
    }
    json_builder_append_char(jb, ']');
}

void json_builder_key(JsonBuilder *jb, const char *key)
{
    json_builder_write_comma_if_needed(jb);
    json_builder_append_char(jb, '"');
    if (key) {
        json_builder_append_raw(jb, key, strlen(key));
    }
    json_builder_append_raw(jb, "\":", 2);
    if (jb->depth > 0 && jb->depth < JSON_MAX_DEPTH) {
        jb->needs_comma[jb->depth] = false;
    }
}

void json_builder_string(JsonBuilder *jb, const char *val)
{
    json_builder_write_comma_if_needed(jb);
    if (!val) {
        json_builder_append_raw(jb, "null", 4);
        return;
    }

    json_builder_append_char(jb, '"');
    for (const char *p = val; *p; p++) {
        switch (*p) {
        case '"':
            json_builder_append_raw(jb, "\\\"", 2);
            break;
        case '\\':
            json_builder_append_raw(jb, "\\\\", 2);
            break;
        case '\b':
            json_builder_append_raw(jb, "\\b", 2);
            break;
        case '\f':
            json_builder_append_raw(jb, "\\f", 2);
            break;
        case '\n':
            json_builder_append_raw(jb, "\\n", 2);
            break;
        case '\r':
            json_builder_append_raw(jb, "\\r", 2);
            break;
        case '\t':
            json_builder_append_raw(jb, "\\t", 2);
            break;
        default:
            if ((unsigned char)*p < 0x20) {
                char hex[8];
                snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)*p);
                json_builder_append_raw(jb, hex, 6);
            } else {
                json_builder_append_char(jb, *p);
            }
            break;
        }
    }
    json_builder_append_char(jb, '"');
}

void json_builder_int(JsonBuilder *jb, int64_t val)
{
    json_builder_write_comma_if_needed(jb);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)val);
    if (len > 0) {
        json_builder_append_raw(jb, buf, (size_t)len);
    }
}

void json_builder_double(JsonBuilder *jb, double val)
{
    json_builder_write_comma_if_needed(jb);
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%.4f", val);
    if (len > 0) {
        json_builder_append_raw(jb, buf, (size_t)len);
    }
}

void json_builder_bool(JsonBuilder *jb, bool val)
{
    json_builder_write_comma_if_needed(jb);
    if (val) {
        json_builder_append_raw(jb, "true", 4);
    } else {
        json_builder_append_raw(jb, "false", 5);
    }
}

void json_builder_null(JsonBuilder *jb)
{
    json_builder_write_comma_if_needed(jb);
    json_builder_append_raw(jb, "null", 4);
}

void json_builder_key_string(JsonBuilder *jb, const char *key, const char *val)
{
    json_builder_key(jb, key);
    json_builder_string(jb, val);
}

void json_builder_key_int(JsonBuilder *jb, const char *key, int64_t val)
{
    json_builder_key(jb, key);
    json_builder_int(jb, val);
}

void json_builder_key_double(JsonBuilder *jb, const char *key, double val)
{
    json_builder_key(jb, key);
    json_builder_double(jb, val);
}

void json_builder_key_bool(JsonBuilder *jb, const char *key, bool val)
{
    json_builder_key(jb, key);
    json_builder_bool(jb, val);
}

void json_builder_key_null(JsonBuilder *jb, const char *key)
{
    json_builder_key(jb, key);
    json_builder_null(jb);
}

const char *json_builder_finish(JsonBuilder *jb, size_t *out_len)
{
    if (out_len) {
        *out_len = jb->length;
    }
    return jb->buffer ? jb->buffer : "";
}

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    Arena *arena;
} JsonParser;

static void json_skip_whitespace(JsonParser *p)
{
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos])) {
        p->pos++;
    }
}

static char json_peek(JsonParser *p)
{
    json_skip_whitespace(p);
    return (p->pos < p->len) ? p->src[p->pos] : '\0';
}

static char json_next(JsonParser *p)
{
    json_skip_whitespace(p);
    return (p->pos < p->len) ? p->src[p->pos++] : '\0';
}

static JsonValue *json_parse_value(JsonParser *p);

static char *json_parse_raw_string(JsonParser *p)
{
    if (json_next(p) != '"') {
        return NULL;
    }

    size_t start = p->pos;
    size_t out_len = 0;
    while (p->pos < p->len && p->src[p->pos] != '"') {
        if (p->src[p->pos] == '\\' && p->pos + 1 < p->len) {
            p->pos += 2;
        } else {
            p->pos++;
        }
        out_len++;
    }

    if (p->pos >= p->len) {
        return NULL;
    }

    char *res = arena_alloc(p->arena, out_len + 1, 1);
    if (!res) {
        p->pos++;
        return NULL;
    }

    size_t in = start;
    size_t out = 0;
    while (in < p->pos) {
        if (p->src[in] == '\\' && in + 1 < p->pos) {
            in++;
            switch (p->src[in]) {
            case '"':
                res[out++] = '"';
                break;
            case '\\':
                res[out++] = '\\';
                break;
            case '/':
                res[out++] = '/';
                break;
            case 'b':
                res[out++] = '\b';
                break;
            case 'f':
                res[out++] = '\f';
                break;
            case 'n':
                res[out++] = '\n';
                break;
            case 'r':
                res[out++] = '\r';
                break;
            case 't':
                res[out++] = '\t';
                break;
            default:
                res[out++] = p->src[in];
                break;
            }
            in++;
        } else {
            res[out++] = p->src[in++];
        }
    }
    res[out] = '\0';
    p->pos++;
    return res;
}

static JsonValue *json_parse_string_value(JsonParser *p)
{
    char *s = json_parse_raw_string(p);
    if (!s) {
        return NULL;
    }
    JsonValue *v = arena_push_type(p->arena, JsonValue);
    if (!v) {
        return NULL;
    }
    v->type = JSON_STRING;
    v->string_val = s;
    return v;
}

static JsonValue *json_parse_number(JsonParser *p)
{
    json_skip_whitespace(p);
    size_t start = p->pos;
    if (p->pos < p->len && (p->src[p->pos] == '-' || p->src[p->pos] == '+')) {
        p->pos++;
    }
    while (p->pos < p->len && (isdigit((unsigned char)p->src[p->pos]) || p->src[p->pos] == '.' ||
                               p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        p->pos++;
    }

    size_t num_len = p->pos - start;
    if (num_len == 0) {
        return NULL;
    }

    char tmp[64];
    if (num_len >= sizeof(tmp)) {
        num_len = sizeof(tmp) - 1;
    }
    memcpy(tmp, p->src + start, num_len);
    tmp[num_len] = '\0';

    double val = strtod(tmp, NULL);
    JsonValue *v = arena_push_type(p->arena, JsonValue);
    if (!v) {
        return NULL;
    }
    v->type = JSON_NUMBER;
    v->number_val = val;
    return v;
}

static JsonValue *json_parse_array(JsonParser *p)
{
    if (json_next(p) != '[') {
        return NULL;
    }

    size_t capacity = 8;
    size_t count = 0;
    JsonValue **items = arena_push_array(p->arena, JsonValue *, capacity);
    if (!items) {
        return NULL;
    }

    while (json_peek(p) != ']' && json_peek(p) != '\0') {
        JsonValue *item = json_parse_value(p);
        if (!item) {
            break;
        }

        if (count >= capacity) {
            size_t new_cap = capacity * 2;
            JsonValue **new_items = arena_push_array(p->arena, JsonValue *, new_cap);
            if (!new_items) {
                break;
            }
            memcpy(new_items, items, sizeof(JsonValue *) * count);
            items = new_items;
            capacity = new_cap;
        }
        items[count++] = item;

        char c = json_peek(p);
        if (c == ',') {
            json_next(p);
        } else if (c != ']') {
            break;
        }
    }

    if (json_next(p) != ']') {
        return NULL;
    }

    JsonValue *v = arena_push_type(p->arena, JsonValue);
    if (!v) {
        return NULL;
    }
    v->type = JSON_ARRAY;
    v->array_val.items = items;
    v->array_val.count = count;
    return v;
}

static JsonValue *json_parse_object(JsonParser *p)
{
    if (json_next(p) != '{') {
        return NULL;
    }

    size_t capacity = 8;
    size_t count = 0;
    const char **keys = arena_push_array(p->arena, const char *, capacity);
    JsonValue **values = arena_push_array(p->arena, JsonValue *, capacity);
    if (!keys || !values) {
        return NULL;
    }

    while (json_peek(p) != '}' && json_peek(p) != '\0') {
        char *key = json_parse_raw_string(p);
        if (!key) {
            break;
        }

        if (json_next(p) != ':') {
            break;
        }

        JsonValue *val = json_parse_value(p);
        if (!val) {
            break;
        }

        if (count >= capacity) {
            size_t new_cap = capacity * 2;
            const char **new_keys = arena_push_array(p->arena, const char *, new_cap);
            JsonValue **new_vals = arena_push_array(p->arena, JsonValue *, new_cap);
            if (!new_keys || !new_vals) {
                break;
            }
            memcpy((void *)new_keys, keys, sizeof(const char *) * count);
            memcpy(new_vals, values, sizeof(JsonValue *) * count);
            keys = new_keys;
            values = new_vals;
            capacity = new_cap;
        }

        keys[count] = key;
        values[count] = val;
        count++;

        char c = json_peek(p);
        if (c == ',') {
            json_next(p);
        } else if (c != '}') {
            break;
        }
    }

    if (json_next(p) != '}') {
        return NULL;
    }

    JsonValue *v = arena_push_type(p->arena, JsonValue);
    if (!v) {
        return NULL;
    }
    v->type = JSON_OBJECT;
    v->object_val.keys = keys;
    v->object_val.values = values;
    v->object_val.count = count;
    return v;
}

static JsonValue *json_parse_value(JsonParser *p)
{
    char c = json_peek(p);
    if (c == '"') {
        return json_parse_string_value(p);
    }
    if (c == '{') {
        return json_parse_object(p);
    }
    if (c == '[') {
        return json_parse_array(p);
    }
    if (c == '-' || c == '+' || isdigit((unsigned char)c)) {
        return json_parse_number(p);
    }
    if (strncmp(p->src + p->pos, "true", 4) == 0) {
        p->pos += 4;
        JsonValue *v = arena_push_type(p->arena, JsonValue);
        if (v) {
            v->type = JSON_BOOL;
            v->bool_val = true;
        }
        return v;
    }
    if (strncmp(p->src + p->pos, "false", 5) == 0) {
        p->pos += 5;
        JsonValue *v = arena_push_type(p->arena, JsonValue);
        if (v) {
            v->type = JSON_BOOL;
            v->bool_val = false;
        }
        return v;
    }
    if (strncmp(p->src + p->pos, "null", 4) == 0) {
        p->pos += 4;
        JsonValue *v = arena_push_type(p->arena, JsonValue);
        if (v) {
            v->type = JSON_NULL;
        }
        return v;
    }
    return NULL;
}

JsonValue *json_parse(Arena *arena, const char *text, size_t len)
{
    if (!arena || !text || len == 0) {
        return NULL;
    }
    JsonParser parser = {
        .src = text,
        .len = len,
        .pos = 0,
        .arena = arena,
    };
    return json_parse_value(&parser);
}

JsonValue *json_object_get(const JsonValue *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) {
        return NULL;
    }
    for (size_t i = 0; i < obj->object_val.count; i++) {
        if (obj->object_val.keys[i] && strcmp(obj->object_val.keys[i], key) == 0) {
            return obj->object_val.values[i];
        }
    }
    return NULL;
}

const char *json_string_value(const JsonValue *val)
{
    return (val && val->type == JSON_STRING) ? val->string_val : NULL;
}

double json_number_value(const JsonValue *val)
{
    return (val && val->type == JSON_NUMBER) ? val->number_val : 0.0;
}

bool json_bool_value(const JsonValue *val)
{
    return (val && val->type == JSON_BOOL) ? val->bool_val : false;
}
