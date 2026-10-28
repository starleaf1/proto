#pragma once
#include "geometry.h"

// The three conditional readings the dial cannot carry itself, each drawn into
// a box layout_compute() has already placed and sized.
//
// The countdown is a row of the central disc. Nav and the warning share the
// notification area, which is the strip of screen under the dial on the
// rectangles — two readings abreast, and both can show at once — and a single
// row of the disc on the round display, where the two contend for one slot and
// the maneuver wins. See slots_draw_warn().
//
// Severe weather joins the pair from WX_NEAR_S before its onset until it ends,
// below the maneuver and above both batteries. See wx_at() in slots.c.
void slots_draw_count(GContext *ctx, const Layout *lo, time_t now);
void slots_draw_nav(GContext *ctx, const Layout *lo, GFont font, time_t now);
void slots_draw_warn(GContext *ctx, const Layout *lo, GFont font, time_t now);
