#pragma once
#include "geometry.h"

// The three conditional readings the dial cannot carry itself, each drawn into
// a box layout_compute() has already placed and sized.
//
// The countdown is a plate cut into the face at nine o'clock; nav and the
// warning share the notification band, which is the strip under the dial on the
// rectangles and a plate at six o'clock on the round display.
void slots_draw_count(GContext *ctx, const Layout *lo, GFont font, time_t now);
void slots_draw_nav(GContext *ctx, const Layout *lo, GFont font);
void slots_draw_warn(GContext *ctx, const Layout *lo, GFont font);
