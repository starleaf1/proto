#include "dial.h"
#include "events.h"
#include "theme.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Six hours of timeline, anchored to the dial.
//
// A marker sits at the clock angle of its own time: a three o'clock meeting is
// at the 3. The window is 1 h behind and 5 h ahead, which on a twelve-hour dial
// is the half-turn running from 30 degrees behind the hour hand to 150 ahead of
// it — thirty degrees to the hour, which is the hour hand's own rate.
//
// That is the whole design. Because the ring and the hands share one angular
// scale, the hand needs no now mark to explain it and the ring needs no
// graduation of its own: the twelve ticks inside are the ring's scale too. And
// because a marker's angle depends only on its own time, markers hold still
// while the window's edge sweeps past them — the inverse of the digital face,
// where the ruler slid past a pinned now, and the same f(t - now) underneath.
//
// Everything the markers have to do — span a duration, flatten where they
// overlap, read at two levels of prominence on a display with one ink — falls
// out of one representation: one byte per *minute* of the visible window,
// holding the most prominent thing happening then. Overlapping appointments
// merge because they write the same array, and max() makes a merged band
// inherit the more urgent member's weight.
//
// A minute is 0.58 px on flint and 2.1 on gabbro, so a per-*degree* array would
// bucket two minutes together and cost gabbro a pixel of placement. The minute
// is both the finer unit and the one that means something.
//
// The arrays are file-scope rather than automatic. A Pebble app's stack is
// small and this is 361 bytes that would otherwise be live across four calls.
// ---------------------------------------------------------------------------

#define COV_NONE 0
#define COV_SOON 1
#define COV_NOW  2

static uint8_t s_cov[RING_SPAN_MIN + 1];   // bands, by minute of the window
static bool    s_cov_any;                  // did anything land in it?

typedef struct {
  int32_t m;   // minutes from the top of the window
  uint8_t w;   // prominence
  uint8_t n;   // how many entries folded into this one
} Point;

static Point s_pts[EVENTS_MAX];
static int   s_pt_n;

// The dial angle of the top of the window, recomputed once per paint. Every
// other angle on the ring is this plus a span, never a fresh absolute — which
// is what keeps the whole window monotonically increasing across twelve
// o'clock, and that in turn is what fill_ring_band() needs to know which way to
// sweep.
static int32_t s_base_a;

// A band this thin would otherwise vanish. Four minutes is about two pixels on
// flint, so this only ever rescues something shorter than that.
#define BAND_MIN_MIN 4

static void build_coverage(time_t now) {
  memset(s_cov, COV_NONE, sizeof s_cov);
  s_cov_any = false;
  const Event *tbl = events_table();
  time_t win0 = now - RING_BACK_S, win1 = now + RING_AHEAD_S;

  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &tbl[i];
    if (!event_visible(e, now) || !event_is_long(e)) continue;

    // Clip to the window at both ends. A meeting that began two hours ago draws
    // from the window's trailing edge; one running past the horizon draws to
    // its leading edge. Either way the band runs into the end cap rather than
    // stopping short of it, which is what says "continues past here".
    time_t s = e->start, en = event_end(e);
    if (s  < win0) s  = win0;
    if (en > win1) en = win1;
    if (en < s) continue;

    uint8_t w = event_prominent(e, now) ? COV_NOW : COV_SOON;
    int32_t m0 = (int32_t)(s - win0) / 60;
    int32_t m1 = (int32_t)(en - win0) / 60;
    if (m1 < m0 + BAND_MIN_MIN) m1 = m0 + BAND_MIN_MIN;
    if (m0 < 0) m0 = 0;
    if (m1 > RING_SPAN_MIN) m1 = RING_SPAN_MIN;
    if (m0 > m1) continue;

    s_cov_any = true;
    for (int32_t m = m0; m <= m1; m++) {
      if (s_cov[m] < w) s_cov[m] = w;
    }
  }
}

// Half a marker's extent along the rim, in pixels. Its base spans this either
// side of the entry's own position.
static int16_t marker_half(const Layout *lo) {
  int16_t hl = lo->radius / 16;
  return (hl < 4) ? 4 : hl;
}

