#include "dial.h"
#include "events.h"
#include "theme.h"
#include <string.h>

// ---------------------------------------------------------------------------
// The ring is a 12-hour timeline.
//
// Everything the markers have to do — span a duration, flatten where they
// overlap, sit behind the notches, and read at two levels of prominence on a
// display with one ink — falls out of a single representation: one byte per
// degree of the dial, holding the most prominent thing happening at that
// degree. Overlapping appointments merge because they write the same array, and
// max() makes a merged band inherit the more urgent member's weight.
//
// Drawing is then one pass per layer, outward to inward in z-order: bands,
// notches, markers, hour index.
//
// The array is file-scope rather than automatic. A Pebble app's stack is small
// and this is 360 bytes that would otherwise be live across four calls.
// ---------------------------------------------------------------------------

#define COV_NONE 0
#define COV_SOON 1
#define COV_NOW  2

static uint8_t s_cov[360];      // appointment bands, by dial degree
static uint8_t s_task_n[60];    // point markers per notch — count, for grouping
static uint8_t s_task_w[60];    // ...and weight, for prominence

// A band this thin would otherwise vanish; a 15-minute appointment is 7 degrees,
// so this only ever rescues something shorter than about four minutes.
#define BAND_MIN_DEG 2

// How much of the notch zone's depth each band fills. A running one used to take all
// of it and an upcoming one half, which was too heavy: at that weight the ring reads
// as a fat coloured arc with ticks on it rather than as a dial carrying a marker, and
// the step down where a running appointment ends and an overlapping upcoming one
// carries on is a cliff rather than a change of weight.
//
// The ratio between them is what has to survive, not the absolute depth. Depth, not
// density, is what separates the two: the first attempt hatched the upcoming band — a
// 1px radial line every other degree — which is a textbook half-tone and completely
// wrong here, because the notches are also 1px radial lines, so a hatched band read as
// nothing more than a patch of extra ticks. A solid arc of reduced depth is a different
// *shape* from a tick, which is what makes it legible even on the platform with one ink.
#define BAND_NOW_PCT  70
#define BAND_SOON_PCT 35

static void build_coverage(time_t now) {
  memset(s_cov, COV_NONE, sizeof s_cov);
  const Event *tbl = events_table();

  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &tbl[i];
    if (!event_visible(e, now) || !event_is_long(e)) continue;

    // Clip to the live window at both ends. A meeting that began three hours
    // ago draws from now - 2 h, which keeps the whole painted span inside 8 h
    // and so inside half a revolution of the dial.
    time_t s = e->start, en = event_end(e);
    if (s  < now - WINDOW_BACK_S)  s  = now - WINDOW_BACK_S;
    if (en > now + WINDOW_AHEAD_S) en = now + WINDOW_AHEAD_S;
    if (en < s) continue;

    uint8_t w = event_prominent(e, now) ? COV_NOW : COV_SOON;
    int d0 = deg_of_time(s);
    int span = (deg_of_time(en) - d0 + 360) % 360;
    if (span < BAND_MIN_DEG) span = BAND_MIN_DEG;

    for (int k = 0; k <= span; k++) {
      int d = (d0 + k) % 360;
      if (s_cov[d] < w) s_cov[d] = w;
    }
  }
}

static void build_tasks(time_t now) {
  memset(s_task_n, 0, sizeof s_task_n);
  memset(s_task_w, 0, sizeof s_task_w);
  const Event *tbl = events_table();

  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &tbl[i];
    if (!event_visible(e, now) || event_is_long(e)) continue;

    // Snap to the nearest of the 60 notches. Each notch is 6 degrees, so this
    // quantises to 12 minutes — which is also exactly the resolution at which
    // two entries become indistinguishable, and therefore what "clustered too
    // closely" means here. Same notch, same marker.
    int tick = ((deg_of_time(e->start) + 3) / 6) % 60;
    if (s_task_n[tick] < 255) s_task_n[tick]++;
    uint8_t w = event_prominent(e, now) ? COV_NOW : COV_SOON;
    if (s_task_w[tick] < w) s_task_w[tick] = w;
  }
}

// Fold runs of neighbouring occupied notches into the middle one.
//
// Snapping to a notch quantises to 12 minutes, but it does not follow that two
// entries within 12 minutes share a notch — six minutes apart is three degrees,
// which straddles a notch boundary about half the time and lands them on two.
// And a marker's base is wider than the six-degree notch pitch on all three
// displays (8px against 7.2 on flint, 14 against 13.1 on gabbro), so neighbours
// always overlap into a smear. If they cannot be drawn apart they are one marker.
static void merge_adjacent(void) {
  uint8_t n[60], w[60];
  memcpy(n, s_task_n, sizeof n);
  memcpy(w, s_task_w, sizeof w);

  int occupied = 0;
  for (int i = 0; i < 60; i++) {
    if (n[i]) occupied++;
  }
  if (occupied == 0 || occupied == 60) return;   // nothing to do, or no run start

  memset(s_task_n, 0, sizeof s_task_n);
  memset(s_task_w, 0, sizeof s_task_w);

  for (int i = 0; i < 60; i++) {
    if (!n[i] || n[(i + 59) % 60]) continue;     // only start at a run's first notch
    int count = 0, weight = 0, len = 0;
    for (int k = 0; k < 60; k++) {
      int j = (i + k) % 60;
      if (!n[j]) break;
      count += n[j];
      if (w[j] > weight) weight = w[j];
      len++;
    }
    int mid = (i + len / 2) % 60;
    s_task_n[mid] = count > 255 ? 255 : (uint8_t)count;
    s_task_w[mid] = (uint8_t)weight;
  }
}

