#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stddef.h>
#include "util/arena.h"
#include "util/error.h"

typedef struct {
    const char *section;
    const char *key;
    const char *value;
    int line;
} ConfigEntry;

typedef struct {
    ConfigEntry *entries;
    size_t count;
} ConfigDoc;

DEFINE_RESULT(ConfigDoc, ConfigDoc)

ConfigDocResult config_parse(Arena *arena, const char *text, size_t len);

#endif
