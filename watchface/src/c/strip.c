#include "strip.h"
#include "events.h"
#include "theme.h"
#include <string.h>

// ---------------------------------------------------------------------------
// The strip is a four-hour timeline down the left edge: one hour above the
// pointer, three below, later always lower.
//
// Everything the markers have to do — span a duration, flatten where they
// overlap, sit behind the notches, and read at two levels of prominence on a
// display with one ink — falls out of a single representation: one byte per
// *minute* of the visible window, holding the most prominent thing happening
// then. Overlapping appointments merge because they write the same array, and
// max() makes a merged band inherit the more urgent member's weight.
//
// A minute is under a pixel on all three displays, so quantising to one costs
// nothing visible and makes the array's unit mean something. The dial this
// replaced used one byte per degree, which was two minutes and an accident of
// the shape.
//
// Drawing is then one pass per layer, outward to inward in z-order: bands,
// notches, markers, pointer. The hour labels ride with the notches, being the
// same thing — a graduation of the ruler — and are the one part of the strip
// that draws *outboard* of the track.
//
// The arrays are file-scope rather than automatic. A Pebble app's stack is
// small and this is 241 bytes that would otherwise be live across four calls.
// ---------------------------------------------------------------------------

#define COV_NONE 0
#define COV_SOON 1
#define COV_NOW  2

static uint8_t s_cov[STRIP_SPAN_MIN + 1];   // bands, by minute of the window

// Point markers, sorted along the track and merged. The dial kept these in a
// fixed 60-slot array indexed by notch, which snapped every task to the nearest
// twelve minutes. The strip has the resolution to place them exactly, so it
// does, and merging is decided by whether two would physically overlap rather
// than by whether they landed in the same bucket.
typedef struct {
  int32_t u;
  uint8_t w;   // prominence
  uint8_t n;   // how many entries folded into this one
} Point;

static Point s_pts[EVENTS_MAX];
static int   s_pt_n;

// The hour boundaries in the visible window, and their wall-clock hour. Filled while
// the notches are walked and drawn in a pass of their own afterwards, because a label
// has to land *over* the markers and the notches are drawn under them on flint.
//
// A four-hour window holds four or five of these; six is room to spare.
typedef struct { int32_t u; uint8_t hh; } Hour;
static Hour s_hours[6];
static int  s_hour_n;

// A band this thin would otherwise vanish. Three minutes is about two pixels on
// flint, so this only ever rescues something shorter than that.
#define BAND_MIN_MIN 3

// How much of the notch zone's depth each band fills. A running one used to take all
// of it and an upcoming one half, which was too heavy: at that weight the strip reads
// as a fat coloured bar with ticks on it rather than as a ruler carrying a marker, and
// the step down where a running appointment ends and an overlapping upcoming one
// carries on is a cliff rather than a change of weight.
//
// The ratio between them is what has to survive, not the absolute depth. Depth, not
// density, is what separates the two: the first attempt hatched the upcoming band — a
// 1px line every other step — which is a textbook half-tone and completely wrong here,
// because the notches are also thin lines, so a hatched band read as nothing more than
// a patch of extra ticks. A solid band of reduced depth is a different *shape* from a
// tick, which is what makes it legible even on the platform with one ink.
#define BAND_NOW_PCT  70
#define BAND_SOON_PCT 35