static void build_points(const Layout *lo, time_t now) {
  s_pt_n = 0;
  const Event *tbl = events_table();
  time_t win0 = now - RING_BACK_S;

  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &tbl[i];
    if (!event_visible(e, now) || event_is_long(e)) continue;
    int32_t m = (int32_t)(e->start - win0) / 60;
    if (m < 0 || m > RING_SPAN_MIN) continue;   // outside the window: not yet

    // Insertion sort by position. The event table has no order of its own, and
    // the merge below only works if neighbours on the ring are neighbours in
    // the array.
    int k = s_pt_n++;
    while (k > 0 && s_pts[k - 1].m > m) {
      s_pts[k] = s_pts[k - 1];
      k--;
    }
    s_pts[k] = (Point){ .m = m,
                        .w = event_prominent(e, now) ? COV_NOW : COV_SOON,
                        .n = 1 };
  }
  if (s_pt_n == 0) return;

  // Fold together whatever cannot be drawn apart. A wedge's base is half its
  // extent either side of its position, so two closer than a full base overlap
  // into a smear that reads as one lumpy shape rather than two markers. If they
  // cannot be drawn apart they *are* one marker: it sits at the group's mean
  // position and carries the group's count in its depth. The count is never
  // drawn — the countdown plate is where specifics live.
  int32_t min_gap = span_of_angle(angle_of_px(2 * marker_half(lo), lo->r_out));
  if (min_gap < 1) min_gap = 1;
  int out = 0;
  for (int r = 0; r < s_pt_n; ) {
    int first = r;
    int32_t sum = 0;
    int count = 0;
    uint8_t weight = 0;
    while (r < s_pt_n && (r == first || s_pts[r].m - s_pts[r - 1].m < min_gap)) {
      sum += s_pts[r].m;
      count += s_pts[r].n;
      if (s_pts[r].w > weight) weight = s_pts[r].w;
      r++;
    }
    s_pts[out++] = (Point){ .m = sum / (r - first),
                            .w = weight,
                            .n = (uint8_t)(count > 255 ? 255 : count) };
  }
  s_pt_n = out;
}

// The angle of a minute of the window.
static int32_t angle_at(int32_t m) {
  return s_base_a + angle_of_span(m);
}

// How deep a band of the given weight fills, from the rim inward.
//
// Depth, not density, is what separates the two states. A running band takes
// the whole ring and an upcoming one its outer half, which reads identically on
// all three displays — where a paler tint would say nothing at all on flint,
// and a hatched fill would only read as a patch of extra ticks.
static int16_t band_depth(const Layout *lo, uint8_t weight) {
  if (weight == COV_NOW) return lo->ring_d;
  int16_t d = (lo->ring_d + 1) / 2;
  return (d < 2) ? 2 : d;
}

// The track the markers sit on, and its two ends.
//
// The rail runs the window and no further, which is the only thing saying where
// the horizon is: past it the ring is not empty, it is simply not being shown.
// The end caps are radial and full ring depth, so the boundary reads as a wall
// rather than as the rail having faded out.
static void draw_rail(GContext *ctx, const Layout *lo) {
  int32_t a0 = angle_at(0), a1 = angle_at(RING_SPAN_MIN);
  draw_ring_arc(ctx, lo, a0, a1, lo->r_in, COL_INK);
  int16_t w = stroke_px(lo->radius / 48);
  draw_spoke(ctx, lo, a0, lo->r_in - 1, lo->r_out, w, COL_INK);
  draw_spoke(ctx, lo, a1, lo->r_in - 1, lo->r_out, w, COL_INK);
}

