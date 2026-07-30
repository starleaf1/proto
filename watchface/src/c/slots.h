#pragma once
#include <pebble.h>
#include "geometry.h"

// The two single-line slots inside the dial. Each shows at most one thing, and
// each resolves its own priority order — see ../../docs/protocol.md.
void slots_draw_top(GContext *ctx, const Layout *lo, GFont font);
void slots_draw_bottom(GContext *ctx, const Layout *lo, GFont font, time_t now);
