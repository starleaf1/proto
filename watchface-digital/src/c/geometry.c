#include "geometry.h"
#include "theme.h"

#define EDGE_MARGIN_MIN 3

// Divide, rounding to the nearest integer instead of toward zero.
//
// Every stepped point on this face is a product scaled back down by a divisor, and C's
// truncation biases each one — never symmetrically, always back toward where the step
// started, by up to a whole pixel. A line's endpoint is a pixel address and there is a
// nearest one; landing on it is the difference between a notch inner-end sitting where
// the arithmetic says and sitting a pixel short of it.
//
// Signed on both operands: the trig products below are direction cosines and are
// negative over half the circle.
static int32_t div_round(int32_t num, int32_t den) {
  if (den < 0) { num = -num; den = -den; }
  return (num >= 0) ? (num + den / 2) / den : (num - den / 2) / den;
}

int16_t stroke_px(int16_t w) {
  if (w < 1) return 1;
  return (w % 2) ? w : (int16_t)(w - 1);
}

static int16_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  int32_t x = v, y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + v / x) / 2;
  }
  return (int16_t)x;
}

Track track_at(const Layout *lo, int32_t u) {
  if (u < 0) u = 0;
  if (u > STRIP_SPAN_S) u = STRIP_SPAN_S;
  int16_t y = lo->strip_top + (int16_t)div_round(u * (lo->strip_h - 1), STRIP_SPAN_S);
  return (Track){ .p = GPoint(lo->strip_x, y), .a = TRIG_MAX_ANGLE * 3 / 4 };
}

int32_t u_of_px(const Layout *lo, int16_t px) {
  if (lo->track_px <= 0) return 0;
  return div_round((int32_t)px * STRIP_SPAN_S, lo->track_px);
}

GPoint step_in(GPoint p, int32_t a, int32_t d) {
  return GPoint(p.x - (int16_t)div_round(sin_lookup(a) * d, TRIG_MAX_RATIO),
                p.y + (int16_t)div_round(cos_lookup(a) * d, TRIG_MAX_RATIO));
}

GPoint step_side(GPoint p, int32_t a, int32_t d) {
  return GPoint(p.x + (int16_t)div_round(cos_lookup(a) * d, TRIG_MAX_RATIO),
                p.y + (int16_t)div_round(sin_lookup(a) * d, TRIG_MAX_RATIO));
}

// How far outboard of the track a band's outer edge sits.
//
// It used to arrive by accident. A band was one thick stroked line per covered minute,
// and a thick line's round cap ran stroke/2 — one pixel, on all three displays — past
// the endpoint it was asked for. The accident was worth keeping: it pushes the band hard
// against the screen edge and lines its outer edge up with an hour notch's own cap. So
// it is asked for now instead of inherited.
#define BAND_OUT_PX 1

// A band on the track: everything between `u0` and `u1`, `depth` px deep, ending square.
//
// Square is the whole point of filling it rather than stroking it. The per-minute line
// this replaced put a semicircular cap on both ends of every one of its samples, and the
// two samples that had nothing overlapping them — the first and last minute of a run —
// kept theirs, so every band came off the display with a pixel taken out of all four
// corners. There is no cap style to set on a Pebble line; the fix is not to be drawing a
// line.
//
// A filled rect, ending on the ray, so a band's end lines up with a notch at the same `u`.
void fill_track_band(GContext *ctx, const Layout *lo, int32_t u0, int32_t u1,
                     int16_t depth, GColor col) {
  if (u0 < 0) u0 = 0;
  if (u1 > STRIP_SPAN_S) u1 = STRIP_SPAN_S;
  if (u1 < u0 || depth < 1) return;

  graphics_context_set_fill_color(ctx, col);
  // The +1 is the track's own column: `depth` is measured from it, so the band covers
  // strip_x - BAND_OUT_PX through strip_x + depth *inclusive*.
  int16_t y0 = track_at(lo, u0).p.y;
  int16_t y1 = track_at(lo, u1).p.y;
  graphics_fill_rect(ctx, GRect(lo->strip_x - BAND_OUT_PX, y0,
                                depth + BAND_OUT_PX + 1, y1 - y0 + 1),
                     0, GCornerNone);
}