static void draw_bands(GContext *ctx, const Layout *lo) {
  // One fill per *run* of equally weighted minutes, not one per minute: a band
  // is a span, s_cov already holds it as one, and an annular sector drawn once
  // has square ends where 360 overlapping ones would have neither.
  //
  // Shallower pass first. Where a running band abuts an upcoming one the two
  // runs can land on the same pixel of rim — consecutive minutes are under a
  // pixel apart on flint — and the deeper of the two has to win it, which is
  // the same rule the max() in build_coverage() applies a minute at a time.
  static const uint8_t order[2] = { COV_SOON, COV_NOW };
  for (int pass = 0; pass < 2; pass++) {
    uint8_t want = order[pass];
    int16_t depth = band_depth(lo, want);
    for (int32_t m = 0; m <= RING_SPAN_MIN; ) {
      if (s_cov[m] != want) { m++; continue; }
      int32_t m0 = m;
      while (m <= RING_SPAN_MIN && s_cov[m] == want) m++;
      fill_ring_band(ctx, lo, angle_at(m0), angle_at(m - 1),
                     lo->r_out, depth, COL_BAND);
    }
  }
}

// Point-in-time entries: a wedge based on the rim, tapering inward through the
// rail and past it.
//
// The poke is what makes it a different kind of thing from a band. A band stops
// at the rail because a span is bounded by the track it is measured on; a task
// is an instant, so it crosses the track and points at the middle of the face.
// Nothing else on the ring goes inside the rail.
static void draw_wedges(GContext *ctx, const Layout *lo) {
  int16_t hl = marker_half(lo);
  int16_t poke = lo->r_in - lo->r_apex;

  for (int i = 0; i < s_pt_n; i++) {
    bool grouped = s_pts[i].n > 1;
    bool late = s_pts[i].w == COV_NOW;

    // A merged marker is wider at the base and blunter as well as deeper.
    // Depth alone is a quarter of a difference, which is not one a reader can
    // see without the single case beside it to compare against; blunting alone
    // takes the taper out, and a wedge with no taper is a rectangle.
    int16_t base_px = grouped ? (hl * 5 / 4) : hl;
    int32_t half_a = angle_of_px(base_px, lo->r_out);
    int32_t tip_a = grouped ? half_a / 2 : half_a / 4;
    int16_t apex = grouped ? (int16_t)(lo->r_in - poke * 3 / 2) : lo->r_apex;
    if (apex < lo->tick_out + 1) apex = lo->tick_out + 1;

    // Solid on every platform, haloed only where it can meet a band in the same
    // ink. On emery and gabbro amber against cerulean is already two things,
    // and outlining it in background would just put a gap in the ring.
    ring_wedge(ctx, lo, angle_at(s_pts[i].m), half_a, tip_a,
               lo->r_out, apex,
               late ? COL_TASK_LATE : COL_TASK_SOON,
               PBL_IF_COLOR_ELSE(false, true));
  }
}

// The twelve ticks, and the ring's scale as well as the clock's — which is the
// payoff of anchoring the markers to the dial. Graduating the ring separately
// would put two scales in two concentric lanes on a display with one ink.
//
// The quarters are thicker, not longer. Length already separates a tick from
// the ring above it; thickness is what is left, and "thicker" is a ratio to its
// neighbour rather than a divisor of its own, so the difference survives on
// gabbro where the minor tick is three pixels to begin with.
static void draw_ticks(GContext *ctx, const Layout *lo) {
  int16_t min_w = stroke_px(lo->radius / 40);
  int16_t maj_w = stroke_px(lo->radius / 24);
  if (maj_w < 3) maj_w = 3;
  if (maj_w < 2 * min_w) maj_w = stroke_px(2 * min_w + 1);

  for (int i = 0; i < 12; i++) {
    bool major = (i % 3) == 0;
    int32_t a = (int32_t)i * TRIG_MAX_ANGLE / 12;
    int16_t len = major ? lo->tick_maj : lo->tick_min;
    draw_spoke(ctx, lo, a, lo->tick_out - len, lo->tick_out,
               major ? maj_w : min_w, COL_INK);
  }
}

void dial_draw(GContext *ctx, const Layout *lo, time_t now) {
  s_base_a = angle_of_time(now - RING_BACK_S);
  build_coverage(now);
  build_points(lo, now);

  // An empty window hides the track along with the markers it would have
  // carried. The rail is a scale, and a scale with nothing on it is a claim
  // about the next six hours that the face cannot make — the ring is blank when
  // the calendar is clear and blank when the companion has never spoken, and
  // only the bottom row can tell those apart. Drawing the rail anyway would make
  // it look answered. What is left is a plain analog clock, which is what a
  // watch with nothing to say should look like.
  if (s_cov_any || s_pt_n > 0) draw_rail(ctx, lo);
  draw_bands(ctx, lo);
  draw_wedges(ctx, lo);
  draw_ticks(ctx, lo);
}