static void draw_bands(GContext *ctx, const Layout *lo) {
  // Rounded, not truncated. These are single-digit pixel counts on flint, where losing
  // most of a pixel to integer division is a tenth of the band.
  int16_t now_depth  = (lo->tick_len * BAND_NOW_PCT  + 50) / 100;
  int16_t soon_depth = (lo->tick_len * BAND_SOON_PCT + 50) / 100;
  if (now_depth < 3) now_depth = 3;
  if (soon_depth < 2) soon_depth = 2;

  // One radial line per degree, stroked wide enough that consecutive samples overlap.
  // Worst-case spacing is at emery's corners, where the perimeter is 146px from the
  // centre — 2.55px per degree — so three pixels closes every gap on all three
  // displays. The depth is a pixel count, so the band stays this thick from its outer
  // edge to its inner one at every angle, corners included.
  int16_t stroke = 2 + lo->radius / 64;
  graphics_context_set_stroke_width(ctx, stroke);
  graphics_context_set_stroke_color(ctx, COL_BAND);

  // A thick line's caps run stroke/2 past each of its endpoints, so asking for a line
  // tick_len long drew a band tick_len + stroke/2 deep — deeper than the notch zone it
  // was meant to fill, and deeper than the notches themselves. The outward overshoot is
  // wanted, since it pushes the band's outer edge past the dial rect and hard against
  // the screen; the inward one is not, so the line is shortened by exactly that much.
  // Measure a band before trusting a depth here: what is asked for and what is drawn
  // are two different numbers.
  int16_t cap = stroke / 2;

  for (int d = 0; d < 360; d++) {
    if (s_cov[d] == COV_NONE) continue;
    int16_t depth = (s_cov[d] == COV_NOW) ? now_depth : soon_depth;
    int32_t a = TRIG_MAX_ANGLE * d / 360;
    GPoint outer = dial_boundary(lo->dial, lo->center, a);
    // Perpendicular depth first, then the cap comes off — the cap overshoots along the
    // line, so it is in ray units and cannot be subtracted before the conversion.
    int32_t len = depth_along_ray(lo->dial, outer, a, depth) - cap;
    if (len < 1) len = 1;
    graphics_draw_line(ctx, outer, step_in(outer, a, len));
  }
}

// True where the thing underneath would swallow an ink notch, so the notch becomes
// a cut instead.
//
// Only ever true on flint, and only under a running band — the deeper of the two, in
// the one ink the display has. Both bands now stop short of the notch inner-ends, so
// a notch keeps a tail of ink past either; what the inversion buys is the part that
// crosses the band, which is most of a notch's length and all of its weight.
//
// Where there is hue the inversion is unwanted: black reads cleanly over both the
// cerulean of a band and the amber of a marker, and a continuous ink grid across
// them is the point. Nothing needs a white cut to separate it from a colour.
static bool notch_inverts(int deg) {
#ifdef PBL_COLOR
  (void)deg;
  return false;
#else
  return s_cov[deg] == COV_NOW;
#endif
}

static void draw_notches(GContext *ctx, const Layout *lo) {
  int16_t hour_w = lo->radius / 24;
  if (hour_w < 2) hour_w = 2;
  int16_t min_w = lo->radius / 90;
  if (min_w < 1) min_w = 1;

  for (int i = 0; i < 60; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 60;
    // Two orthogonal rules, unchanged from the original dial: the four quarters
    // run full length and everything else 25% shorter, while the twelve hour
    // positions are the thick ones.
    int32_t len = (i % 15 == 0) ? lo->tick_len : (lo->tick_len * 75) / 100;
    graphics_context_set_stroke_width(ctx, (i % 5 == 0) ? hour_w : min_w);
    graphics_context_set_stroke_color(ctx, notch_inverts(i * 6) ? COL_BG : COL_INK);

    GPoint outer = dial_boundary(lo->dial, lo->center, a);
    graphics_draw_line(ctx, outer, step_in(outer, a, depth_along_ray(lo->dial, outer, a, len)));
  }
}

