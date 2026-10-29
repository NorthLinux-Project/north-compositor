#ifndef DECORATION_H
#define DECORATION_H

#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "decor/cairo_buffer.h"
#include "config/config.h"

enum north_decor_part {
    NORTH_DECOR_PART_NONE = 0,
    NORTH_DECOR_PART_TITLEBAR,
    NORTH_DECOR_PART_BUTTON_CLOSE,
    NORTH_DECOR_PART_BUTTON_MAXIMIZE,
    NORTH_DECOR_PART_BUTTON_MINIMIZE,
    NORTH_DECOR_PART_BORDER_TOP,
    NORTH_DECOR_PART_BORDER_BOTTOM,
    NORTH_DECOR_PART_BORDER_LEFT,
    NORTH_DECOR_PART_BORDER_RIGHT
};

struct north_view;

struct north_decoration {
    struct north_view *view;
    struct wlr_scene_tree *tree;

    struct wlr_scene_buffer *titlebar_node;
    struct wlr_scene_rect *border_top;
    struct wlr_scene_rect *border_bottom;
    struct wlr_scene_rect *border_left;
    struct wlr_scene_rect *border_right;

    struct north_cairo_buffer *rendered_titlebar;

    bool enabled;
    bool active;
    int width;
    int height;
    float scale;
    char *title;
};

void decoration_init(struct north_decoration *decor, struct north_view *view);
void decoration_finish(struct north_decoration *decor);
void decoration_update(struct north_decoration *decor, int width, int height, float scale);
void decoration_set_active(struct north_decoration *decor, bool active);
void decoration_set_title(struct north_decoration *decor, const char *title);
void decoration_set_enabled(struct north_decoration *decor, bool enabled);

enum north_decor_part decoration_find_part(struct north_decoration *decor, struct wlr_scene_node *node, double lx, double ly);

struct north_cairo_buffer *render_titlebar(int width, int height, float scale,
                                           const char *title, bool active, bool maximized,
                                           const NorthDecorConfig *decor_cfg);

#endif
