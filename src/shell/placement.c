#include "shell/placement.h"
#include <stdlib.h>

NorthBox placement_initial(NorthBox output_usable, NorthBox cursor_output,
                           const NorthBox *existing, size_t count, int w, int h) {
    NorthBox base = cursor_output;
    if (output_usable.width > 0 && output_usable.height > 0) {
        base = output_usable;
    }

    int x = base.x + (base.width - w) / 2;
    int y = base.y + (base.height - h) / 2;

    if (x < base.x) x = base.x;
    if (y < base.y) y = base.y;

    const int cascade_offset = 32;
    bool collision = true;
    while (collision) {
        collision = false;
        for (size_t i = 0; i < count; ++i) {
            if (existing[i].x == x && existing[i].y == y) {
                collision = true;
                x += cascade_offset;
                y += cascade_offset;
                if (x + w > base.x + base.width || y + h > base.y + base.height) {
                    x = base.x + 32;
                    y = base.y + 32;
                }
                break;
            }
        }
    }

    return (NorthBox){ .x = x, .y = y, .width = w, .height = h };
}

static int snap_axis(int val, int size, int target, int threshold) {
    if (abs(val - target) <= threshold) {
        return target;
    }
    if (abs(val + size - target) <= threshold) {
        return target - size;
    }
    return val;
}

NorthBox placement_snap(NorthBox moving, NorthBox output_usable,
                        const NorthBox *others, size_t count, int threshold) {
    NorthBox res = moving;

    if (output_usable.width > 0 && output_usable.height > 0) {
        res.x = snap_axis(res.x, res.width, output_usable.x, threshold);
        res.x = snap_axis(res.x, res.width, output_usable.x + output_usable.width, threshold);
        res.y = snap_axis(res.y, res.height, output_usable.y, threshold);
        res.y = snap_axis(res.y, res.height, output_usable.y + output_usable.height, threshold);
    }

    for (size_t i = 0; i < count; ++i) {
        const NorthBox *o = &others[i];
        if (res.y < o->y + o->height && res.y + res.height > o->y) {
            res.x = snap_axis(res.x, res.width, o->x, threshold);
            res.x = snap_axis(res.x, res.width, o->x + o->width, threshold);
        }
        if (res.x < o->x + o->width && res.x + res.width > o->x) {
            res.y = snap_axis(res.y, res.height, o->y, threshold);
            res.y = snap_axis(res.y, res.height, o->y + o->height, threshold);
        }
    }

    return res;
}

NorthBox placement_clamp_visible(NorthBox view, NorthBox output, int min_visible) {
    NorthBox res = view;
    if (output.width <= 0 || output.height <= 0) {
        return res;
    }

    if (res.x + res.width < output.x + min_visible) {
        res.x = output.x + min_visible - res.width;
    }
    if (res.x > output.x + output.width - min_visible) {
        res.x = output.x + output.width - min_visible;
    }

    if (res.y < output.y) {
        res.y = output.y;
    }
    if (res.y > output.y + output.height - min_visible) {
        res.y = output.y + output.height - min_visible;
    }

    return res;
}

float placement_default_scale(int width_mm, int width_px) {
    if (width_mm <= 0 || width_px <= 0) {
        return 1.0f;
    }

    float inches = (float)width_mm / 25.4f;
    float dpi = (float)width_px / inches;

    if (dpi >= 240.0f) {
        return 2.0f;
    }
    if (dpi >= 180.0f) {
        return 1.5f;
    }
    if (dpi >= 140.0f) {
        return 1.25f;
    }
    return 1.0f;
}
