#include "decor/decoration.h"

#include "core/server.h"
#include "shell/view.h"

#include <stdlib.h>
#include <string.h>

void decoration_init(struct north_decoration *decor, struct north_view *view)
{
    memset(decor, 0, sizeof(*decor));
    decor->view = view;
    decor->tree = wlr_scene_tree_create(view->tree);
    decor->enabled = true;
    decor->active = false;
    decor->scale = 1.0f;

    const float color[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    decor->border_top = wlr_scene_rect_create(decor->tree, 0, 0, color);
    decor->border_bottom = wlr_scene_rect_create(decor->tree, 0, 0, color);
    decor->border_left = wlr_scene_rect_create(decor->tree, 0, 0, color);
    decor->border_right = wlr_scene_rect_create(decor->tree, 0, 0, color);

    decor->border_top->node.data = (void *)(uintptr_t)NORTH_DECOR_PART_BORDER_TOP;
    decor->border_bottom->node.data = (void *)(uintptr_t)NORTH_DECOR_PART_BORDER_BOTTOM;
    decor->border_left->node.data = (void *)(uintptr_t)NORTH_DECOR_PART_BORDER_LEFT;
    decor->border_right->node.data = (void *)(uintptr_t)NORTH_DECOR_PART_BORDER_RIGHT;

    decor->titlebar_node = wlr_scene_buffer_create(decor->tree, NULL);
    decor->titlebar_node->node.data = (void *)(uintptr_t)NORTH_DECOR_PART_TITLEBAR;
}

void decoration_finish(struct north_decoration *decor)
{
    if (decor->rendered_titlebar) {
        wlr_buffer_drop(&decor->rendered_titlebar->base);
        decor->rendered_titlebar = NULL;
    }
    if (decor->title) {
        free(decor->title);
        decor->title = NULL;
    }
}

void decoration_set_enabled(struct north_decoration *decor, bool enabled)
{
    decor->enabled = enabled;
    wlr_scene_node_set_enabled(&decor->tree->node, enabled);
}

void decoration_set_title(struct north_decoration *decor, const char *title)
{
    if (decor->title) {
        free(decor->title);
        decor->title = NULL;
    }
    if (title) {
        decor->title = strdup(title);
    }
    decoration_update(decor, decor->width, decor->height, decor->scale);
}

void decoration_set_active(struct north_decoration *decor, bool active)
{
    decor->active = active;
    decoration_update(decor, decor->width, decor->height, decor->scale);
}

void decoration_update(struct north_decoration *decor, int width, int height, float scale)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    decor->width = width;
    decor->height = height;
    decor->scale = scale > 0.0f ? scale : 1.0f;

    NorthConfig *cfg = decor->view->server->config;
    const NorthDecorConfig *dcfg = &cfg->decor;

    bool maximized = (decor->view && decor->view->state == VIEW_MAXIMIZED);
    int tb_h = dcfg->titlebar_height;
    int bw = maximized ? 0 : dcfg->border_width;

    if (decor->rendered_titlebar) {
        wlr_buffer_drop(&decor->rendered_titlebar->base);
        decor->rendered_titlebar = NULL;
    }

    decor->rendered_titlebar =
        render_titlebar(width, tb_h, decor->scale, decor->title, decor->active, maximized, dcfg);
    if (decor->rendered_titlebar) {
        wlr_scene_buffer_set_buffer(decor->titlebar_node, &decor->rendered_titlebar->base);
        wlr_scene_buffer_set_dest_size(decor->titlebar_node, width, tb_h);
        wlr_scene_node_set_position(&decor->titlebar_node->node, 0, -tb_h);
    }

    NorthColor border_col = decor->active ? dcfg->active_border : dcfg->inactive_border;
    const float col[4] = {border_col.r, border_col.g, border_col.b, border_col.a};

    wlr_scene_rect_set_color(decor->border_top, col);
    wlr_scene_rect_set_size(decor->border_top, width + 2 * bw, bw);
    wlr_scene_node_set_position(&decor->border_top->node, -bw, -tb_h - bw);

    wlr_scene_rect_set_color(decor->border_bottom, col);
    wlr_scene_rect_set_size(decor->border_bottom, width + 2 * bw, bw);
    wlr_scene_node_set_position(&decor->border_bottom->node, -bw, height);

    wlr_scene_rect_set_color(decor->border_left, col);
    wlr_scene_rect_set_size(decor->border_left, bw, height + tb_h);
    wlr_scene_node_set_position(&decor->border_left->node, -bw, -tb_h);

    wlr_scene_rect_set_color(decor->border_right, col);
    wlr_scene_rect_set_size(decor->border_right, bw, height + tb_h);
    wlr_scene_node_set_position(&decor->border_right->node, width, -tb_h);
}

enum north_decor_part decoration_find_part(struct north_decoration *decor,
                                           struct wlr_scene_node *node, double lx, double ly)
{
    (void)ly;
    if (!decor || !decor->enabled || !node) {
        return NORTH_DECOR_PART_NONE;
    }

    if (node == &decor->titlebar_node->node) {
        int tb_h = decor->view->server->config->decor.titlebar_height;
        int btn_w = tb_h;
        int w = decor->width;

        if (lx >= w - btn_w && lx < w) {
            return NORTH_DECOR_PART_BUTTON_CLOSE;
        }
        if (lx >= w - 2 * btn_w && lx < w - btn_w) {
            return NORTH_DECOR_PART_BUTTON_MAXIMIZE;
        }
        if (lx >= w - 3 * btn_w && lx < w - 2 * btn_w) {
            return NORTH_DECOR_PART_BUTTON_MINIMIZE;
        }
        return NORTH_DECOR_PART_TITLEBAR;
    }

    if (node == &decor->border_top->node)
        return NORTH_DECOR_PART_BORDER_TOP;
    if (node == &decor->border_bottom->node)
        return NORTH_DECOR_PART_BORDER_BOTTOM;
    if (node == &decor->border_left->node)
        return NORTH_DECOR_PART_BORDER_LEFT;
    if (node == &decor->border_right->node)
        return NORTH_DECOR_PART_BORDER_RIGHT;

    return NORTH_DECOR_PART_NONE;
}
