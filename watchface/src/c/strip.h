#pragma once
#include <pebble.h>
#include "geometry.h"

// Appointment bands, the quarter-hour notches over them, and the point-in-time
// markers over those. Call before the text rows.
//
// `tick` is for the hour labels outboard of the track, and it is the only font the
// strip draws in.
void strip_draw(GContext *ctx, const Layout *lo, time_t now, GFont tick);

// The "now" pointer. Call last of everything — see the note in strip.c.
void strip_draw_now(GContext *ctx, const Layout *lo);
