#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// The timeline strip and the vertical layout.
//
// Every coordinate on this face derives from the root layer's bounds, so the
// same code lays out a 144x168 rectangle (flint), a 200x228 one (emery) and a
// 260x260 circle (gabbro). Only layout_compute() and fit_row() branch on display
// shape: on the circle the strip is a chord, placed so the clock fits beside it.
// ---------------------------------------------------------------------------

// The window: 1 h above the pointer and 3 h below it on the rectangles, 1.5 h and 3.5 h on
// gabbro. The pointer never moves — the ruler slides past it, one minute at a time, and
// that is the whole of the "scrolling". Four hours is what stays legible down a straight
// strip at a 15-minute pitch.
//
// gabbro's track runs the full height of the display, not of the glass, so the circle
// cuts both ends off and the ruler reads as carrying on past the edge rather than as a
// bar that stops short of it. Five hours over that height is about the rectangles'
// pitch, and the pointer at three tenths of it sits where the clock fits beside it;
// what the glass actually shows at the strip's x is roughly an hour back and three
// ahead, the rectangles' reading.
//
// The dial this replaced had to reason about wraparound: eight hours of a
// twelve-hour ring was the most that could be shown before a marker could be
// mistaken for one half a revolution away. A line has no ends to meet.
#define STRIP_BACK_S    PBL_IF_ROUND_ELSE(90 * 60, 1 * 60 * 60)
#define STRIP_AHEAD_S   PBL_IF_ROUND_ELSE(210 * 60, 3 * 60 * 60)
#define STRIP_SPAN_S    (STRIP_BACK_S + STRIP_AHEAD_S)
// 240, or 300 on gabbro — one coverage byte each. PBL_IF_ROUND_ELSE is a preprocessor
// selection, so this stays an integer constant expression and is legal as an array bound.
#define STRIP_SPAN_MIN  (STRIP_SPAN_S / 60)
#define NOTCH_STEP_MIN  15

// Marker and pointer extents, as percentages of notch_len. They live here
// rather than in strip.c because layout_compute() has to reserve the room they
// claim before it can place a single text row.
//
// The pointer constants are flint's alone. Where there is colour, "now" is a red rule
// struck across the strip instead of a wedge beside it — see strip_draw_now. A rule
// cannot work on flint: it would be a black line across a ruler made of black lines,
// which is an extra notch and nothing else. Colour is what makes the simpler shape
// legible, so colour is where it is used.
//
// The wedge's base still sets `zone` on flint, and the hour labels cost that platform
// nothing at all: their lane fits inside the free area the wedge already claimed, between
// the notch zone and the wedge's own tip gap. What it costs instead is the label the
// wedge lands on — see draw_hour_label.
#define MARKER_DEPTH_PCT   150   // one point marker, reaching in from the track
#define MARKER_GROUP_PCT   190   // a merged one: deeper, which is what says so
#define POINTER_LEN_PCT    200
#define POINTER_HALF_PCT   100   // half-extent along the track
#define POINTER_TIP_GAP      3   // clearance from the notch zone's inner edge

// The "now" rule's thickness, as a fraction of the radius. Thicker than an hour notch
// on both colour displays, because it has to read as a different kind of thing from the
// ruler it crosses — hue says that too, but hue is not a shape.
#define RULE_W_DIV 18

// The gap between the hour labels' lane and whatever sits outboard of it — the notch
// zone on flint, the "now" rule's inner end on colour — is `margin`, not a constant.
// So is the clearance that closes `zone` on the inboard side, against the text column.
//
// It was a literal 3 on both counts, and a literal is the one thing a length on this
// face may not be: three pixels is a third of flint's notch on a 144 px display and a
// fifth of gabbro's on a 260 px one, so the lane got proportionally tighter exactly
// where there was most room. Measured on emery, "10" and the T of "THU 27" came out two
// pixels apart and the hour numbers read as the text column's first glyph rather than
// as graduations of the ruler. `margin` is 3/4/5 and is already the face's unit of "far
// enough apart to be separate things"; on flint it is the same 3 this used to be, so
// the display the old constant was tuned against does not move.
//
// It has to clear an hour notch, which is the *thicker* kind, and the halo a marker in
// the same lane carries — see layout_compute.

// How the text rows sit in their boxes: left, against the strip, on every display.
// A column of rows sharing a left edge reads as one block, and the edge is the strip's,
// which is what every row is beside. On gabbro that edge is still constant — the strip
// is a vertical line there too — and only the right edge is a chord.
#define ROW_ALIGN GTextAlignmentLeft

// The margin of the running countdown's filled box on every side of the glyph and
// the digits, as a fraction of the measured text height. The row's content is set in
// by it on the leading side in both states, so the box's edge, not the glyph, is what
// lines up with the date's column. Reaching back past that column instead put the
// box hard against the hour label beside it on emery and gabbro.
#define COUNT_PAD(h) ((h) / 6)

