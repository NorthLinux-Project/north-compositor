#include "config/parser.h"
#include <ctype.h>
#include <string.h>

static const char *skip_whitespace(const char *s, const char *end) {
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r')) {
        s++;
    }
    return s;
}

static const char *rtrim(const char *start, const char *end) {
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        end--;
    }
    return end;
}

static char *arena_strndup(Arena *arena, const char *src, size_t len) {
    char *dst = (char *)arena_alloc(arena, len + 1, sizeof(char));
    if (!dst) return NULL;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

ConfigDocResult config_parse(Arena *arena, const char *text, size_t len) {
    if (!arena) {
        return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_INVALID_ARGUMENT, "arena is NULL"));
    }
    if (!text && len > 0) {
        return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_INVALID_ARGUMENT, "text is NULL"));
    }

    const char *p = text ? text : "";
    const char *end = p + len;

    size_t capacity = 32;
    size_t count = 0;
    ConfigEntry *entries = (ConfigEntry *)arena_alloc(arena, sizeof(ConfigEntry) * capacity, _Alignof(ConfigEntry));
    if (!entries && capacity > 0) {
        return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_OUT_OF_MEMORY, "failed allocating config entries"));
    }

    const char *current_section = "";
    int current_line = 1;

    while (p < end) {
        const char *line_start = p;
        while (p < end && *p != '\n') {
            p++;
        }
        const char *line_end = p;
        if (p < end && *p == '\n') {
            p++;
        }

        const char *s = skip_whitespace(line_start, line_end);
        const char *e = rtrim(s, line_end);

        if (s >= e || *s == '#' || *s == ';') {
            current_line++;
            continue;
        }

        if (*s == '[') {
            if (e[-1] != ']') {
                return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_INVALID_ARGUMENT, "unclosed section bracket"));
            }
            const char *sec_start = s + 1;
            const char *sec_end = e - 1;
            sec_start = skip_whitespace(sec_start, sec_end);
            sec_end = rtrim(sec_start, sec_end);

            if (sec_start >= sec_end) {
                return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_INVALID_ARGUMENT, "empty section name"));
            }

            current_section = arena_strndup(arena, sec_start, (size_t)(sec_end - sec_start));
            if (!current_section) {
                return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_OUT_OF_MEMORY, "failed to copy section name"));
            }
            current_line++;
            continue;
        }

        const char *eq = NULL;
        for (const char *scan = s; scan < e; ++scan) {
            if (*scan == '=') {
                eq = scan;
                break;
            }
        }

        if (!eq) {
            return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_INVALID_ARGUMENT, "missing '=' separator"));
        }

        const char *key_start = s;
        const char *key_end = rtrim(key_start, eq);
        if (key_start >= key_end) {
            return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_INVALID_ARGUMENT, "missing key before '='"));
        }

        const char *val_start = skip_whitespace(eq + 1, e);
        const char *val_end = e;

        if (val_start < val_end && ((*val_start == '"' && val_end[-1] == '"') ||
                                    (*val_start == '\'' && val_end[-1] == '\''))) {
            val_start++;
            val_end--;
        }

        char *key = arena_strndup(arena, key_start, (size_t)(key_end - key_start));
        char *val = arena_strndup(arena, val_start, (size_t)(val_end - val_start));
        if (!key || !val) {
            return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_OUT_OF_MEMORY, "failed to allocate key/value pair"));
        }

        if (count >= capacity) {
            size_t new_cap = capacity * 2;
            ConfigEntry *new_entries = (ConfigEntry *)arena_alloc(arena, sizeof(ConfigEntry) * new_cap, _Alignof(ConfigEntry));
            if (!new_entries) {
                return RESULT_ERR(ConfigDoc, ERROR_CREATE(ERR_OUT_OF_MEMORY, "failed to expand config entry list"));
            }
            memcpy(new_entries, entries, sizeof(ConfigEntry) * count);
            entries = new_entries;
            capacity = new_cap;
        }

        entries[count].section = current_section;
        entries[count].key = key;
        entries[count].value = val;
        entries[count].line = current_line;
        count++;

        current_line++;
    }

    ConfigDoc doc = {
        .entries = entries,
        .count = count
    };
    return RESULT_OK(ConfigDoc, doc);
}
