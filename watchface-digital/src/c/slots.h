#pragma once
#include "geometry.h"

// The three conditional rows below the clock, in the order they stack.
void slots_draw_count(GContext *ctx, const Layout *lo, GFont font, time_t now);
void slots_draw_nav(GContext *ctx, const Layout *lo, GFont font);
void slots_draw_warn(GContext *ctx, const Layout *lo, GFont font);

// Whether slots_draw_warn() would draw anything. The strip's hour labels ask, because
// on gabbro their lane reaches into this row and a row that stays silent knocks
// nothing out of it.
bool slots_warn_active(void);