GRect text_plate(GRect box, GFont font, const char *text) {
  if (!text || !text[0]) return GRect(box.origin.x, box.origin.y, 0, 0);
  GSize ts = graphics_text_layout_get_content_size(
      text, font, box, GTextOverflowModeFill, ROW_ALIGN);
  // A pixel of pad on the leading edge, two on the trailing one. `zone` carries exactly
  // two pixels of clearance past the pointer's base, so a plate reaching two back would
  // sit on the pointer itself.
  int16_t pad = 2;
  return GRect(box.origin.x - 1, box.origin.y, ts.w + 1 + pad, box.size.h);
}

void knock_out(GContext *ctx, GRect r) {
  if (r.size.w <= 0 || r.size.h <= 0) return;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

// How far outside the shape a halo reaches, in pixels. Small on purpose: it exists
// to make an edge findable, not to clear a moat.
#define HALO_PX 2

// How much of a wedge's base is drawn as a band rather than left to the polygon.
// See draw_track_wedge(): two pixels covers the outboard column a band gets and
// the one a polygon fill and a rect fill disagree about at the same coordinate,
// on every display.
#define WEDGE_CAP_D 2

// Move p away from c by d px. Returns p unchanged if they coincide.
static GPoint grow_from(GPoint p, GPoint c, int16_t d) {
  int32_t dx = p.x - c.x, dy = p.y - c.y;
  int16_t len = isqrt32(dx * dx + dy * dy);
  if (len == 0) return p;
  return GPoint(p.x + (int16_t)div_round(dx * d, len),
                p.y + (int16_t)div_round(dy * d, len));
}

static void fill_poly_haloed(GContext *ctx, GPoint *pts, int n, GColor ink, bool halo);

void draw_tri(GContext *ctx, GPoint p0, GPoint p1, GPoint p2,
              GColor ink, bool filled, int16_t stroke_w, bool halo) {
  GPoint pts[3] = { p0, p1, p2 };
  GPathInfo info = { .num_points = 3, .points = pts };
  GPath *path = gpath_create(&info);
  if (!path) return;

  if (halo && filled) {
    gpath_destroy(path);
    // The grown-centroid halo and the fill both live in fill_poly_haloed(), which the
    // wedge shares. See there for why a halo is never a wide stroked outline.
    GPoint hp[3] = { p0, p1, p2 };
    fill_poly_haloed(ctx, hp, 3, ink, true);
    return;
  } else if (halo) {
    // A hollow shape has to keep whatever is under it showing through, so its halo
    // stays an outline. The miter overshoot fill_poly_haloed() avoids is harmless here:
    // the only hollow
    // shape is flint's upcoming marker, whose sharp vertex points inward into the free
    // area rather than out at the track.
    graphics_context_set_stroke_color(ctx, COL_BG);
    graphics_context_set_stroke_width(ctx, stroke_w + 2);
    gpath_draw_outline(ctx, path);
  }
  if (filled) {
    graphics_context_set_fill_color(ctx, ink);
    gpath_draw_filled(ctx, path);
  } else {
    graphics_context_set_stroke_color(ctx, ink);
    graphics_context_set_stroke_width(ctx, stroke_w);
    gpath_draw_outline(ctx, path);
  }
  gpath_destroy(path);
}

// A filled polygon with an optional background halo — the shared body of draw_tri()
// and draw_track_wedge(), and the halo is the reason it is shared. `n` is at most 4.
static void fill_poly_haloed(GContext *ctx, GPoint *pts, int n, GColor ink, bool halo) {
  if (n < 3 || n > 4) return;

  if (halo) {
    // The same shape grown HALO_PX away from its own centroid, filled in the
    // background colour and then covered by the shape — which leaves a ring of about
    // that width around it, and never more.
    //
    // The obvious version of this is a background-coloured *outline* stroked wider
    // than the shape, and it is a trap. A stroked path miters its corners, and the
    // miter at a sharp vertex runs far past the vertex itself — at the pointer's
    // sharp tip a 4px stroke overshoots by more than five pixels. That is enough to
    // reach past the tip's clearance and cut a background-coloured slot clean through
    // an appointment band, which is exactly what it did. Growing the vertices instead
    // bounds the halo by construction, at any angle and any sharpness.
    int32_t sx = 0, sy = 0;
    for (int i = 0; i < n; i++) { sx += pts[i].x; sy += pts[i].y; }
    GPoint c = GPoint((int16_t)(sx / n), (int16_t)(sy / n));
    GPoint hp[4];
    for (int i = 0; i < n; i++) hp[i] = grow_from(pts[i], c, HALO_PX);
    GPathInfo hinfo = { .num_points = (uint32_t)n, .points = hp };
    GPath *hpath = gpath_create(&hinfo);
    if (hpath) {
      graphics_context_set_fill_color(ctx, COL_BG);
      gpath_draw_filled(ctx, hpath);
      gpath_destroy(hpath);
    }
  }

  GPathInfo info = { .num_points = (uint32_t)n, .points = pts };
  GPath *path = gpath_create(&info);
  if (!path) return;
  graphics_context_set_fill_color(ctx, ink);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// A point marker: a wedge off the track, `depth` px deep, `half_len` of track either
// side of `u` at its base, and `tip_half` either side at its inner end.
//
// The tip is blunt, and that is the whole of why this is not draw_tri(). A marker used
// to be a triangle with depth about three times its half-base, which means the inner
// third of it tapers below two pixels and draws as a hairline — thinner than the minute
// notches it exists to stand out from. Measured on emery: the shape came off the display
// as a blob with a whisker, and the whisker was the apex, which is the end carrying the
// time. Stopping the taper at `tip_half` keeps the wedge at least three pixels across
// for its whole length without giving up any depth, so the depth ladder in geometry.h
// and everything `zone` is measured from stay where they were.
//
// It also buys a second channel for a merged marker. Depth alone said "more than one"
// in a 25% difference — 12 px against 15 on flint — which is not a difference a reader
// can see without the other one to compare against. A blunter tip is visible on its own.
void draw_track_wedge(GContext *ctx, const Layout *lo, int32_t u,
                      int16_t depth, int16_t half_len, int16_t tip_half,
                      GColor ink, bool halo) {
  if (tip_half < 1) tip_half = 1;
  if (tip_half > half_len) tip_half = half_len;

  Track t = track_at(lo, u);
  GPoint tip = step_in(t.p, t.a, depth);
  GPoint pts[4] = {
    step_side(t.p, t.a,  half_len),
    step_side(tip,  t.a,  tip_half),
    step_side(tip,  t.a, -tip_half),
    step_side(t.p, t.a, -half_len),
  };
  fill_poly_haloed(ctx, pts, 4, ink, halo);

  // Then draw the base again, as a band of its own.
  //
  // The polygon's base lands on the track, and a band's outer edge does not: it
  // sits BAND_OUT_PX outboard of it, hard against the screen edge and level with
  // an hour notch's cap. Measured on emery, that plus the pixel a gpath fill and a
  // rect fill disagree about at the same coordinate put the base two pixels inboard
  // of the band it was sitting on — enough for the marker to read as floating inside
  // the appointment rather than standing on it.
  //
  // None of it is worth arithmetic, and arithmetic could not fix the last of it
  // anyway — two rasterisers at one coordinate do not agree by being asked to. The
  // base is drawn by the call that draws the edge it has to match, so the two land
  // on the same pixels by construction.
  int16_t cap_d = WEDGE_CAP_D;
  if (cap_d > depth) cap_d = depth;
  if (cap_d > 0) {
    int32_t du = u_of_px(lo, half_len);
    fill_track_band(ctx, lo, u - du, u + du, cap_d, ink);
  }
}

// Clamp a full-width row to what the strip and the display leave it at that height.
//
// The left edge is the strip's plus `zone` on every display, because the strip is a
// vertical line on every display. The right edge is the screen's less the margin on a
// rectangle; on the circle it is the chord, measured at whichever edge of the row sits
// *furthest* from the vertical centre — pessimistic within the row, which is what is
// wanted.
static GRect fit_row(GRect box, const Layout *lo) {
  int16_t left = lo->strip_x + lo->zone;
#ifdef PBL_ROUND
  int16_t mid = box.origin.y + box.size.h / 2;
  int16_t far = (mid < lo->center.y) ? box.origin.y
                                    : (int16_t)(box.origin.y + box.size.h);
  int16_t dy = far - lo->center.y;
  if (dy < 0) dy = -dy;
  int16_t r = lo->radius - lo->margin;
  int16_t half = (dy < r) ? isqrt32((int32_t)r * r - (int32_t)dy * dy) : 0;
  int16_t right = lo->center.x + half;
#else
  int16_t right = lo->bounds.origin.x + lo->bounds.size.w - lo->margin;
#endif
  if (right < left) right = left;
  return GRect(left, box.origin.y, right - left, box.size.h);
}

#ifdef PBL_ROUND
// Put gabbro's strip at `x`, running the full height of the display. The glass is a
// circle inside that square, so both ends of the track are off the glass and the ruler
// runs out under the edge rather than stopping short of it — see STRIP_BACK_S.
static void place_strip(Layout *lo, int16_t x) {
  lo->strip_x = x;
  lo->strip_top = lo->bounds.origin.y;
  lo->strip_h = lo->bounds.size.h;
  lo->track_px = lo->strip_h;
}
#endif

// Where the clock's box goes, for the strip as it is placed: its ink level with the
// pointer's body.
//
// A content box is not symmetric about the glyphs in it: Pebble's font resources
// carry their own ascent and descent, and a digits-and-colon subset never puts
// anything below the baseline, so the box has more slack above the ink than
// below and centring the box leaves the digits sitting low. Measured off a
// flint screenshot: with the box centred on the pointer at y 43, the ink came
// out spanning 31..59 — centre 45, two pixels down.
//
// The correction is a fraction of the numeral's own height rather than a pixel
// count, so it scales with the font, and it is measured rather than derived: the
// TTF's hhea metrics predict the opposite sign, because what the SDK lays out to
// is the generated resource's metrics and not the source font's.
//
// And it goes on the *pointer*, not on the point of the track the pointer marks.
// The ray is horizontal on every display now, so those are the same y, but the
// measurement stays on the shape: the eye lines up with the shape, so the shape is
// what to measure — whichever shape this display's "now" is. On colour that is a rule
// struck across the strip and its middle is half its length in; on flint it is the
// wedge, whose body starts past the notch zone.
static int16_t clock_top(const Layout *lo, int16_t ns_h, int16_t num_h, int16_t span_top) {
  Track ptr = track_at(lo, STRIP_BACK_S);
#ifdef PBL_COLOR
  int16_t p_mid = lo->rule_len / 2;
#else
  int16_t p_mid = lo->ptr_tip + lo->notch_len * POINTER_LEN_PCT / 100 / 2;
#endif
  int16_t py = step_in(ptr.p, ptr.a, p_mid).y;
  int16_t top = py - num_h / 2 - ns_h / 20;
  if (top < span_top) top = span_top;
  return top;
}

Layout layout_compute(GRect bounds, GFont num_font, GFont date_font,
                      GFont slot_font, GFont tick_font) {
  Layout lo;
  lo.bounds = bounds;
  lo.center = GPoint(bounds.origin.x + bounds.size.w / 2,
                     bounds.origin.y + bounds.size.h / 2);
  lo.radius = (bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h) / 2;

  int16_t margin = lo.radius / 24;
  if (margin < EDGE_MARGIN_MIN) margin = EDGE_MARGIN_MIN;
  lo.margin = margin;
  lo.notch_len = lo.radius / 9;

  // The hour labels' width — measured, not chosen. "00" is the widest an hour ever
  // gets, in 24-hour style or 12; the 12-hour case just centres a narrower glyph in the
  // same lane, so no label moves as the day goes on.
  GSize ls = graphics_text_layout_get_content_size(
      "00", tick_font, GRect(0, 0, bounds.size.w, bounds.size.h),
      GTextOverflowModeFill, GTextAlignmentCenter);
  lo.label_w = ls.w;

  // The depth ladder, and its rungs are in a different order on the two kinds of
  // display because "now" is a different element on them. `zone` is the end of it
  // either way: what the strip claims inward from the track, all of it, reserved
  // before any row is placed, because a row placed into it would be knocked out from
  // under the elements the timeline is read off.
#ifdef PBL_COLOR
  // The rule strikes through exactly what the strip draws and no further, and a merged
  // marker is the deepest of that. The labels then start past all of it, so nothing is
  // ever over or under them.
  lo.rule_len = lo.notch_len * MARKER_GROUP_PCT / 100;
  lo.label_x = lo.rule_len + margin;
  lo.zone = lo.label_x + lo.label_w + margin;
#else
  // flint puts the labels between the ruler and the wedge — into the free area the wedge
  // was already claiming, not past it. So the wedge keeps the tip gap and the length it
  // always had, its base still closes the zone, and the labels cost the content column
  // nothing at all: `zone` is what it was before there were any numbers on this face.
  // The two overlap in depth and are dealt with in time instead, by dropping the label
  // the wedge lands on. See draw_hour_label.
  lo.label_x = lo.notch_len + margin;
  lo.ptr_tip = lo.notch_len + POINTER_TIP_GAP;
  int16_t label_end = lo.label_x + lo.label_w;
  int16_t ptr_end = lo.ptr_tip + lo.notch_len * POINTER_LEN_PCT / 100;
  lo.zone = (label_end > ptr_end ? label_end : ptr_end) + margin;
#endif

  lo.strip_x = bounds.origin.x + margin;
  lo.strip_top = bounds.origin.y + margin;
  lo.strip_h = bounds.size.h - 2 * margin;
  lo.track_px = lo.strip_h;

  // Representative strings, not the live ones: no row may shift as the day or the
  // countdown changes. "+00:00" is no longer anything the countdown draws: the sign
  // gave way to a filled box, and the box's margin is narrower than the '+' was. It
  // stays as the slot rows' stand-in width because gabbro's span_bot is solved from
  // it, and a narrower one would lower the warnings row into a narrower chord for
  // readings that were never measured against it. "00:00" is also the widest the clock ever gets, and the clock
  // is what the whole column's width is budgeted against — Orbitron is a wide face
  // and five glyphs of it is the binding constraint on this layout.
  GRect measure = GRect(0, 0, bounds.size.w, bounds.size.h);
  GSize ns = graphics_text_layout_get_content_size(
      "00:00", num_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize ds = graphics_text_layout_get_content_size(
      "MON 22", date_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize ss = graphics_text_layout_get_content_size(
      "+00:00", slot_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);

  int16_t num_h = ns.h + 6;
  int16_t date_h = ds.h + 4;
  int16_t slot_h = ss.h + 4;

#ifdef PBL_ROUND
  // gabbro's strip is a vertical line the height of the display, left of centre by as
  // much as the clock needs and no more.
  //
  // It cannot simply go where the rectangles put theirs. Hard against the left of a
  // circle the glass shows only a short stretch of a vertical line, and the strip would
  // lose most of its visible length for nothing; in the middle of the glass there is no room for "00:00" beside it. So
  // the x is solved: start at the centre and step left until the clock's row, level
  // with the pointer, is as wide as the clock. The further left, the less of the track
  // the glass shows, so the first x that fits is the one that costs the strip least.
  int16_t r = lo.radius - margin;
  int16_t x_min = bounds.origin.x + margin;
  int16_t min_half = r * 45 / 100;
  int16_t span_top = lo.center.y - isqrt32((int32_t)r * r - (int32_t)min_half * min_half);
  int16_t x = lo.center.x;
  for (; x > x_min; x--) {
    place_strip(&lo, x);
    int16_t t = clock_top(&lo, ns.h, num_h, span_top);
    if (fit_row(GRect(bounds.origin.x, t, bounds.size.w, num_h), &lo).size.w
        >= ns.w + margin) break;
  }
  place_strip(&lo, x);

  // The bottom of the usable span: the lowest height at which the chord still leaves a
  // row as wide as a slot's representative string. Below that the glass narrows faster
  // than the rows can shrink, and the warnings row pinned there would clip.
  int16_t need_half = lo.strip_x + lo.zone + ss.w + margin - lo.center.x;
  if (need_half < 0) need_half = 0;
  if (need_half > r) need_half = r;
  int16_t span_bot = lo.center.y + isqrt32((int32_t)r * r - (int32_t)need_half * need_half);
#else
  int16_t span_top = bounds.origin.y + margin;
  int16_t span_bot = bounds.origin.y + bounds.size.h - margin;
#endif

  // The countdown is an ordinary row now. It used to reserve a strip under the digits
  // for a progress bar — unconditionally, because the bar only drew while an
  // appointment was running and a row that changed height when it appeared would move
  // the countdown out from under the reader's eye at the one moment they are watching
  // it. The sign in front of the digits says the same thing on the line itself, so the
  // reservation and the gap above it are both gone. The clock is pinned to the pointer
  // and the warnings row to the bottom, so what that frees goes where this layout puts
  // all its slack: the run between the countdown and the warnings, which nav centres
  // itself in — and the gap clamps below get that much more headroom before they bite.
  int16_t count_h = slot_h;

  // The date and the countdown still belong to the clock — they qualify it — but they
  // are given air now rather than tucked under it. A fifth and a quarter of the row
  // above, which scales with the font rather than with the display.
  //
  // These are signed, and that is what replaced the two tuck constants that used to
  // live here. Spacing on this face was *negative* — the rows overlapped by a tenth of
  // the row above, on the reasoning that a content box is taller than the ink in it, so
  // the gaps read wider than they measure. True, and it was overdone: the ink slack
  // hides a few pixels, not a whole row's tenth. Starting positive and letting the
  // clamps below drive them back down through zero into a tuck keeps the old
  // behaviour as the failure mode without keeping it as the default.
  int16_t gap_n = ns.h / 5;
  int16_t gap_d = ds.h / 4;

  // The clock's ink goes level with the pointer's body — see clock_top().
  int16_t top = clock_top(&lo, ns.h, num_h, span_top);

  // Pinned at both ends. The clock's centre goes on the pointer; the warnings row
  // goes on the bottom.
  lo.warn_box = GRect(bounds.origin.x, span_bot - slot_h, bounds.size.w, slot_h);

  int16_t room = lo.warn_box.origin.y - top - 2;
  int16_t fixed = num_h + date_h + count_h;

  // The air may not be taken out of the nav row. Nav still reserves nothing in the
  // flow — nothing below the countdown moves whether it draws or not, which is what
  // seats five rows in four rows' height — but there is a difference between a row
  // that claims no space and a row whose space is fair game for spacing above it. What
  // is left after the three fixed rows, less a nav-sized hole, is the gap budget.
  int16_t budget = room - fixed - (slot_h + 2);
  if (budget < 0) budget = 0;
  int16_t want = gap_n + gap_d;
  if (want > budget) {
    // Proportionally, so the two gaps keep their ratio as they give way.
    gap_n = (int16_t)((int32_t)gap_n * budget / want);   // want > budget >= 0, so != 0
    gap_d = budget - gap_n;
  }

  // A font set that does not fit has to show up as rows abutting, never as text
  // crossing another row. Past zero the gaps go negative and are tucks again, which is
  // exactly what this clamp used to do to the tucks themselves.
  int16_t need = fixed + gap_n + gap_d;
  if (need > room) {
    int16_t over = (need - room + 1) / 2;
    gap_n -= over;
    gap_d -= over;
  }

  int16_t y = top;
  lo.num_box = GRect(bounds.origin.x, y, bounds.size.w, num_h);
  y += num_h + gap_n;
  lo.date_box = GRect(bounds.origin.x, y, bounds.size.w, date_h);
  y += date_h + gap_d;
  lo.count_box = GRect(bounds.origin.x, y, bounds.size.w, count_h);
  y += count_h;

  // Nav takes the slack between the countdown and the warnings, and reserves
  // nothing. Every row above it is flowed from the pointer down and the warnings row
  // is pinned to the bottom, so whether nav draws or not, no other row moves — which
  // is the whole reason five rows fit where four used to.
  int16_t slack = lo.warn_box.origin.y - y;
  int16_t nav_y = (slack > slot_h) ? y + (slack - slot_h) / 2 : y;
  lo.nav_box = GRect(bounds.origin.x, nav_y, bounds.size.w, slot_h);

  lo.num_box   = fit_row(lo.num_box,   &lo);
  lo.date_box  = fit_row(lo.date_box,  &lo);
  lo.count_box = fit_row(lo.count_box, &lo);
  lo.nav_box   = fit_row(lo.nav_box,   &lo);
  lo.warn_box  = fit_row(lo.warn_box,  &lo);

  return lo;
}
