#ifndef CONFIG_H
#define CONFIG_H

#include "input/keybind.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float r;
    float g;
    float b;
    float a;
} NorthColor;

typedef struct {
    const char *name;
    int width;
    int height;
    int refresh_mhz;
    float scale;
    int x;
    int y;
    bool enabled;
} NorthOutputConfig;

typedef struct {
    const char *layout;
    const char *variant;
    const char *options;
    int repeat_rate;
    int repeat_delay;
} NorthKeyboardConfig;

typedef struct {
    bool tap;
    bool natural_scroll;
    float accel_speed;
} NorthPointerConfig;

typedef struct {
    const char *font;
    int titlebar_height;
    int border_width;
    int shadow_radius;
    bool force_ssd;
    NorthColor active_title;
    NorthColor inactive_title;
    NorthColor active_border;
    NorthColor inactive_border;
} NorthDecorConfig;

typedef struct {
    bool enabled;
    int threshold;
    NorthColor preview_color;
    NorthColor preview_border_color;
    int preview_border_width;
} NorthSnapConfig;

typedef struct north_config {
    Arena arena;
    NorthKeyboardConfig keyboard;
    NorthPointerConfig pointer;
    NorthPointerConfig touchpad;
    NorthDecorConfig decor;
    NorthSnapConfig snap;

    NorthOutputConfig *outputs;
    size_t output_count;

    NorthKeybind *binds;
    size_t bind_count;
} NorthConfig;

DEFINE_RESULT(NorthColor, NorthColor)
DEFINE_RESULT(NorthConfig *, NorthConfigPtr)

NorthColorResult config_parse_color(const char *hex_str);
NorthConfig *config_create_default(void);
NorthConfigPtrResult config_load(const char *path);
void config_destroy(NorthConfig *config);

const NorthOutputConfig *config_find_output(const NorthConfig *config, const char *name);

#endif