static void build_coverage(time_t now) {
  memset(s_cov, COV_NONE, sizeof s_cov);
  const Event *tbl = events_table();
  time_t win0 = now - STRIP_BACK_S, win1 = now + STRIP_AHEAD_S;

  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &tbl[i];
    if (!event_visible(e, now) || !event_is_long(e)) continue;

    // Clip to the visible window at both ends. A meeting that began three hours
    // ago draws from the top of the strip; one running past the horizon draws to
    // the bottom. Either way the band runs off the edge rather than stopping
    // short, which is what says "continues past here".
    time_t s = e->start, en = event_end(e);
    if (s  < win0) s  = win0;
    if (en > win1) en = win1;
    if (en < s) continue;

    uint8_t w = event_prominent(e, now) ? COV_NOW : COV_SOON;
    int32_t m0 = (int32_t)(s - win0) / 60;
    int32_t m1 = (int32_t)(en - win0) / 60;
    if (m1 < m0 + BAND_MIN_MIN) m1 = m0 + BAND_MIN_MIN;
    if (m0 < 0) m0 = 0;
    if (m1 > STRIP_SPAN_MIN) m1 = STRIP_SPAN_MIN;

    for (int32_t m = m0; m <= m1; m++) {
      if (s_cov[m] < w) s_cov[m] = w;
    }
  }
}

// Half a marker's extent along the track. Its base spans this either side of the
// entry's own position.
static int16_t marker_half(const Layout *lo) {
  int16_t hl = lo->radius / 16;
  return (hl < 4) ? 4 : hl;
}

#ifndef PBL_COLOR
// The same, for flint's wedge: its base spans this either side of now. Two callers —
// the wedge itself and the hour label that has to get out of its way.
static int16_t pointer_half(const Layout *lo) {
  int16_t phw = lo->notch_len * POINTER_HALF_PCT / 100;
  return (phw < 5) ? 5 : phw;
}
#endif

static void build_points(const Layout *lo, time_t now) {
  s_pt_n = 0;
  const Event *tbl = events_table();

  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &tbl[i];
    if (!event_visible(e, now) || event_is_long(e)) continue;
    int32_t u = u_of_time(e->start, now);
    if (u < 0 || u > STRIP_SPAN_S) continue;   // off-strip: it will scroll in

    // Insertion sort by position. The event table has no order of its own —
    // nothing else on this face has ever needed one — and the merge below only
    // works if neighbours along the track are neighbours in the array.
    int k = s_pt_n++;
    while (k > 0 && s_pts[k - 1].u > u) {
      s_pts[k] = s_pts[k - 1];
      k--;
    }
    s_pts[k] = (Point){ .u = u,
                        .w = event_prominent(e, now) ? COV_NOW : COV_SOON,
                        .n = 1 };
  }
  if (s_pt_n == 0) return;

  // Fold together whatever cannot be drawn apart.
  //
  // A marker's base is half its extent either side of its position, so two
  // closer than a full base overlap into a smear that reads as one lumpy shape
  // rather than two markers. If they cannot be drawn apart they *are* one
  // marker, and the merged one sits at the group's mean position and carries the
  // group's count in its depth. The count itself is never drawn; the countdown
  // row is where specifics live.
  int32_t min_gap = u_of_px(lo, 2 * marker_half(lo));
  int out = 0;
  for (int r = 0; r < s_pt_n; ) {
    int first = r;
    int32_t sum = 0;
    int count = 0;
    uint8_t weight = 0;
    while (r < s_pt_n && (r == first || s_pts[r].u - s_pts[r - 1].u < min_gap)) {
      sum += s_pts[r].u;
      count += s_pts[r].n;
      if (s_pts[r].w > weight) weight = s_pts[r].w;
      r++;
    }
    s_pts[out++] = (Point){ .u = sum / (r - first),
                            .w = weight,
                            .n = (uint8_t)(count > 255 ? 255 : count) };
  }
  s_pt_n = out;
}

// How deep a band of the given coverage weight fills, in pixels from the track.
//
// Two callers, which is why it is a function. The bands draw themselves with it, and
// the notches need it to know where a running band stops — see draw_notches.
//
// Rounded, not truncated. These are single-digit pixel counts on flint, where losing
// most of a pixel to integer division is a tenth of the band.
static int16_t band_depth(const Layout *lo, uint8_t weight) {
  if (weight == COV_NOW) {
    int16_t d = (lo->notch_len * BAND_NOW_PCT + 50) / 100;
    return (d < 3) ? 3 : d;
  }
  int16_t d = (lo->notch_len * BAND_SOON_PCT + 50) / 100;
  return (d < 2) ? 2 : d;
}

