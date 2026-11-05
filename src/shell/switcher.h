#ifndef SHELL_SWITCHER_H
#define SHELL_SWITCHER_H

#include "anim/animator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct north_cairo_buffer;
struct north_output;
struct north_server;
struct north_view;
struct wlr_scene_buffer;
struct wlr_scene_tree;

struct north_switcher {
    struct north_server *server;
    struct wlr_scene_tree *tree;
    struct wlr_scene_buffer *scene_buffer;
    struct north_cairo_buffer *cairo_buf;
    struct north_animator fade_anim;

    bool active;
    uint32_t trigger_mods;
    struct north_output *output;
    struct north_view **items;
    size_t count;
    size_t capacity;
    size_t selected_index;
    struct north_view *original_focus;

    int hud_x;
    int hud_y;
    int hud_width;
    int hud_height;
    int card_width;
    int card_height;
};

void switcher_init(struct north_server *server);
void switcher_finish(struct north_server *server);
void switcher_show(struct north_server *server, int direction);
void switcher_cycle(struct north_server *server, int direction);
void switcher_commit(struct north_server *server);
void switcher_cancel(struct north_server *server);
bool switcher_handle_click(struct north_server *server, double lx, double ly);
void switcher_render(struct north_switcher *switcher);
void switcher_notify_view_unmap(struct north_server *server, struct north_view *view);

#endif
