#include "config/config.h"
#include "config/parser.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

NorthColorResult config_parse_color(const char *hex_str) {
    if (!hex_str) {
        return RESULT_ERR(NorthColor, ERROR_CREATE(ERR_INVALID_ARGUMENT, "hex_str is NULL"));
    }
    if (hex_str[0] == '#') {
        hex_str++;
    }

    size_t len = strlen(hex_str);
    if (len != 6 && len != 8) {
        return RESULT_ERR(NorthColor, ERROR_CREATE(ERR_INVALID_ARGUMENT, "color must be 6 or 8 hex digits"));
    }

    unsigned int r = 0, g = 0, b = 0, a = 255;
    if (sscanf(hex_str, "%02x%02x%02x", &r, &g, &b) != 3) {
        return RESULT_ERR(NorthColor, ERROR_CREATE(ERR_INVALID_ARGUMENT, "failed parsing hex color"));
    }
    if (len == 8) {
        if (sscanf(hex_str + 6, "%02x", &a) != 1) {
            return RESULT_ERR(NorthColor, ERROR_CREATE(ERR_INVALID_ARGUMENT, "failed parsing alpha channel"));
        }
    }

    NorthColor color = {
        .r = (float)r / 255.0f,
        .g = (float)g / 255.0f,
        .b = (float)b / 255.0f,
        .a = (float)a / 255.0f
    };
    return RESULT_OK(NorthColor, color);
}

static void apply_defaults(NorthConfig *cfg) {
    cfg->outputs = NULL;
    cfg->output_count = 0;
    cfg->binds = NULL;
    cfg->bind_count = 0;

    cfg->keyboard.layout = "us";
    cfg->keyboard.variant = "";
    cfg->keyboard.options = "";
    cfg->keyboard.repeat_rate = 25;
    cfg->keyboard.repeat_delay = 600;

    cfg->pointer.accel_speed = 0.0f;
    cfg->pointer.natural_scroll = false;
    cfg->pointer.tap = false;

    cfg->touchpad.accel_speed = 0.0f;
    cfg->touchpad.natural_scroll = true;
    cfg->touchpad.tap = true;

    cfg->decor.font = "Sans 10";
    cfg->decor.titlebar_height = 28;
    cfg->decor.border_width = 1;
    cfg->decor.shadow_radius = 18;
    cfg->decor.force_ssd = false;

    NorthColorResult c_at = config_parse_color("#1e2230ff");
    if (c_at.ok) cfg->decor.active_title = c_at.value;
    NorthColorResult c_it = config_parse_color("#2a2e3aff");
    if (c_it.ok) cfg->decor.inactive_title = c_it.value;
    NorthColorResult c_ab = config_parse_color("#3b4252ff");
    if (c_ab.ok) cfg->decor.active_border = c_ab.value;
    NorthColorResult c_ib = config_parse_color("#2e3440ff");
    if (c_ib.ok) cfg->decor.inactive_border = c_ib.value;
}