// One hand: a quad tapering from the tail, behind the hub, to a blunt tip.
//
// Blunt for the reason every wedge on this face is — a shape that tapers to a
// point spends its last stretch under two pixels wide and draws it as a
// hairline, and on the hands that stretch is the end carrying the reading.
//
// Haloed, because both hands cross the ring and the hour hand ends inside it.
// On flint a black hand over a black band is otherwise one shape.
static void draw_hand(GContext *ctx, const Layout *lo, int32_t a,
                      int16_t len, int16_t half, GColor ink) {
  // Three quarters, not a half.
  //
  // The taper runs from the tail to the tip, and the disc hides everything inside
  // its own radius — which on flint is half the hand. At a half-width tip the
  // hour hand had already narrowed to about two pixels by the time it emerged
  // from under the rim, so the one distinction flint has between the two hands
  // was spent entirely on the part of them nobody can see. A gentle taper keeps
  // the hand's width where the hand is.
  int16_t tip_half = half * 3 / 4;
  if (tip_half < 1) tip_half = 1;
  int32_t left = a - TRIG_MAX_ANGLE / 4, right = a + TRIG_MAX_ANGLE / 4;

  GPoint tip  = point_on_circle(lo->center, len, a);
  GPoint tail = point_on_circle(lo->center, -lo->hand_tail, a);
  GPoint pts[4] = {
    point_step(tip,  left,  tip_half),
    point_step(tip,  right, tip_half),
    point_step(tail, right, half),
    point_step(tail, left,  half),
  };
  fill_poly(ctx, pts, 4, ink, true);

  // And then the spine, stroked, because the polygon alone does not survive being
  // this thin. `gpath_draw_filled` rasterises by scanline, and a slanted sliver a
  // few pixels across loses whole scanlines to it: measured on flint, where the
  // minute hand is three pixels wide, the hand drew as a dotted line between 102
  // and 150 degrees and **did not draw at all** between 156 and 174 — sixteen
  // minutes of every hour with no minute hand, and the same sixteen again half a
  // turn later, the defect being a property of the slope rather than the
  // direction. An axis-aligned sliver is fine and so is a broad one, which is why
  // the hour hand never showed it and why noon and six o'clock looked correct.
  //
  // Widening the hand is not the fix: at five pixels it still broke at 156, and
  // the two hands on this display are told apart by width alone — spending that
  // difference on a rasteriser bug would cost more than the bug does.
  //
  // A stroked line is a different rasteriser and is continuous at every angle. It
  // is drawn at the *tip's* width, not the tail's, so it can never be wider than
  // the polygon it is reinforcing and the taper survives; and it stops a tip-width
  // short, because a thick Pebble line is capped with a semicircle and there is no
  // cap style to turn that off — at full length the cap would round the blunt tip
  // and lend the hour hand back the clearance it keeps from the ring.
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, stroke_px(tip_half * 2 + 1));
  graphics_draw_line(ctx, tail, point_on_circle(lo->center, len - tip_half, a));
}

void dial_draw_hands(GContext *ctx, const Layout *lo, const struct tm *t) {
  // The hour hand carries the minutes too, or it would jump an hour at a time
  // and stop agreeing with the ring it is pointing into.
  int32_t hour_a = angle_of_span((int32_t)(t->tm_hour % 12) * 60 + t->tm_min);
  int32_t min_a = (int32_t)t->tm_min * TRIG_MAX_ANGLE / 60;

  // Minute first, so the hour hand — which is "now", and the one element the
  // ring is read against — is the one on top where they cross.
  draw_hand(ctx, lo, min_a, lo->hand_m_len, lo->hand_m_half, COL_INK);
  draw_hand(ctx, lo, hour_a, lo->hand_h_len, lo->hand_h_half, COL_INDEX);

  graphics_context_set_fill_color(ctx, COL_INK);
  graphics_fill_circle(ctx, lo->center, lo->hub_r);
}
