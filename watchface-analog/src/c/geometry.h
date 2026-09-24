#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// The dial, the ring, and the disc cut into the middle of it.
//
// Every coordinate derives from the root layer's bounds, so the same code lays
// out a 144x168 rectangle (flint), a 200x228 one (emery) and a 260x260 circle
// (gabbro). On the rectangles the dial is the full screen width and flush to
// the top, and the strip of screen left under it is the notification band. On
// the round display the dial is the whole glass, so the notification area moves
// inside it — the disc's bottom row, which is one slot where the band is two.
//
// Everything here is polar. There is one centre, one set of radii and one
// angular scale, and that scale is the clock's own: this face has no linear
// track and no second mapping to keep in step.
// ---------------------------------------------------------------------------

// The visible window: 1 h behind and 5 h ahead. Six hours on a twelve-hour dial
// is exactly half a turn, 30 degrees to the hour and half a degree to the
// minute — the hour hand's own rate, which is the whole reason the markers can
// be anchored to the dial instead of carrying a scale of their own.
//
// Six is also what fits without wrapping. Twelve hours is a full revolution, so
// anything past that would put two different times on one angle; at six there
// is no am/pm ambiguity anywhere inside what is drawn.
#define RING_BACK_S    (1 * 60 * 60)
#define RING_AHEAD_S   (5 * 60 * 60)
#define RING_SPAN_S    (RING_BACK_S + RING_AHEAD_S)
#define RING_SPAN_MIN  (RING_SPAN_S / 60)   // 360 — one coverage byte each

// A full turn of the dial, in minutes. Half a day, because the hands read a
// twelve-hour clock and the ring reads the same dial the hands do.
#define DIAL_SPAN_MIN  720

// Between the disc's outermost ink and its border.
#define PLATE_PAD 3

// The gap between a row's glyph and its number, as a fraction of the glyph's own
// side. slot_layout() draws it and layout_compute() budgets the plate against it,
// so it lives here rather than in either.
//
// The countdown used to get half of this, to pay for the '+' or '-' its number
// began with. The sign is gone, and with it the one hazard a narrow gap raised: a
// task wedge hard against a minus is a maneuver arrow.
#define SLOT_GAP_DIV  3
#define SLOT_GAP(side) ((side) / SLOT_GAP_DIV)

// A text row's box is taller than the ink in it. Pebble font resources carry their own
// ascent, and an uppercase-and-digits subset never descends below the baseline, so a
// row reserved at the measured height keeps a fifth of itself as slack above the
// glyphs — which is invisible on its own and compounds into a disc a size larger than
// its contents once three rows stack.
//
// So a row reserves seven eighths of what it measures and draws a quarter of it higher,
// which puts the ink where the reservation says it is.
//
// Both numbers are measured off a screenshot rather than derived, and both moved when
// the fonts became the firmware's: in a 14 px GOTHIC_14 box the digits ink rows 5 to 13,
// so *all* of the slack is above them — a system Gothic puts its baseline on the box's
// last row where the Rajdhani resource this replaced kept 0.07 of the height below it.
// With nothing below to share the correction, the lift that centres the ink in a
// shortened row is half the slack: a quarter, against the three sixteenths the TTF
// wanted. Three sixteenths under Gothic left the ink a pixel and a half low in every
// row of the disc at once, which is the kind of error that reads as the disc being
// off-centre rather than as the text being.
//
// The reservation did not move. Seven eighths still clears the slack, and shortening it
// further is not free: the notification rows size their glyph from the row height, and
// the glyph has a floor under which its outline goes to a hairline. See glyph_stroke().
#define ROW_H(h)    ((h) * 7 / 8)
#define ROW_LIFT(h) ((h) / 4)

// The inside margin of the running countdown's filled box, left and right of the
// glyph and the digits, as a fraction of the measured text height. layout_compute()
// reserves it in both states, so the row is the same width whether or not the box
// draws and nothing in it moves when an appointment starts.
#define COUNT_PAD(h) ((h) / 6)