NorthConfig *config_create_default(void) {
    Arena arena = arena_create(1024 * 512);
    NorthConfig *cfg = arena_push_type(&arena, NorthConfig);
    if (!cfg) {
        arena_destroy(&arena);
        return NULL;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->arena = arena;
    apply_defaults(cfg);

    size_t default_bind_cap = 8;
    cfg->binds = arena_push_array(&cfg->arena, NorthKeybind, default_bind_cap);
    cfg->bind_count = 0;

    const char *default_specs[][2] = {
        { "Super+Return", "exec foot" },
        { "Super+Q", "close" },
        { "Super+Up", "maximize" },
        { "Super+F", "fullscreen" },
        { "Alt+Tab", "focus_next" },
        { "Super+Shift+R", "reload" },
        { "Super+Shift+E", "exit" }
    };

    for (size_t i = 0; i < sizeof(default_specs) / sizeof(default_specs[0]); ++i) {
        NorthKeybindResult kr = keybind_parse(&cfg->arena, default_specs[i][0], default_specs[i][1]);
        if (kr.ok) {
            cfg->binds[cfg->bind_count++] = kr.value;
        }
    }

    return cfg;
}

NorthConfigPtrResult config_load(const char *path) {
    char default_path[512];
    if (!path) {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        const char *home = getenv("HOME");
        if (xdg) {
            snprintf(default_path, sizeof(default_path), "%s/north/compositor.conf", xdg);
        } else if (home) {
            snprintf(default_path, sizeof(default_path), "%s/.config/north/compositor.conf", home);
        } else {
            return RESULT_OK(NorthConfigPtr, config_create_default());
        }
        path = default_path;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_INFO("Config file '%s' not found, using built-in defaults", path);
        return RESULT_OK(NorthConfigPtr, config_create_default());
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0 || sz > 1024 * 1024) {
        fclose(f);
        return RESULT_ERR(NorthConfigPtr, ERROR_CREATE(ERR_INVALID_ARGUMENT, "invalid config file size"));
    }

    Arena arena = arena_create(1024 * 512 + (size_t)sz * 2);
    char *buf = (char *)arena_alloc(&arena, (size_t)sz + 1, sizeof(char));
    if (!buf) {
        fclose(f);
        arena_destroy(&arena);
        return RESULT_ERR(NorthConfigPtr, ERROR_CREATE(ERR_OUT_OF_MEMORY, "failed to allocate buffer for config file"));
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[read_bytes] = '\0';

    ConfigDocResult doc_res = config_parse(&arena, buf, read_bytes);
    if (!doc_res.ok) {
        arena_destroy(&arena);
        return RESULT_ERR(NorthConfigPtr, doc_res.err);
    }

    ConfigDoc doc = doc_res.value;
    NorthConfig *cfg = arena_push_type(&arena, NorthConfig);
    if (!cfg) {
        arena_destroy(&arena);
        return RESULT_ERR(NorthConfigPtr, ERROR_CREATE(ERR_OUT_OF_MEMORY, "failed allocating config struct"));
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->arena = arena;
    apply_defaults(cfg);

    size_t bind_cap = 64;
    cfg->binds = arena_push_array(&cfg->arena, NorthKeybind, bind_cap);
    cfg->bind_count = 0;

    size_t out_cap = 16;
    cfg->outputs = arena_push_array(&cfg->arena, NorthOutputConfig, out_cap);
    cfg->output_count = 0;

    for (size_t i = 0; i < doc.count; ++i) {
        ConfigEntry *e = &doc.entries[i];

        if (strcmp(e->section, "input:keyboard") == 0) {
            if (strcmp(e->key, "layout") == 0) cfg->keyboard.layout = e->value;
            else if (strcmp(e->key, "variant") == 0) cfg->keyboard.variant = e->value;
            else if (strcmp(e->key, "options") == 0) cfg->keyboard.options = e->value;
            else if (strcmp(e->key, "repeat_rate") == 0) cfg->keyboard.repeat_rate = atoi(e->value);
            else if (strcmp(e->key, "repeat_delay") == 0) cfg->keyboard.repeat_delay = atoi(e->value);
        } else if (strcmp(e->section, "input:touchpad") == 0) {
            if (strcmp(e->key, "tap") == 0) cfg->touchpad.tap = (strcmp(e->value, "true") == 0);
            else if (strcmp(e->key, "natural_scroll") == 0) cfg->touchpad.natural_scroll = (strcmp(e->value, "true") == 0);
            else if (strcmp(e->key, "accel_speed") == 0) cfg->touchpad.accel_speed = (float)atof(e->value);
        } else if (strcmp(e->section, "input:pointer") == 0) {
            if (strcmp(e->key, "natural_scroll") == 0) cfg->pointer.natural_scroll = (strcmp(e->value, "true") == 0);
            else if (strcmp(e->key, "accel_speed") == 0) cfg->pointer.accel_speed = (float)atof(e->value);
        } else if (strcmp(e->section, "decoration") == 0) {
            if (strcmp(e->key, "font") == 0) cfg->decor.font = e->value;
            else if (strcmp(e->key, "titlebar_height") == 0) cfg->decor.titlebar_height = atoi(e->value);
            else if (strcmp(e->key, "border_width") == 0) cfg->decor.border_width = atoi(e->value);
            else if (strcmp(e->key, "shadow_radius") == 0) cfg->decor.shadow_radius = atoi(e->value);
            else if (strcmp(e->key, "force_ssd") == 0) cfg->decor.force_ssd = (strcmp(e->value, "true") == 0);
            else if (strcmp(e->key, "active_title") == 0) {
                NorthColorResult cr = config_parse_color(e->value);
                if (cr.ok) cfg->decor.active_title = cr.value;
            } else if (strcmp(e->key, "inactive_title") == 0) {
                NorthColorResult cr = config_parse_color(e->value);
                if (cr.ok) cfg->decor.inactive_title = cr.value;
            } else if (strcmp(e->key, "active_border") == 0) {
                NorthColorResult cr = config_parse_color(e->value);
                if (cr.ok) cfg->decor.active_border = cr.value;
            } else if (strcmp(e->key, "inactive_border") == 0) {
                NorthColorResult cr = config_parse_color(e->value);
                if (cr.ok) cfg->decor.inactive_border = cr.value;
            }
        } else if (strncmp(e->section, "output:", 7) == 0) {
            const char *out_name = e->section + 7;
            NorthOutputConfig *out = NULL;
            for (size_t o = 0; o < cfg->output_count; ++o) {
                if (strcmp(cfg->outputs[o].name, out_name) == 0) {
                    out = &cfg->outputs[o];
                    break;
                }
            }
            if (!out && cfg->output_count < out_cap) {
                out = &cfg->outputs[cfg->output_count++];
                memset(out, 0, sizeof(*out));
                out->name = out_name;
                out->scale = 1.0f;
                out->enabled = true;
            }
            if (out) {
                if (strcmp(e->key, "mode") == 0) {
                    int w = 0, h = 0, hz = 60;
                    if (sscanf(e->value, "%dx%d@%d", &w, &h, &hz) >= 2) {
                        out->width = w;
                        out->height = h;
                        out->refresh_mhz = hz * 1000;
                    }
                } else if (strcmp(e->key, "scale") == 0) {
                    out->scale = (float)atof(e->value);
                } else if (strcmp(e->key, "position") == 0) {
                    sscanf(e->value, "%d,%d", &out->x, &out->y);
                } else if (strcmp(e->key, "enabled") == 0) {
                    out->enabled = (strcmp(e->value, "true") == 0);
                }
            }
        } else if (strcmp(e->section, "bind") == 0) {
            if (cfg->bind_count < bind_cap) {
                NorthKeybindResult kr = keybind_parse(&cfg->arena, e->key, e->value);
                if (kr.ok) {
                    cfg->binds[cfg->bind_count++] = kr.value;
                } else {
                    LOG_WARN("Failed to parse keybind '%s = %s': %s",
                             e->key, e->value, kr.err.frames[0].context);
                }
            }
        }
    }

    return RESULT_OK(NorthConfigPtr, cfg);
}

void config_destroy(NorthConfig *config) {
    if (config) {
        Arena a = config->arena;
        arena_destroy(&a);
    }
}

const NorthOutputConfig *config_find_output(const NorthConfig *config, const char *name) {
    if (!config || !name) return NULL;
    for (size_t i = 0; i < config->output_count; ++i) {
        if (strcmp(config->outputs[i].name, name) == 0) {
            return &config->outputs[i];
        }
    }
    return NULL;
}
