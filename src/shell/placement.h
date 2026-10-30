#ifndef PLACEMENT_H
#define PLACEMENT_H

#include "util/geom.h"

#include <stddef.h>

enum north_snap_zone {
    SNAP_NONE = 0,
    SNAP_LEFT,
    SNAP_RIGHT,
    SNAP_TOP,
    SNAP_TOP_LEFT,
    SNAP_TOP_RIGHT,
    SNAP_BOTTOM_LEFT,
    SNAP_BOTTOM_RIGHT,
};

NorthBox placement_initial(NorthBox output_usable, NorthBox cursor_output, const NorthBox *existing,
                           size_t count, int w, int h);

NorthBox placement_snap(NorthBox moving, NorthBox output_usable, const NorthBox *others,
                        size_t count, int threshold);

NorthBox placement_clamp_visible(NorthBox view, NorthBox output, int min_visible);

float placement_default_scale(int width_mm, int width_px);

enum north_snap_zone placement_detect_snap_zone(NorthBox usable, double cx, double cy,
                                                int threshold);

NorthBox placement_calculate_snap_box(NorthBox usable, enum north_snap_zone zone,
                                      int titlebar_height);

#endif