// How the notification reading sits in its row. Side by side on the rectangles,
// where the band under the dial is one wide row and the two read as a line;
// centred on the round display, where they are not two readings at all but one
// row holding whichever of them outranks the rest, and a lone reading in a
// circle belongs on its centre line.
#define NAV_ALIGN  PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft)
#define WARN_ALIGN PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight)

// The face's fixed geometry, measured once per paint.
//
// The radii are a ladder from the rim inward, and the order is load-bearing:
// the ring carries the markers, the tick lane carries the clock, and `r_text`
// is the line past which nothing may be drawn that a hand would have to cross
// — it is what sizes the two plates.
typedef struct {
  GRect   bounds;
  GPoint  center;      // the dial's centre, not the screen's, on the rectangles
  int16_t radius;      // the dial's radius — the scale every size derives from
  int16_t margin;

  // The ring: markers only, and the one lane the hands are allowed to enter.
  int16_t r_out;       // outer edge
  int16_t ring_d;      // depth
  int16_t r_in;        // inner edge
  int16_t r_apex;      // how far in a task wedge's apex reaches past r_in

  // The dial proper.
  int16_t tick_out;    // outer end of the tick lane
  int16_t tick_maj;    // 12/3/6/9
  int16_t tick_min;    // the other eight
  int16_t r_text;      // nothing outside this is safe from a hand

  int16_t hand_h_len;  // hour hand: flush with the ticks' outer end, never into the ring
  int16_t hand_m_len;  // minute hand: to the ring's outer edge
  int16_t hand_h_half; // half-widths at the hub, in px
  int16_t hand_m_half;
  int16_t hand_tail;   // how far each hand runs behind the centre
  int16_t hub_r;

  // The plate: one disc at the centre, drawn over the hands.
  //
  // Over, not under, and that is the whole reason it is here. Text anywhere
  // else on this face is text a hand crosses twice an hour; at the centre it
  // covers the part of both hands that carries no reading — their tips are what
  // point at things — so the hands lose nothing by passing behind it. It is
  // also the one shape that cannot collide with the ring at any hour, having no
  // hour of its own.
  int16_t plate_r;
  GRect   date_box;    // the weekday and the day of the month, one line
  GRect   count_box;   // the countdown, signed; a plain row of the disc
  GRect   nav_box;
  GRect   warn_box;    // the same rect as nav_box where band_inset: one slot, not two
  bool    band_inset;  // true where the notification reading is a row of the disc
} Layout;

Layout layout_compute(GRect bounds, GFont date_font, GFont slot_font);

// ---------------------------------------------------------------------------
// Angles
// ---------------------------------------------------------------------------

// Where a wall-clock time sits on the dial, in Pebble trig units, measured
// clockwise from twelve.
//
// Computed straight into trig units with no intermediate degrees: a whole
// degree is two minutes of the ring and 2.2 px of gabbro's rim, which is a
// rounding a reader would see. (mins % 720) * TRIG_MAX_ANGLE tops out at
// 47 million, so the product stays inside int32 and the division is exact
// enough to place a minute.
int32_t angle_of_time(time_t t);

// The same conversion for a duration rather than an instant. Never reduced mod
// a turn: a caller adding this to a base angle needs the result to keep
// increasing, because that is what tells graphics_fill_radial which way round
// to sweep.
int32_t angle_of_span(int32_t minutes);

// How much angle a distance along the rim covers, at radius r. The polar
// equivalent of the strip's u_of_px().
int32_t angle_of_px(int16_t px, int16_t r);

// Minutes of the dial covered by an angle — angle_of_span() run backwards.
int32_t span_of_angle(int32_t a);

// Step from p by d px along the ray at angle a. Signed, so -d is the opposite
// direction: the hands place their tails that way, and their base corners by
// stepping a quarter turn off their own angle.
GPoint point_step(GPoint p, int32_t a, int32_t d);

// Point at radius r and angle a from c. point_step() from the centre.
GPoint point_on_circle(GPoint c, int32_t r, int32_t a);

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

