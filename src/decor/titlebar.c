#include "decor/decoration.h"
#include "decor/cairo_buffer.h"
#include "config/config.h"
#include <pango/pangocairo.h>
#include <string.h>

struct north_cairo_buffer *render_titlebar(int width, int height, float scale,
                                           const char *title, bool active, bool maximized,
                                           const NorthDecorConfig *decor_cfg) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    int px_width = (int)((float)width * scale);
    int px_height = (int)((float)height * scale);
    if (px_width <= 0 || px_height <= 0) {
        return NULL;
    }

    struct north_cairo_buffer *buf = north_cairo_buffer_create(px_width, px_height);
    if (!buf) {
        return NULL;
    }

    cairo_t *cr = buf->cairo;
    cairo_scale(cr, scale, scale);

    NorthColor bg = active ? decor_cfg->active_title : decor_cfg->inactive_title;
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    int btn_width = height;
    int btns_total_width = btn_width * 3;
    int text_max_width = width - btns_total_width - 16;
    if (text_max_width > 0) {
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_from_string(decor_cfg->font ? decor_cfg->font : "Sans 10");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);

        pango_layout_set_text(layout, title ? title : "", -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, text_max_width * PANGO_SCALE);

        int text_w, text_h;
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        int text_y = (height - text_h) / 2;

        if (active) {
            cairo_set_source_rgba(cr, 0.95, 0.95, 0.95, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.65, 0.65, 0.65, 1.0);
        }

        cairo_move_to(cr, 12, text_y);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }

    cairo_set_line_width(cr, 1.5);
    if (active) {
        cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 1.0);
    } else {
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 1.0);
    }

    int btn_close_x = width - btn_width;
    int cx = btn_close_x + btn_width / 2;
    int cy = height / 2;
    int s = 4;
    cairo_move_to(cr, cx - s, cy - s);
    cairo_line_to(cr, cx + s, cy + s);
    cairo_move_to(cr, cx + s, cy - s);
    cairo_line_to(cr, cx - s, cy + s);
    cairo_stroke(cr);

    int btn_max_x = btn_close_x - btn_width;
    cx = btn_max_x + btn_width / 2;
    cy = height / 2;
    if (maximized) {
        cairo_rectangle(cr, cx - s + 2, cy - s - 2, s * 2 - 2, s * 2 - 2);
        cairo_stroke(cr);
        cairo_rectangle(cr, cx - s, cy - s + 2, s * 2 - 2, s * 2 - 2);
        cairo_stroke(cr);
    } else {
        cairo_rectangle(cr, cx - s, cy - s, s * 2, s * 2);
        cairo_stroke(cr);
    }

    int btn_min_x = btn_max_x - btn_width;
    cx = btn_min_x + btn_width / 2;
    cy = height / 2;
    cairo_move_to(cr, cx - s, cy + s);
    cairo_line_to(cr, cx + s, cy + s);
    cairo_stroke(cr);

    return buf;
}
