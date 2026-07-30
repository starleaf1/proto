#pragma once
#include <pebble.h>
#include "geometry.h"

// Appointment bands, the 60 notches over them, and the point-in-time markers over
// those. Call before the text rows.
void dial_draw(GContext *ctx, const Layout *lo, time_t now);

// The hour wedge. Call last of everything — see the note in dial.c.
void dial_draw_now(GContext *ctx, const Layout *lo, const struct tm *t);