static void draw_bands(GContext *ctx, const Layout *lo) {
  // One fill per *run* of equally weighted minutes, not one line per minute.
  //
  // The per-minute version was a sampling trick — 241 thick stroked lines spaced under a
  // pixel apart, overlapping into something solid — and it worked, but it paid for the
  // solidity with the shape of the ends: a thick line is capped with a semicircle, and
  // the one sample at each end of a run had no neighbour to cover its cap, so every band
  // came out with a pixel bitten off each of its four corners. Runs are also the honest
  // unit. A band is a span, s_cov already holds it as one, and drawing it as a span is
  // both squarer and two orders of magnitude fewer draw calls.
  //
  // Shallower pass first. Where a running band abuts an upcoming one the two runs can
  // land on the same pixel row — consecutive minutes are under a pixel apart on all
  // three displays — and the deeper of the two has to win that row, which is the same
  // rule the max() in build_coverage() applies a minute at a time.
  static const uint8_t order[2] = { COV_SOON, COV_NOW };
  for (int pass = 0; pass < 2; pass++) {
    uint8_t want = order[pass];
    int16_t depth = band_depth(lo, want);
    for (int32_t m = 0; m <= STRIP_SPAN_MIN; ) {
      if (s_cov[m] != want) { m++; continue; }
      int32_t m0 = m;
      while (m <= STRIP_SPAN_MIN && s_cov[m] == want) m++;
      fill_track_band(ctx, lo, m0 * 60, (m - 1) * 60, depth, COL_BAND);
    }
  }
}

// True where the thing underneath would swallow an ink notch, so the notch becomes
// a cut instead.
//
// Only ever true on flint, and only under a running band — the deeper of the two, in
// the one ink the display has. Both bands stop short of the notch inner-ends, so a
// notch keeps a tail of ink past either; what the inversion buys is the part that
// crosses the band, which is most of a notch's length and all of its weight.
//
// Where there is hue the inversion is unwanted: black reads cleanly over both the
// cerulean of a band and the amber of a marker, and a continuous ink ruler across
// them is the point. Nothing needs a white cut to separate it from a colour.
static bool notch_inverts(int32_t u) {
#ifdef PBL_COLOR
  (void)u;
  return false;
#else
  int32_t m = u / 60;
  if (m < 0 || m > STRIP_SPAN_MIN) return false;
  return s_cov[m] == COV_NOW;
#endif
}

