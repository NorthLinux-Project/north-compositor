#include "shell/switcher.h"

#include "core/server.h"
#include "decor/cairo_buffer.h"
#include "output/output.h"
#include "shell/view.h"
#include "shell/workspace.h"
#include "util/log.h"

#include <cairo.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    if (r * 2.0 > w) {
        r = w / 2.0;
    }
    if (r * 2.0 > h) {
        r = h / 2.0;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

static bool view_is_valid(struct north_server *server, struct north_view *view)
{
    if (!server || !view) {
        return false;
    }
    struct north_view *v;
    wl_list_for_each(v, &server->views, link)
    {
        if (v == view) {
            return true;
        }
    }
    return false;
}

static struct north_output *switcher_get_target_output(struct north_server *server)
{
    struct north_output *out = NULL;
    if (server->seat.cursor) {
        out = output_at(server, server->seat.cursor->x, server->seat.cursor->y);
    }
    if (!out && !wl_list_empty(&server->views)) {
        struct north_view *v;
        wl_list_for_each(v, &server->views, link)
        {
            if (v->workspace && v->workspace->output) {
                out = v->workspace->output;
                break;
            }
        }
    }
    if (!out && !wl_list_empty(&server->outputs)) {
        out = wl_container_of(server->outputs.next, out, link);
    }
    return out;
}

static const char *view_get_display_title(struct north_view *view)
{
    if (!view) {
        return "Window";
    }
    if (view->decor.title && view->decor.title[0] != '\0') {
        return view->decor.title;
    }
    if (view->type == NORTH_VIEW_XDG && view->xdg_toplevel) {
        if (view->xdg_toplevel->title && view->xdg_toplevel->title[0] != '\0') {
            return view->xdg_toplevel->title;
        }
        if (view->xdg_toplevel->app_id && view->xdg_toplevel->app_id[0] != '\0') {
            return view->xdg_toplevel->app_id;
        }
    }
    if (view->type == NORTH_VIEW_XWAYLAND && view->xwayland_surface) {
        if (view->xwayland_surface->title && view->xwayland_surface->title[0] != '\0') {
            return view->xwayland_surface->title;
        }
        if (view->xwayland_surface->class && view->xwayland_surface->class[0] != '\0') {
            return view->xwayland_surface->class;
        }
    }
    return "Window";
}

void switcher_init(struct north_server *server)
{
    struct north_switcher *switcher = &server->switcher;
    memset(switcher, 0, sizeof(*switcher));
    switcher->server = server;
    switcher->tree = wlr_scene_tree_create(server->layers.overlay);
    switcher->scene_buffer = wlr_scene_buffer_create(switcher->tree, NULL);
    wlr_scene_node_set_enabled(&switcher->tree->node, false);
}

void switcher_finish(struct north_server *server)
{
    struct north_switcher *switcher = &server->switcher;
    anim_cancel(&switcher->fade_anim);
    if (switcher->active) {
        switcher_cancel(server);
    }
    if (switcher->cairo_buf) {
        wlr_scene_buffer_set_buffer(switcher->scene_buffer, NULL);
        wlr_buffer_drop(&switcher->cairo_buf->base);
        switcher->cairo_buf = NULL;
    }
    if (switcher->tree) {
        wlr_scene_node_destroy(&switcher->tree->node);
        switcher->tree = NULL;
        switcher->scene_buffer = NULL;
    }
    free(switcher->items);
    switcher->items = NULL;
    switcher->count = 0;
    switcher->capacity = 0;
}

void switcher_show(struct north_server *server, int direction)
{
    if (!server) {
        return;
    }
    struct north_switcher *switcher = &server->switcher;
    if (switcher->active) {
        switcher_cycle(server, direction);
        return;
    }

    struct north_output *out = switcher_get_target_output(server);
    if (!out || !out->active_workspace) {
        return;
    }

    switcher->count = 0;
    struct north_view *v;
    wl_list_for_each(v, &server->views, link)
    {
        if (v->workspace == out->active_workspace && v->surface != NULL && v->surface->mapped) {
            if (switcher->count >= switcher->capacity) {
                size_t new_cap = switcher->capacity == 0 ? 8 : switcher->capacity * 2;
                struct north_view **new_items =
                    realloc(switcher->items, new_cap * sizeof(*new_items));
                if (!new_items) {
                    return;
                }
                switcher->items = new_items;
                switcher->capacity = new_cap;
            }
            switcher->items[switcher->count++] = v;
        }
    }

    if (switcher->count <= 1) {
        if (switcher->count == 1 && switcher->items[0] &&
            view_is_valid(server, switcher->items[0])) {
            view_focus(switcher->items[0]);
        }
        switcher->active = false;
        return;
    }

    struct north_view *focused = NULL;
    size_t start_idx = 0;
    struct wlr_surface *focused_surf =
        server->seat.wlr_seat ? server->seat.wlr_seat->keyboard_state.focused_surface : NULL;
    if (focused_surf) {
        for (size_t i = 0; i < switcher->count; i++) {
            if (switcher->items[i]->surface == focused_surf) {
                focused = switcher->items[i];
                start_idx = i;
                break;
            }
        }
    }
    if (!focused) {
        focused = switcher->items[0];
        start_idx = 0;
    }
    switcher->original_focus = focused;
    switcher->output = out;
    switcher->active = true;

    if (direction > 0) {
        switcher->selected_index = (start_idx + (size_t)direction) % switcher->count;
    } else if (direction < 0) {
        long idx = (long)start_idx + direction;
        while (idx < 0) {
            idx += (long)switcher->count;
        }
        switcher->selected_index = (size_t)idx % switcher->count;
    } else {
        switcher->selected_index = start_idx;
    }

    wlr_scene_node_set_enabled(&switcher->tree->node, true);
    switcher_render(switcher);
    anim_switcher_fade(server, true);
}

void switcher_cycle(struct north_server *server, int direction)
{
    if (!server) {
        return;
    }
    struct north_switcher *switcher = &server->switcher;
    if (!switcher->active || switcher->count == 0) {
        return;
    }

    if (direction != 0) {
        long new_idx = ((long)switcher->selected_index + direction) % (long)switcher->count;
        if (new_idx < 0) {
            new_idx += (long)switcher->count;
        }
        switcher->selected_index = (size_t)new_idx;
    }

    switcher_render(switcher);
}

void switcher_commit(struct north_server *server)
{
    if (!server) {
        return;
    }
    struct north_switcher *switcher = &server->switcher;
    if (!switcher->active) {
        return;
    }

    switcher->active = false;
    anim_cancel(&switcher->fade_anim);
    server->switcher_just_committed = true;
    wlr_scene_node_set_enabled(&switcher->tree->node, false);

    struct north_view *target = NULL;
    if (switcher->count > 0 && switcher->selected_index < switcher->count) {
        target = switcher->items[switcher->selected_index];
    }

    if (switcher->cairo_buf) {
        wlr_scene_buffer_set_buffer(switcher->scene_buffer, NULL);
        wlr_buffer_drop(&switcher->cairo_buf->base);
        switcher->cairo_buf = NULL;
    }

    switcher->output = NULL;
    switcher->original_focus = NULL;
    switcher->count = 0;

    if (target && view_is_valid(server, target)) {
        view_focus(target);
    }
}

void switcher_cancel(struct north_server *server)
{
    if (!server) {
        return;
    }
    struct north_switcher *switcher = &server->switcher;
    if (!switcher->active) {
        return;
    }

    switcher->active = false;
    anim_cancel(&switcher->fade_anim);
    server->switcher_just_committed = false;
    wlr_scene_node_set_enabled(&switcher->tree->node, false);

    struct north_view *orig = switcher->original_focus;

    if (switcher->cairo_buf) {
        wlr_scene_buffer_set_buffer(switcher->scene_buffer, NULL);
        wlr_buffer_drop(&switcher->cairo_buf->base);
        switcher->cairo_buf = NULL;
    }

    switcher->output = NULL;
    switcher->original_focus = NULL;
    switcher->count = 0;

    if (orig && view_is_valid(server, orig)) {
        view_focus(orig);
    }
}

void switcher_notify_view_unmap(struct north_server *server, struct north_view *view)
{
    if (!server || !server->switcher.active || !view) {
        return;
    }
    struct north_switcher *sw = &server->switcher;
    if (sw->original_focus == view) {
        sw->original_focus = NULL;
    }
    for (size_t i = 0; i < sw->count; i++) {
        if (sw->items[i] == view) {
            for (size_t j = i; j + 1 < sw->count; j++) {
                sw->items[j] = sw->items[j + 1];
            }
            sw->count--;
            if (sw->selected_index >= sw->count && sw->count > 0) {
                sw->selected_index = sw->count - 1;
            }
            break;
        }
    }
    if (sw->count <= 1) {
        switcher_cancel(server);
    } else {
        switcher_render(sw);
    }
}

bool switcher_handle_click(struct north_server *server, double lx, double ly)
{
    if (!server) {
        return false;
    }
    struct north_switcher *switcher = &server->switcher;
    if (!switcher->active || switcher->count == 0) {
        return false;
    }

    int pad = 20;
    int gap = 16;
    int card_w = switcher->card_width;
    int card_h = switcher->card_height;

    for (size_t i = 0; i < switcher->count; i++) {
        int cx = switcher->hud_x + pad + (int)i * (card_w + gap);
        int cy = switcher->hud_y + pad;
        if (lx >= cx && lx < cx + card_w && ly >= cy && ly < cy + card_h) {
            switcher->selected_index = i;
            switcher_commit(server);
            return true;
        }
    }

    switcher_cancel(server);
    return true;
}

void switcher_render(struct north_switcher *switcher)
{
    if (!switcher || !switcher->active || switcher->count == 0) {
        return;
    }

    struct wlr_box out_box = {0};
    if (switcher->output && switcher->server->output_layout) {
        wlr_output_layout_get_box(switcher->server->output_layout, switcher->output->wlr_output,
                                  &out_box);
    }
    if (out_box.width <= 0 || out_box.height <= 0) {
        if (switcher->output && switcher->output->wlr_output) {
            out_box.width = switcher->output->wlr_output->width;
            out_box.height = switcher->output->wlr_output->height;
        }
    }
    if (out_box.width <= 0) {
        out_box.width = 1920;
    }
    if (out_box.height <= 0) {
        out_box.height = 1080;
    }

    int card_w = 200;
    int card_h = 130;
    int pad = 20;
    int gap = 16;

    int max_w = out_box.width - 80;
    int total_w = 2 * pad + (int)switcher->count * card_w + ((int)switcher->count - 1) * gap;
    if (total_w > max_w && switcher->count > 0) {
        int avail_w = max_w - 2 * pad;
        if (avail_w > 0) {
            gap = (int)switcher->count > 1 ? avail_w / ((int)switcher->count * 6) : 16;
            if (gap < 4) {
                gap = 4;
            }
            card_w = (avail_w - ((int)switcher->count - 1) * gap) / (int)switcher->count;
            if (card_w < 60) {
                card_w = 60;
            }
            total_w = 2 * pad + (int)switcher->count * card_w + ((int)switcher->count - 1) * gap;
        }
    }

    int hud_w = total_w;
    int hud_h = 2 * pad + card_h;
    int hud_x = out_box.x + (out_box.width - hud_w) / 2;
    int hud_y = out_box.y + (out_box.height - hud_h) / 2;
    if (hud_x < out_box.x) {
        hud_x = out_box.x;
    }
    if (hud_y < out_box.y) {
        hud_y = out_box.y;
    }

    switcher->hud_x = hud_x;
    switcher->hud_y = hud_y;
    switcher->hud_width = hud_w;
    switcher->hud_height = hud_h;
    switcher->card_width = card_w;
    switcher->card_height = card_h;

    float scale = (switcher->output && switcher->output->wlr_output &&
                   switcher->output->wlr_output->scale > 0.0f)
                      ? switcher->output->wlr_output->scale
                      : 1.0f;

    int px_w = (int)((float)hud_w * scale + 0.999f);
    int px_h = (int)((float)hud_h * scale + 0.999f);
    if (px_w <= 0 || px_h <= 0) {
        return;
    }

    struct north_cairo_buffer *buf = north_cairo_buffer_create(px_w, px_h);
    if (!buf) {
        return;
    }

    cairo_t *cr = buf->cairo;
    cairo_scale(cr, scale, scale);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_rounded_rect(cr, 0.5, 0.5, hud_w - 1.0, hud_h - 1.0, 14.0);
    cairo_set_source_rgba(cr, 0.10, 0.12, 0.16, 0.88);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.30, 0.34, 0.42, 0.50);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    for (size_t i = 0; i < switcher->count; i++) {
        double cx = pad + (int)i * (card_w + gap);
        double cy = pad;
        double cw = card_w;
        double ch = card_h;
        bool selected = (i == switcher->selected_index);

        draw_rounded_rect(cr, cx, cy, cw, ch, 8.0);
        if (selected) {
            cairo_set_source_rgba(cr, 0.20, 0.24, 0.32, 0.95);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.36, 0.62, 0.98, 1.0);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
        } else {
            cairo_set_source_rgba(cr, 0.14, 0.16, 0.22, 0.70);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.24, 0.28, 0.36, 0.40);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        double prev_pad = 10.0;
        double prev_w = cw - 2 * prev_pad;
        double prev_h = ch - 50.0;
        double prev_x = cx + prev_pad;
        double prev_y = cy + prev_pad;

        draw_rounded_rect(cr, prev_x, prev_y, prev_w, prev_h, 4.0);
        if (selected) {
            cairo_set_source_rgba(cr, 0.12, 0.15, 0.22, 0.90);
        } else {
            cairo_set_source_rgba(cr, 0.09, 0.11, 0.15, 0.60);
        }
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.28, 0.32, 0.40, 0.35);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        double ic_w = 28.0;
        double ic_h = 20.0;
        double ic_x = prev_x + (prev_w - ic_w) / 2.0;
        double ic_y = prev_y + (prev_h - ic_h) / 2.0;
        draw_rounded_rect(cr, ic_x, ic_y, ic_w, ic_h, 2.0);
        if (selected) {
            cairo_set_source_rgba(cr, 0.40, 0.65, 0.95, 0.80);
        } else {
            cairo_set_source_rgba(cr, 0.45, 0.50, 0.60, 0.50);
        }
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);

        cairo_move_to(cr, ic_x, ic_y + 5.0);
        cairo_line_to(cr, ic_x + ic_w, ic_y + 5.0);
        cairo_stroke(cr);

        const char *title = view_get_display_title(switcher->items[i]);
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_from_string("Sans 10");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        pango_layout_set_text(layout, title ? title : "", -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        pango_layout_set_width(layout, (int)((cw - 16.0) * PANGO_SCALE));

        int text_w, text_h;
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        double text_x = cx + 8.0;
        double text_y = cy + ch - 30.0 + (24.0 - text_h) / 2.0;

        if (selected) {
            cairo_set_source_rgba(cr, 0.98, 0.98, 0.98, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.70, 0.74, 0.80, 1.0);
        }

        cairo_move_to(cr, text_x, text_y);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }

    wlr_scene_buffer_set_buffer(switcher->scene_buffer, &buf->base);
    wlr_scene_buffer_set_dest_size(switcher->scene_buffer, hud_w, hud_h);
    wlr_scene_node_set_position(&switcher->tree->node, hud_x, hud_y);

    if (switcher->cairo_buf) {
        wlr_buffer_drop(&switcher->cairo_buf->base);
    }
    switcher->cairo_buf = buf;
}
