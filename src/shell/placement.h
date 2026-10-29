#ifndef PLACEMENT_H
#define PLACEMENT_H

#include "util/geom.h"
#include <stddef.h>

NorthBox placement_initial(NorthBox output_usable, NorthBox cursor_output,
                           const NorthBox *existing, size_t count, int w, int h);

NorthBox placement_snap(NorthBox moving, NorthBox output_usable,
                        const NorthBox *others, size_t count, int threshold);

NorthBox placement_clamp_visible(NorthBox view, NorthBox output, int min_visible);

float placement_default_scale(int width_mm, int width_px);

#endif