// An hour label, in a lane inboard of the track. Where in the ladder that lane sits is
// per-display and lives in layout_compute; see `label_x`. On colour it is past the rule
// and the markers both, and nothing is ever over or under it. On flint it is between the
// ruler and the wedge, sharing that free area with the markers and the wedge — and a
// label really does want the same pixels as those, often, a reminder landing on the hour
// being an ordinary thing rather than a coincidence.
//
// Against a marker the label wins: it knocks out its own footprint and draws over the
// top, which is what every text row on this face already does and for the same reason.
// Drawn the other way round, with the marker on top, its background halo cut a slot clean
// through the digit and the number came out unreadable while the marker lost nothing
// worth having. The marker is still there above and below the plate, and it is the
// *position* of a marker that carries its meaning, not its middle.
//
// Against the wedge the label loses, and gets out of the way rather than being covered —
// see the suppression below.
//
// The plate can only ever cut a marker or a band: the notches end three pixels outboard
// of this lane, and the wedge is not on the screen yet when this runs.
//
// One expression covers both display shapes, the same way the markers get one: `label_x`
// is a depth along the ray, so on a rectangle this is a box in a vertical lane and on
// gabbro's arc it is a box centred on the ray, the lane curving with the strip. Nothing
// here branches on shape.
static void draw_hour_label(GContext *ctx, const Layout *lo, GFont font,
                            int32_t u, int hh) {
  char buf[4];
  if (clock_is_24h_style()) {
    snprintf(buf, sizeof buf, "%02d", hh);
  } else {
    // Same reasoning as the clock's leading zero: "07" is not how anyone reads a
    // twelve-hour dial. The lane is sized on "00" either way, so a one-digit hour
    // centres in it rather than moving the lane.
    int h = hh % 12;
    snprintf(buf, sizeof buf, "%d", h ? h : 12);
  }

  GSize ts = graphics_text_layout_get_content_size(
      buf, font, lo->bounds, GTextOverflowModeFill, GTextAlignmentCenter);

  // Drop a label that would hang off either end of the window rather than let it draw
  // half a digit. Expressed in seconds, like every other extent on the track: the
  // label's own half-height, converted. Conservative by a pixel or two, because a
  // content box is taller than the ink in it.
  int32_t edge = u_of_px(lo, ts.h / 2);
  if (u < edge || u > STRIP_SPAN_S - edge) return;

#ifndef PBL_COLOR
  // And drop the one the wedge lands on. Everything else in this lane can be covered or
  // covered over, but the wedge is drawn last of all — after the text rows, so that
  // nothing can hide the element the whole strip is read against — and a plate cannot
  // win against something drawn later.
  //
  // What is lost is the label nearest now, for the twenty minutes either side of the
  // hour that the wedge is wide enough to reach: call it two thirds of every hour, one
  // label of four or five. It is also the only label on the strip that is being stated
  // somewhere else — the clock is pinned to this wedge and is showing that very hour, a
  // few pixels to the right of the gap. Dropping any other label would lose information;
  // dropping this one loses a repetition.
  int32_t near = u_of_px(lo, pointer_half(lo) + ts.h / 2 + 1);
  if (u > STRIP_BACK_S - near && u < STRIP_BACK_S + near) return;
#endif

  Track tr = track_at(lo, u);
  GPoint q = step_in(tr.p, tr.a, lo->label_x + lo->label_w / 2);

  // The same measured lift the clock takes, and for the same reason: a digits-only
  // subset never descends below the baseline, so the box carries its slack above the ink
  // and centring the box leaves the digits low. An eighth, not the clock's twentieth —
  // measured off a flint screenshot, where a label drawn with no correction at all came
  // out with its ink spanning y 24..29 against an hour notch centred on 25, a pixel and
  // a half low. At this size a twentieth truncates to nothing, which is how it was
  // found: the label is small enough that integer division is a design decision.
  GRect box = GRect(q.x - lo->label_w / 2, q.y - ts.h / 2 - ts.h / 8,
                    lo->label_w, ts.h);

  // Two pixels wider than the box on each side, which is the pad text_plate() uses and
  // is here for a reason a screenshot gave up: cut to the box exactly, a marker's apex
  // stopped one pixel short of the first digit and the two read as one shape. Not three:
  // the lane is `margin` off the notch zone, and three would take the tip off the hour
  // notch this label is naming.
  knock_out(ctx, GRect(box.origin.x - 2, box.origin.y,
                       box.size.w + 4, box.size.h));
  graphics_context_set_text_color(ctx, COL_INK);
  graphics_draw_text(ctx, buf, font, box, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

// Every hour boundary the last notch pass found, over the top of the strip.
static void draw_labels(GContext *ctx, const Layout *lo, GFont font) {
  for (int i = 0; i < s_hour_n; i++) {
    draw_hour_label(ctx, lo, font, s_hours[i].u, s_hours[i].hh);
  }
}

// The ruler. A notch every fifteen minutes, on the wall clock's own quarter hours
// rather than at multiples of fifteen from the top of the window — which is what
// makes the strip scroll: each minute every notch's position slides by the same
// fraction of a pitch, and the stationary pointer is what that motion is read
// against.
//
// The hour notches are thicker, not longer. Length is already spoken for: it is what
// separates a notch from a band, which fills part of the same depth. They are also the
// ones that carry a number, which is what makes the strip say *when* rather than only
// how far off — four or five labels in the window, and the reader stops counting
// notches from the "now" mark. This pass only records where they are; draw_labels()
// puts them on the screen once everything they have to sit over is down.
static void draw_notches(GContext *ctx, const Layout *lo, time_t now) {
  // A ninetieth of the radius is 0, 1 and 1 on the three displays, so stroke_px() put
  // every quarter-hour graduation on a single pixel — on gabbro that is a hairline on a
  // 260 px screen beside a five-pixel hour notch, and the ruler's pitch stopped being
  // legible at all between the numbered notches. A fortieth is still 1 on flint and
  // emery, where one pixel is the right weight and the only other option is three, and
  // it reaches three on gabbro, which is the display that has the room for it.
  int16_t min_w = stroke_px(lo->radius / 40);

  // The hour notch is thicker, and "thicker" is a ratio, not a divisor of its own. A
  // twenty-fourth of the radius gave 3/3/5, which was two and three times the minute
  // notch on flint and emery and — once the minute notch on gabbro grew to three — barely
  // more than it there. Measured on gabbro, the ruler lost its structure entirely: every
  // graduation looked the same and only the numbers said which were hours. Twice its
  // neighbour is the floor, and stroke_px() takes it to the next odd width up.
  int16_t hour_w = stroke_px(lo->radius / 24);
  if (hour_w < 3) hour_w = 3;
  if (hour_w < 2 * min_w) hour_w = stroke_px(2 * min_w + 1);

  time_t start = now - STRIP_BACK_S;
  struct tm lt = *localtime(&start);

  // Back up to the quarter hour at or before the top of the window, then step
  // forward to the first one inside it. Tracking the minute alongside avoids a
  // localtime() per notch: from a quarter-hour boundary it only ever advances by
  // fifteen, and all that is needed is whether it landed on the hour.
  int32_t back = lt.tm_sec + (lt.tm_min % NOTCH_STEP_MIN) * 60;
  time_t t = start - back;
  int m = (lt.tm_min / NOTCH_STEP_MIN) * NOTCH_STEP_MIN;

  // The hour rides along for the same reason and by the same rule: it is the hour of
  // `t`, and from a quarter-hour boundary it only ever advances when the minute wraps
  // to zero. The one case that is not free is the step forward above — if that landed
  // on the hour it crossed one, so the seed is already a step behind.
  int hh = lt.tm_hour;
  if (back > 0) {
    t += NOTCH_STEP_MIN * 60;
    m = (m + NOTCH_STEP_MIN) % 60;
    if (m == 0) hh = (hh + 1) % 24;
  }

  s_hour_n = 0;
  for (;;) {
    int32_t u = (int32_t)(t - start);
    if (u > STRIP_SPAN_S) break;

    graphics_context_set_stroke_width(ctx, (m == 0) ? hour_w : min_w);

    Track tr = track_at(lo, u);
    GPoint inner = step_in(tr.p, tr.a, lo->notch_len);

    if (notch_inverts(u)) {
      // Two segments, and the split is where the band stops.
      //
      // Inverting the whole notch is what this did, and it deleted the ruler. A notch is
      // longer than a running band is deep — two pixels longer on flint — so those last
      // two were being drawn in the background colour on top of the background. Under a
      // running appointment the strip came off the display as a black bar with white
      // slots in it and no graduations anywhere, and a white slot where a notch cut
      // looked exactly like the white gap where one band run ends and the next begins.
      //
      // The cut belongs only where there is band under it. Past that, the notch is ink
      // like every other notch, and the ruler runs unbroken through the appointment.
      int16_t bd = band_depth(lo, COV_NOW);
      graphics_context_set_stroke_color(ctx, COL_BG);
      graphics_draw_line(ctx, tr.p, step_in(tr.p, tr.a, bd));
      if (bd + 1 <= lo->notch_len) {
        graphics_context_set_stroke_color(ctx, COL_INK);
        graphics_draw_line(ctx, step_in(tr.p, tr.a, bd + 1), inner);
      }
    } else {
      graphics_context_set_stroke_color(ctx, COL_INK);
      graphics_draw_line(ctx, tr.p, inner);
    }

    if (m == 0 && s_hour_n < (int)(sizeof s_hours / sizeof s_hours[0])) {
      s_hours[s_hour_n++] = (Hour){ .u = u, .hh = (uint8_t)hh };
    }

    // Advanced here rather than in the for-increment, so the hour can follow the
    // minute's wrap.
    t += NOTCH_STEP_MIN * 60;
    m = (m + NOTCH_STEP_MIN) % 60;
    if (m == 0) hh = (hh + 1) % 24;
  }
}

static void draw_markers(GContext *ctx, const Layout *lo) {
  int16_t hl = marker_half(lo);

  for (int i = 0; i < s_pt_n; i++) {
    bool late    = (s_pts[i].w == COV_NOW);
    bool grouped = (s_pts[i].n > 1);

    // Depth carries grouping — independent of urgency, and it survives having no
    // colour. Both depths reach past the notch inner-ends, so a marker always
    // reads as a spike out of the ruler rather than as a fatter tick.
    int16_t depth = lo->notch_len
                  * (grouped ? MARKER_GROUP_PCT : MARKER_DEPTH_PCT) / 100;

    // Always solid, on every platform — which the dial could not afford.
    //
    // There, an upcoming marker was hollow on flint, because folding every colour
    // to black left hollow-against-solid as the only channel that could say
    // "overdue". On a linear track that channel is redundant: overdue is *above*
    // the pointer and upcoming is *below* it, always, and position is a stronger
    // statement than fill was. A twelve-hour ring could not say this — every
    // point on it was both past and future — and paying for that in fill was the
    // cost of the shape, not a preference.
    //
    // Dropping it is not just simplification. Measured on flint, a hollow wedge
    // at this size is a 1px outline under a 3px background halo, and where it
    // crossed a band it striped the two together into noise. A solid wedge with a
    // grown halo reads cleanly at the same size. Colour still separates amber from
    // orange where there is hue; it is now saying the same thing twice, cheaply.
    //
    // The halo exists for one case: a marker sitting on a band drawn in the *same*
    // ink, which is flint's situation and nobody else's. On emery and gabbro amber
    // against cerulean is already two things, and outlining it in background just
    // puts a gap in the ruler running over the top.
    // The inner end is blunt rather than pointed. See draw_track_wedge: a triangle at
    // this depth-to-base ratio spends its last third under two pixels tall, which is
    // thinner than the minute notches the shape exists to stand out from.
    //
    // A merged marker is wider at the base as well as deeper and blunter, and the base
    // is the part that had to be added. Blunting alone said "merged" by taking the taper
    // out, and a wedge with no taper is a rectangle: on flint the group came off the
    // display as a horizontal bar and stopped reading as a marker at all. Widening the
    // base restores the taper at the larger size, which is the point — a group should
    // look like more of the same thing, not like a different thing. It is also honest,
    // a group being exactly the events that could not be drawn apart.
    int16_t base = grouped ? (hl * 5 / 4) : hl;
    int16_t tip  = grouped ? (base / 2) : (base / 4);

    draw_track_wedge(ctx, lo, s_pts[i].u, depth, base, tip,
                     late ? COL_TASK_LATE : COL_TASK_SOON,
                     PBL_IF_COLOR_ELSE(false, true));
  }
}

// "Now", and the reason the strip is readable at all: without it a marker two notches
// down has no now to be relative to. It never moves — the ruler moves past it — which is
// what lets the clock beside it be plain digits.
//
// Drawn after the text rows rather than with the rest of the strip, and last of
// everything: the clock is pinned to it, so the two are adjacent by construction, and
// the clock's background knockout was erasing it outright. Nothing on this face may
// hide the one element everything else is measured against.
//
// The shape is not the same on both kinds of display, and this is the one place on the
// face where that is true of an element rather than of its colour.
//
// Where there is colour it is a **rule struck across the strip** — through the bands,
// the notches and the markers alike, over the top of all of them, as deep as the strip's
// own elements go and no deeper, which it now is exactly rather than approximately: it is
// filled as a span of the track, so `rule_len` is where it stops instead of where its
// end cap started. What it costs to read that way it takes back in hue: red
// is not on the strip anywhere else, so a red line is unambiguous at a glance, and a line
// *through* the ruler says "the ruler is here" in a way a shape beside it has to be
// learned to mean. It also frees the whole free area, which is where the hour labels
// went.
//
// flint gets the wedge, because a black rule there would be a black line across a ruler
// made of black lines — an extra notch, thicker than its neighbours, which is already
// what an hour notch is. So up there "now" stays a shape, it points *outward* back at
// the track, and that direction is what keeps it distinct from the point markers, which
// reach inward. Its tip stops short of the notch zone so the strip stays the bands' and
// markers' alone, and the halo keeps it legible where it meets a band in the same ink.
void strip_draw_now(GContext *ctx, const Layout *lo) {
#ifdef PBL_COLOR
  // The rule is a band like any other — a span of the track, filled to a depth — so it is
  // drawn by the same primitive, and that is what squares its ends. Stroked, it came out
  // a lozenge: a thick line is capped with a semicircle at each end, so the one element
  // the whole face is measured against had a rounded nose sticking off the strip and a
  // rounded tail poking into the hour labels' lane.
  //
  // Squaring it also gives back the depth those caps were spending. `rule_len` is the
  // deepest thing the strip draws and `margin` is the gap between that and the
  // numbers; the inner cap was taking two of them on emery and all three on gabbro, which
  // is why the rule and the hour beside it read as touching there.
  //
  // The width is still a stroke width, because it is still what it was chosen to be — a
  // thickness across the track, wider than an hour notch — and w/2 either side of now is
  // the same w pixels the stroke drew.
  int16_t w = stroke_px(lo->radius / RULE_W_DIV);
  if (w < 3) w = 3;
  int32_t half = u_of_px(lo, w / 2);
  fill_track_band(ctx, lo, STRIP_BACK_S - half, STRIP_BACK_S + half,
                  lo->rule_len, COL_INDEX);
#else
  Track t = track_at(lo, STRIP_BACK_S);
  int16_t phw = pointer_half(lo);
  int16_t len = lo->notch_len * POINTER_LEN_PCT / 100;

  GPoint apex = step_in(t.p, t.a, lo->ptr_tip);
  GPoint base = step_in(t.p, t.a, lo->ptr_tip + len);
  draw_tri(ctx, apex, step_side(base, t.a, phw), step_side(base, t.a, -phw),
           COL_INDEX, true, 1, true);   // filled: the stroke width is unused
#endif
}

// Outward to inward, except for where the notches sit in the stack.
//
// On a colour display they go last, so one unbroken black ruler runs over the bands
// and the markers both. The notches are what the timeline is read off; a band or a
// marker interrupting them makes it harder to read, and black over cerulean or amber
// costs nothing to see.
//
// flint keeps the notches underneath the markers. Up there a notch over a solid
// marker would have to invert to be visible at all, and that background-coloured cut
// would split the one shape that says "overdue" straight down the middle.
//
// The hour labels go last on both, which is the one part of this order that is not a
// choice: a label knocks out its own footprint, so anything it may cover has to already
// be drawn.
void strip_draw(GContext *ctx, const Layout *lo, time_t now, GFont tick) {
  build_coverage(now);
  build_points(lo, now);
  draw_bands(ctx, lo);
#ifdef PBL_COLOR
  draw_markers(ctx, lo);
  draw_notches(ctx, lo, now);
#else
  draw_notches(ctx, lo, now);
  draw_markers(ctx, lo);
#endif
  draw_labels(ctx, lo, tick);
}