// The face's fixed geometry, measured once per paint.
//
// The rows are pinned at both ends rather than flowed through the whole height:
// the clock's centre is pinned to the pointer, because a digital clock beside a
// stationary "now" marker that does not line up with it reads as two unrelated
// things, and the warnings row is pinned to the bottom. The date and the
// countdown flow down from the clock, and nav takes whatever is left between.
//
// On gabbro "the bottom" is the lowest height where the chord still leaves a usable
// row, and the strip's x is solved rather than fixed — see layout_compute.
typedef struct {
  GRect   bounds;
  GPoint  center;
  int16_t radius;      // min(w, h) / 2 — the scale every other size derives from
  int16_t margin;
  int16_t notch_len;   // full notch; also the reference for every band depth
  // Depths inward from the track, and the order of them is not the same on both kinds
  // of display, because the "now" mark is not the same element.
  //
  // Colour:  notch zone | rule, over the markers | hour labels        | zone
  // flint:   notch zone | hour labels, then the wedge over them both  | zone
  //
  // On colour the rule is only as deep as the strip's own elements, so the labels go
  // past it and past the markers with it, and nothing is ever over or under a number.
  // On flint they go *between* the ruler and the wedge — inside the free area the wedge
  // already claimed, so they cost that platform no width whatever. What they share that
  // area with they deal with two different ways: a marker they cover, having a plate and
  // being drawn later, and the wedge covers them, being drawn last of everything, so the
  // one label the wedge would land on is dropped instead.
  int16_t label_x;
  int16_t label_w;
#ifdef PBL_COLOR
  int16_t rule_len;    // the "now" rule: exactly the depth it has to strike through
#else
  int16_t ptr_tip;     // the wedge's apex, POINTER_TIP_GAP past the label lane
#endif
  int16_t zone;
  int16_t track_px;    // the track's length, for px <-> seconds conversions
  int16_t strip_x;     // the track's x; left of centre on gabbro, by as much as the clock needs
  int16_t strip_top;
  int16_t strip_h;
  GRect   num_box;     // HH:MM, level with the pointer
  GRect   date_box;
  GRect   count_box;   // countdown; a plain row, sized like any other slot
  GRect   nav_box;
  GRect   warn_box;    // pinned to the bottom
} Layout;

// tick_font is here for one number: the width of "00" in it is the label lane, and
// the lane moves the track, which moves everything else.
Layout layout_compute(GRect bounds, GFont num_font, GFont date_font,
                      GFont slot_font, GFont tick_font);

// A point on the track, and the ray angle there.
//
// `a` is in the sense step_in() and step_side() take: step_in() moves from `p`
// inward, toward the content column, and step_side() moves along the track.
typedef struct { GPoint p; int32_t a; } Track;

// `u` is seconds from the top of the visible window, clamped to
// [0, STRIP_SPAN_S].
//
// The track is a vertical line and `a` is a constant 270 degrees. That is not a
// special case bolted on: a left-edge strip *is* the old dial's nine-o'clock ray, and
// at 270 degrees step_in() moves +x while step_side() moves +/-y, so every helper
// written for the ring works here untouched.
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

// A band on the track: everything between `u0` and `u1` (seconds from the top of the
// visible window), filled `depth` px inward from the track, ending square on the ray at
// each end and reaching one pixel outboard of the track.
//
// Square ends are the reason this is a fill and not a stroke. A band used to be one thick
// line per covered minute, and a thick line's caps are semicircles, so the first and last
// sample of a run kept a cap nothing overlapped and every band lost a pixel off all four
// corners. Pebble has no cap style to set, so the answer is to stop drawing a line.
//
// A filled rect, cut square on the ray so a band's end lines up with whatever else sits
// at the same `u`.
//
// The colour displays' "now" rule goes through here too. It is the same thing — a span of
// the track, filled to a depth — and it had the same lozenge ends for the same reason.
void fill_track_band(GContext *ctx, const Layout *lo, int32_t u0, int32_t u1,
                     int16_t depth, GColor col);

// The tight bounding box of one text row's ink inside `box`, padded a little.
// Follows ROW_ALIGN, because a plate that stayed centred while the text moved left
// would knock out the wrong pixels and leave a marker crossing the digits.
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

// A point marker: a wedge off the track at `u`, `depth` px deep, `half_len` of track
// either side of it at the base and `tip_half` either side at the inner end.
//
// Blunt-tipped, and that is not a detail. A triangle whose depth is about three times
// its half-base — which is what every marker on this face is — tapers below two pixels
// for its inner third and draws that third as a hairline thinner than the ruler's own
// minute notches. The apex is the end that carries the time, so the shape was least
// visible exactly where it was most load-bearing. `tip_half` stops the taper.
//
// A merged marker is passed a wider `half_len` and a wider `tip_half` as well as more
// depth — see draw_markers. Depth alone was a 25% difference and unreadable without the
// single-marker case beside it to compare against; blunting alone took the taper out and
// left a rectangle.
void draw_track_wedge(GContext *ctx, const Layout *lo, int32_t u,
                      int16_t depth, int16_t half_len, int16_t tip_half,
                      GColor ink, bool halo);
