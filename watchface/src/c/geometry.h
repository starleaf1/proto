#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// The timeline strip and the vertical layout.
//
// Every coordinate on this face derives from the root layer's bounds, so the
// same code lays out a 144x168 rectangle (flint), a 200x228 one (emery) and a
// 260x260 circle (gabbro). Only track_at() branches on display shape.
// ---------------------------------------------------------------------------

// The visible window: 1 h above the pointer and 3 h below it. The pointer
// therefore sits at a quarter of the track and never moves — the ruler slides
// past it, one minute at a time, and that is the whole of the "scrolling".
//
// The dial this replaced had to reason about wraparound: eight hours of a
// twelve-hour ring was the most that could be shown before a marker could be
// mistaken for one half a revolution away. A linear track cannot wrap, so the
// window is simply what stays legible at a 15-minute pitch and nothing more.
#define STRIP_BACK_S    (1 * 60 * 60)
#define STRIP_AHEAD_S   (3 * 60 * 60)
#define STRIP_SPAN_S    (STRIP_BACK_S + STRIP_AHEAD_S)
#define STRIP_SPAN_MIN  (STRIP_SPAN_S / 60)   // 240 — one coverage byte each
#define NOTCH_STEP_MIN  15

// Marker and pointer extents, as percentages of notch_len. They live here
// rather than in strip.c because layout_compute() has to reserve the room they
// claim before it can place a single text row.
#define MARKER_DEPTH_PCT   150   // one point marker, reaching in from the track
#define MARKER_GROUP_PCT   190   // a merged one: deeper, which is what says so
#define POINTER_LEN_PCT    200
#define POINTER_HALF_PCT   100   // half-extent along the track
#define POINTER_TIP_GAP      3   // clearance from the notch zone's inner edge

// Between the countdown's digits and the progress bar under them.
#define PROGRESS_GAP 2

// The face's fixed geometry, measured once per paint.
//
// The rows are pinned at both ends rather than flowed through the whole height:
// the clock's centre is pinned to the pointer, because a digital clock beside a
// stationary "now" marker that does not line up with it reads as two unrelated
// things, and the warnings row is pinned to the bottom. The date and the
// countdown flow down from the clock, and nav takes whatever is left between.
typedef struct {
  GRect   bounds;
  GPoint  center;
  int16_t radius;      // min(w, h) / 2 — the scale every other size derives from
  int16_t margin;
  int16_t notch_len;   // full notch; also the reference for every band depth
  int16_t zone;        // what the strip claims inward from the track, in px
  int16_t track_px;    // the track's length, for px <-> seconds conversions
#ifdef PBL_ROUND
  int16_t arc_r;       // the circle the strip's arc is traced on
#else
  int16_t strip_x;     // the track's x
  int16_t strip_top;
  int16_t strip_h;
#endif
  GRect   num_box;     // HH:MM, centred on the pointer
  GRect   date_box;
  GRect   count_box;   // countdown; always tall enough for the progress bar
  int16_t bar_h;       // the bar's own height, reserved whether it is drawn or not
  GRect   nav_box;
  GRect   warn_box;    // pinned to the bottom
} Layout;

Layout layout_compute(GRect bounds, GFont num_font, GFont date_font,
                      GFont slot_font);

// A point on the track, and the ray angle there.
//
// `a` is in the sense step_in() and step_side() take: step_in() moves from `p`
// inward, toward the content column, and step_side() moves along the track.
typedef struct { GPoint p; int32_t a; } Track;

// `u` is seconds from the top of the visible window, clamped to
// [0, STRIP_SPAN_S].
//
// On a rectangular display the track is the left edge and `a` is a constant
// 270 degrees. That is not a special case bolted on: a left-edge strip *is* the
// old dial's nine-o'clock ray, and at 270 degrees step_in() moves +x while
// step_side() moves +/-y, so every helper written for the ring works here
// untouched. On gabbro the track is an arc down the left of the circle and `a`
// sweeps with it.
Track track_at(const Layout *lo, int32_t u);

// Seconds from the top of the visible window. The replacement for the dial's
// deg_of_time(), and unlike it there is no modulo: the result may fall outside
// [0, STRIP_SPAN_S], which is exactly how a caller knows something is off-strip.
static inline int32_t u_of_time(time_t t, time_t now) {
  return (int32_t)(t - (now - STRIP_BACK_S));
}

// Convert a distance along the track into the seconds it covers.
int32_t u_of_px(const Layout *lo, int16_t px);

// A stroke width the display can actually draw, and the only kind that lands square on
// the pixel grid. Rounds down to odd.
//
// The SDK supports odd widths only: an even one is stored as given but the drawing
// routines round it down, so asking for 4 puts 3 on the screen and any arithmetic
// derived from the 4 — a cap overshoot, a rect inset — is describing a line that was
// never drawn. Odd is also what a stroke has to be to sit on whole pixels, since only
// an odd count can put the same number either side of the centre line; an even one
// straddles a boundary and the renderer has to pick a side.
//
// Down rather than up, so what reaches the display is unchanged and it is the source
// that stops overstating itself. Every graphics_context_set_stroke_width() on this face
// goes through here.
int16_t stroke_px(int16_t w);

// Move p inward along the ray at angle a by d px — away from the track, toward
// the content column.
//
// Every marker depth on this face is a count of pixels, never a fraction of
// anything. The dial needed a cosine correction on top of that, because a ray
// leaving a rectangle at an angle is not square to the edge it leaves through.
// The strip needs none: a circle's ray is its normal and a vertical edge's ray
// is too, so a depth in pixels is already perpendicular to the boundary
// everywhere on both shapes.
GPoint step_in(GPoint p, int32_t a, int32_t d);

// Move p along the track from the ray at angle a by d px. Signed, so +d and -d
// give the two base corners of a marker.
GPoint step_side(GPoint p, int32_t a, int32_t d);

// The tight, centred bounding box of one text row inside `box`, padded a little.
GRect text_plate(GRect box, GFont font, const char *text);

// Fill `r` with the background colour. Invisible over the background itself, so
// the only thing it does is cut away whatever a marker or band had already drawn
// there. Used behind every text row: the row names something specific and the
// marker it covers is still visible either side of it, so text wins the overlap.
void knock_out(GContext *ctx, GRect r);

// A triangle, optionally haloed. `halo` first strokes the outline in the
// background colour, wide and centred on the path, so 1px of it lands outside
// the shape — that ring is what keeps a marker readable where it overlaps a
// band drawn in the same ink.
void draw_tri(GContext *ctx, GPoint p0, GPoint p1, GPoint p2,
              GColor ink, bool filled, int16_t stroke_w, bool halo);

// A triangle with its base on the track at `u` and its apex pointing inward.
// Hollow shapes get a stroke of half_len/2, so the outline scales with the
// marker instead of vanishing to a hairline on the larger displays.
void draw_track_triangle(GContext *ctx, const Layout *lo, int32_t u,
                         int16_t depth, int16_t half_len,
                         GColor ink, bool filled, bool halo);
