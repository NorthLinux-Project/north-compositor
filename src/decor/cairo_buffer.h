#ifndef CAIRO_BUFFER_H
#define CAIRO_BUFFER_H

#include <cairo.h>
#include <wlr/interfaces/wlr_buffer.h>

struct north_cairo_buffer {
    struct wlr_buffer base;
    cairo_surface_t *surface;
    cairo_t *cairo;
};

struct north_cairo_buffer *north_cairo_buffer_create(int width, int height);

#endif
