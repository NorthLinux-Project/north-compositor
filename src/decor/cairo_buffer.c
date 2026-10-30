#include "decor/cairo_buffer.h"

#include <drm_fourcc.h>
#include <stdlib.h>

static void cairo_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
    struct north_cairo_buffer *buf = wl_container_of(wlr_buffer, buf, base);
    if (buf->cairo) {
        cairo_destroy(buf->cairo);
    }
    if (buf->surface) {
        cairo_surface_destroy(buf->surface);
    }
    wlr_buffer_finish(&buf->base);
    free(buf);
}

static bool cairo_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
                                               void **data, uint32_t *format, size_t *stride)
{
    (void)flags;
    struct north_cairo_buffer *buf = wl_container_of(wlr_buffer, buf, base);
    *data = cairo_image_surface_get_data(buf->surface);
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)cairo_image_surface_get_stride(buf->surface);
    return true;
}

static void cairo_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) { (void)wlr_buffer; }

static const struct wlr_buffer_impl cairo_buffer_impl = {
    .destroy = cairo_buffer_destroy,
    .begin_data_ptr_access = cairo_buffer_begin_data_ptr_access,
    .end_data_ptr_access = cairo_buffer_end_data_ptr_access,
};

struct north_cairo_buffer *north_cairo_buffer_create(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    struct north_cairo_buffer *buf = calloc(1, sizeof(*buf));
    if (!buf) {
        return NULL;
    }

    buf->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(buf->surface) != CAIRO_STATUS_SUCCESS) {
        free(buf);
        return NULL;
    }

    buf->cairo = cairo_create(buf->surface);
    if (cairo_status(buf->cairo) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(buf->surface);
        free(buf);
        return NULL;
    }

    wlr_buffer_init(&buf->base, &cairo_buffer_impl, width, height);
    return buf;
}
