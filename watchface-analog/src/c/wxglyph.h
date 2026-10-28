#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// The weather hazards, one icon each.
//
// Shared by copy with the other face, and identical there, which makes it the one
// shared file that knows what something looks like. The icons are bitmaps from
// resources/images/pebble_weather_icons/ — the same files and the same resource
// names in both faces, a black-and-white set on flint and a colour set on emery and
// gabbro — so the two faces cannot draw the same storm two ways.
//
// Bitmaps, where everything else on both faces is drawn from primitives, because
// these are pixel art at their native twelve pixels and do not survive scaling: the
// drawn shapes they replaced read on emery and not at all as small marks on gabbro,
// where a fifth of the radius made them complications rather than icons. So they are
// drawn at 1:1 on every display, WX_ICON_PX square, centred in whatever box the
// caller gives.
//
// Every hazard has one, ice included — it borrows the hail icon — and a hazard this
// watch predates gets the warning triangle.
// ---------------------------------------------------------------------------

#define WX_ICON_PX 12

void wx_glyph(GContext *ctx, GRect box, uint8_t hazard);

// Releases the icons, which are loaded on first use and kept. Call from deinit().
void wx_glyph_deinit(void);
