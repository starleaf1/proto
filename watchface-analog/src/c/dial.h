#pragma once
#include <pebble.h>
#include "geometry.h"

// The ring and the dial under it: the rail spanning the visible window, the
// appointment bands, the task wedges, and the twelve hour ticks.
void dial_draw(GContext *ctx, const Layout *lo, time_t now);

// The hands, drawn over all of it.
//
// Not last, which is the one rule this face inverts from the digital one.
// There, "now" was a mark of its own and nothing was allowed to cover it. Here
// now *is* the hour hand, and the two plates have to outrank it — a hand
// crossing the date twice an hour is the problem the plates exist to solve.
void dial_draw_hands(GContext *ctx, const Layout *lo, const struct tm *t);