// A stroke width the display can actually draw, and the only kind that lands
// square on the pixel grid. Rounds down to odd.
//
// The SDK supports odd widths only: an even one is stored as given but the
// drawing routines round it down, so asking for 4 puts 3 on the screen and any
// arithmetic derived from the 4 is describing a line that was never drawn. Odd
// is also what a stroke has to be to sit on whole pixels. Every
// graphics_context_set_stroke_width() on this face goes through here.
int16_t stroke_px(int16_t w);

// An annular sector of the ring: everything between angles a0 and a1, filled
// `depth` px inward from radius `r_outer`.
//
// a1 must be the later of the two. Time runs *clockwise* with increasing dial
// angle here, which is the opposite of the digital face's arc, where the angle
// decreased down the strip — so the earlier end is the one to sweep from.
// graphics_fill_radial draws nothing at all if the start is the larger, which
// is a blank ring rather than an error.
//
// A span that crosses twelve o'clock is drawn as two fills. The window is half
// a turn and sits wherever the hour hand is, so it straddles the boundary for
// six hours out of every twelve; relying on the SDK to normalise an angle past
// a full turn would make a third of the day's renders a guess.
void fill_ring_band(GContext *ctx, const Layout *lo, int32_t a0, int32_t a1,
                    int16_t r_outer, int16_t depth, GColor col);

// A 1px arc at radius r, same angular contract and the same boundary split.
void draw_ring_arc(GContext *ctx, const Layout *lo, int32_t a0, int32_t a1,
                   int16_t r, GColor col);

// A radial line from r0 out to r1 at angle a.
void draw_spoke(GContext *ctx, const Layout *lo, int32_t a,
                int16_t r0, int16_t r1, int16_t w, GColor col);

// A filled convex polygon of 3 or 4 points, optionally haloed.
//
// The halo is the same shape grown HALO_PX away from its own centroid, filled
// in the background colour and then covered by the shape — which leaves a ring
// of about that width around it and never more. The obvious version, a
// background-coloured *outline* stroked wider than the shape, is a trap: a
// stroked path miters its corners and the miter at a sharp vertex runs far past
// the vertex itself, far enough to cut a background-coloured slot clean through
// a band. Growing the vertices bounds the halo by construction, at any angle
// and any sharpness. Both hands and every task wedge depend on it.
void fill_poly(GContext *ctx, GPoint *pts, int n, GColor ink, bool halo);

// A filled triangle. The glyph library in slots.c is built out of these — arrow
// heads, the task wedge, the roundabout's exit — and nothing on this face draws a
// hollow one: the digital face's only outlined shape was flint's upcoming marker,
// and here that distinction is carried by the band's depth instead.
void draw_tri(GContext *ctx, GPoint p0, GPoint p1, GPoint p2, GColor ink, bool halo);

// A wedge off the ring: base `half_a` either side of `a` at radius `r_base`,
// tapering to `tip_a` either side at `r_apex`.
//
// Blunt-tipped, and that is not a detail. A wedge whose depth is about three
// times its half-base tapers below two pixels for its inner third and draws
// that third as a hairline thinner than the dial's own minute ticks. The apex
// is the end that carries the time, so the shape was least visible exactly
// where it was most load-bearing.
void ring_wedge(GContext *ctx, const Layout *lo, int32_t a,
                int32_t half_a, int32_t tip_a,
                int16_t r_base, int16_t r_apex, GColor ink, bool halo);

// The plate: a background disc with an ink rim, drawn after the hands.
//
// The hands run behind it and emerge at the rim, which is the classic sector
// dial and the only arrangement that keeps text legible at every minute on a
// display with a single ink. The rim is what makes the hands read as passing
// under rather than as having been cut.
void draw_disc(GContext *ctx, GPoint c, int16_t r);

// Half the chord of a circle of radius r at a vertical offset of dy from its
// centre. What decides how wide each row of the disc is allowed to be.
int16_t chord_half(int16_t r, int16_t dy);
