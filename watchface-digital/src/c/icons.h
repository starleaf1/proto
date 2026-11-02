#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// The rows' glyphs: hand-drawn bitmaps, one per glyph per display.
//
// Shared by copy with the other face, and identical there. The resource *names* are
// the same in both faces and the files behind them are not — each face's rows are their
// own height, so each face carries its own sizes, under
// resources/images/icons/<platform>/slot/. Pixel art does not survive scaling, so every
// icon is drawn at its own size, centred in whatever box the caller gives; the files
// are drawn at exactly the size the row's glyph box comes to.
//
// What the face does not draw is mirrored here rather than carried. The right turns
// are the left ones mirrored, always. The roundabouts and the u-turn are drawn for
// left-hand traffic — clockwise, and swinging right — and right-hand traffic is their
// mirror image. The exit number is drawn on top afterwards, so it is never mirrored
// with them.
// ---------------------------------------------------------------------------

// The digits are pixel art drawn at 1:1, and their size is the bitmap's own: 3 x 5 on
// flint, which is all that fits inside that display's thick ring, and 5 x 7 on the
// colour displays, which fits the colour art's island with a pixel to spare except at
// its diagonals. The colour set was the flint one doubled first, and ten pixels of
// digit landed on the route drawn around the island, so the icon read as a crosshair.
// The scale stays a constant, and whole, so the art stays crisp if it ever changes.
// MAX is what the mask holds; larger art is cut to it.
#define ICON_DIGIT_MAX_W 8
#define ICON_DIGIT_MAX_H 9
#define ICON_DIGIT_SCALE 1

// `res` at its own size, centred in `box`, mirrored left for right when asked.
void icon_draw(GContext *ctx, GRect box, uint32_t res, bool mirror);

// The current maneuver, from wire: the turn, mirrored as the traffic side asks, and
// on a roundabout the exit number centred on the icon.
void icon_nav(GContext *ctx, GRect box);

// Releases every loaded icon. Call from deinit().
void icons_deinit(void);