static void draw_markers(GContext *ctx, const Layout *lo) {
  int16_t hw = lo->radius / 16;
  if (hw < 4) hw = 4;

  for (int i = 0; i < 60; i++) {
    if (!s_task_n[i]) continue;
    bool late    = (s_task_w[i] == COV_NOW);
    bool grouped = (s_task_n[i] > 1);

    // Depth carries grouping — independent of urgency, and it survives having no
    // colour. The count itself is never drawn; the bottom slot is where specifics
    // live. Both depths reach past the notch inner-ends, so a marker always reads
    // as a spike into the dial rather than as a fatter tick.
    int16_t depth = lo->tick_len * (grouped ? 19 : 15) / 10;

    // Solid everywhere there is hue: a filled wedge is a stronger mark than an
    // outline of the same size, and both states have earned it. Amber against
    // orange is what separates upcoming from overdue there, so fill is free to
    // say something else — that this is a point in time, not a span.
    //
    // On flint it is not free. Every colour above folds to black, which leaves
    // hollow-against-solid as the only urgency channel a marker has, so flint
    // keeps outlining the upcoming one.
    //
    // The halo is a white ring, and it exists for one case only: a marker sitting
    // on a band drawn in the *same* ink, which is again flint's situation and
    // nobody else's. On emery and gabbro amber against cerulean is already two
    // things, and outlining it in background just puts a gap in the notch grid
    // running over the top.
    draw_radial_triangle(ctx, lo->dial, lo->center, TRIG_MAX_ANGLE * i / 60,
                         depth, hw,
                         late ? COL_TASK_LATE : COL_TASK_SOON,
                         PBL_IF_COLOR_ELSE(true, late),
                         PBL_IF_COLOR_ELSE(false, true));
  }
}

// The hour index, and the reason absolute clock positions are readable at all:
// without it a marker at the 5 has no "now" to be relative to. It sweeps like an
// analog hand — at 4:30 it sits midway between the 4 and the 5.
//
// It points *outward*, which is what keeps it distinct from the point markers:
// those reach inward from the boundary, this one reaches outward toward it. The
// two never share a zone either — the tip stops short of the notch ring's inner
// edge, so the whole hand lives in the free area inside the dial and the ring
// stays the markers' alone.
//
// The tip's clearance is measured perpendicular to the boundary, like every other depth
// on the ring, so the hand keeps the same gap above the notch inner-ends at every angle
// including through a rectangular display's corner. Its length is not a boundary
// measurement and stays a plain distance along the ray, so the hand does not grow.
//
// Drawn after the slots rather than with the rest of the dial: both slots are
// centred, which puts them at twelve and six o'clock, and their background
// knockout was erasing the index outright whenever the hour was near either.
// Nothing on this face may hide the one element everything else is measured
// against. Being this long, it now crosses whichever slot it is nearest instead
// of stopping above it; the halo is what keeps it legible over the text.
#define HAND_TIP_GAP 3          // clearance from the notch ring's inner edge
#define HAND_LEN_DIV 4          // hand length as a divisor of the radius

void dial_draw_now(GContext *ctx, const Layout *lo, const struct tm *t) {
  int32_t a = TRIG_MAX_ANGLE * ((t->tm_hour % 12) * 60 + t->tm_min) / (12 * 60);
  int16_t phw = lo->radius / 10;
  if (phw < 5) phw = 5;

  int16_t len = lo->radius / HAND_LEN_DIV;

  // The clearance is measured off the boundary and so is perpendicular, which keeps the
  // tip the same distance clear of the notch inner-ends all the way round. The hand's
  // own length is not a boundary measurement — it is just how long the hand is — so it
  // stays a plain distance along the ray and the hand does not grow near a corner.
  GPoint edge = dial_boundary(lo->dial, lo->center, a);
  int32_t tip = depth_along_ray(lo->dial, edge, a, lo->tick_len + HAND_TIP_GAP);
  GPoint apex = step_in(edge, a, tip);
  GPoint base = step_in(edge, a, tip + len);
  draw_tri(ctx, apex, step_side(base, a, phw), step_side(base, a, -phw),
           COL_INDEX, true, 2, true);
}

// Outward to inward, except for where the notch ring sits in the stack.
//
// On a colour display it goes last, so one unbroken black grid runs over the
// bands and the markers both. The ring is the dial's ruler; a band or a marker
// interrupting it makes the timeline harder to read off, and black over cerulean
// or amber costs nothing to see.
//
// flint keeps the ring underneath the markers. Up there a notch over a solid
// marker would have to invert to be visible at all, and that white cut would
// split the one shape that says "overdue" straight down the middle.
void dial_draw(GContext *ctx, const Layout *lo, time_t now) {
  build_coverage(now);
  build_tasks(now);
  merge_adjacent();
  draw_bands(ctx, lo);
#ifdef PBL_COLOR
  draw_markers(ctx, lo);
  draw_notches(ctx, lo);
#else
  draw_notches(ctx, lo);
  draw_markers(ctx, lo);
#endif
}
