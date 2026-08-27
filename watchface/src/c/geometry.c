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
// negative over half the arc.
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

#ifdef PBL_ROUND
// The arc the strip is traced on: a quarter turn down the left of the circle, from
// 315 degrees at the top to 225 at the bottom.
//
// The span is a real tuning knob and 90 degrees is not arbitrary. The arc bulges
// hard left at nine o'clock and curls back in at both ends, so a wider span pushes
// its ends rightward into the content column at exactly the height the clock wants
// — 120 degrees costs a whole font size. Narrower than 90 and the ends crowd the
// clock from the other direction, because the circle pinches toward the top.
#define ARC_TOP_DEG   315
#define ARC_SPAN_DEG   90

// Where `u` lands on the arc, in trig angles rather than in whole degrees.
//
// One function, because two callers have to agree to the pixel: a band's square end is
// placed by this and so is the notch or the marker it has to line up with. A whole
// degree of gabbro's arc is 2.2 px, which is a rounding either of them would show.
static int32_t arc_angle(int32_t u) {
  // Later is lower, so the angle *decreases* down the arc.
  return DEG_TO_TRIGANGLE(ARC_TOP_DEG)
       - div_round(u * DEG_TO_TRIGANGLE(ARC_SPAN_DEG), STRIP_SPAN_S);
}

// Point at radius r, angle a. int32 math before the int16 cast avoids overflow —
// sin_lookup * r reaches ~7M at gabbro's 125px radius.
static GPoint point_on_circle(GPoint c, int32_t r, int32_t a) {
  return GPoint(c.x + (int16_t)div_round(sin_lookup(a) * r, TRIG_MAX_RATIO),
                c.y - (int16_t)div_round(cos_lookup(a) * r, TRIG_MAX_RATIO));
}
#endif

Track track_at(const Layout *lo, int32_t u) {
  if (u < 0) u = 0;
  if (u > STRIP_SPAN_S) u = STRIP_SPAN_S;
#ifdef PBL_ROUND
  int32_t a = arc_angle(u);
  return (Track){ .p = point_on_circle(lo->center, lo->arc_r, a), .a = a };
#else
  int16_t y = lo->strip_top + (int16_t)div_round(u * (lo->strip_h - 1), STRIP_SPAN_S);
  return (Track){ .p = GPoint(lo->strip_x, y), .a = TRIG_MAX_ANGLE * 3 / 4 };
#endif
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
// One expression per display shape, and it is the same split track_at() makes: a filled
// rect on a rectangle, an annular sector on gabbro's arc. Both end on the ray, so a
// band's end lines up with a notch at the same `u` on either shape.
void fill_track_band(GContext *ctx, const Layout *lo, int32_t u0, int32_t u1,
                     int16_t depth, GColor col) {
  if (u0 < 0) u0 = 0;
  if (u1 > STRIP_SPAN_S) u1 = STRIP_SPAN_S;
  if (u1 < u0 || depth < 1) return;

  graphics_context_set_fill_color(ctx, col);
#ifdef PBL_ROUND
  // Radii arc_r + BAND_OUT_PX down to arc_r - depth, cut off on the two rays.
  // graphics_fill_radial sweeps clockwise from start to end and draws nothing if the
  // start is the larger, and the arc's angle *decreases* as u grows — so the later end
  // of the band is the one to start from.
  int16_t r = lo->arc_r + BAND_OUT_PX;
  graphics_fill_radial(ctx, GRect(lo->center.x - r, lo->center.y - r, 2 * r, 2 * r),
                       GOvalScaleModeFitCircle, depth + BAND_OUT_PX,
                       arc_angle(u1), arc_angle(u0));
#else
  // The +1 is the track's own column: `depth` is measured from it, so the band covers
  // strip_x - BAND_OUT_PX through strip_x + depth *inclusive*.
  int16_t y0 = track_at(lo, u0).p.y;
  int16_t y1 = track_at(lo, u1).p.y;
  graphics_fill_rect(ctx, GRect(lo->strip_x - BAND_OUT_PX, y0,
                                depth + BAND_OUT_PX + 1, y1 - y0 + 1),
                     0, GCornerNone);
#endif
}

GRect text_plate(GRect box, GFont font, const char *text) {
  if (!text || !text[0]) return GRect(box.origin.x, box.origin.y, 0, 0);
  GSize ts = graphics_text_layout_get_content_size(
      text, font, box, GTextOverflowModeFill, ROW_ALIGN);
  // A pixel of pad on the leading edge where the row is left-aligned, two everywhere
  // else. `zone` carries exactly two pixels of clearance past the pointer's base, so
  // a left-aligned plate reaching two back would sit on the pointer itself.
  int16_t pad = 2;
  int16_t x = PBL_IF_ROUND_ELSE(box.origin.x + (box.size.w - ts.w) / 2 - pad,
                                box.origin.x - 1);
  int16_t w = PBL_IF_ROUND_ELSE(ts.w + 2 * pad, ts.w + 1 + pad);
  return GRect(x, box.origin.y, w, box.size.h);
}

void knock_out(GContext *ctx, GRect r) {
  if (r.size.w <= 0 || r.size.h <= 0) return;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

// How far outside the shape a halo reaches, in pixels. Small on purpose: it exists
// to make an edge findable, not to clear a moat.
#define HALO_PX 2

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
}

// Clamp a full-width row to what the strip and the display leave it at that height.
//
// The strip claims a fixed depth inward from the track, so on a rectangle this is a
// constant inset from the left plus the edge margin on the right. On a circle it is
// neither: the arc's x varies with y and so does the chord's right edge, and both are
// tightest at whichever edge of the row sits *furthest* from the vertical centre —
// the arc is leftmost at nine o'clock, so a row above or below that has the arc
// pushed rightward into it, and the chord narrowing at the same time. Measuring at
// that edge is pessimistic within the row, which is what is wanted.
//
// One chord, measured on the one circle the strip and the glass share, with `zone` taken
// off the left. That is also what keeps the round display's rows in a column: `left` and
// `right` are symmetric about center.x + zone/2 at every height, so rows whose available
// width varies by a factor of two still share a centre. It is the property the circle
// offers in place of the shared left edge it will not have — see ROW_ALIGN.
static GRect fit_row(GRect box, const Layout *lo) {
#ifdef PBL_ROUND
  int16_t mid = box.origin.y + box.size.h / 2;
  int16_t far = (mid < lo->center.y) ? box.origin.y
                                    : (int16_t)(box.origin.y + box.size.h);
  int16_t dy = far - lo->center.y;
  if (dy < 0) dy = -dy;
  int16_t half = (dy < lo->arc_r)
      ? isqrt32((int32_t)lo->arc_r * lo->arc_r - (int32_t)dy * dy) : 0;
  int16_t left  = lo->center.x - half + lo->zone;
  int16_t right = lo->center.x + half;
  if (right < left) right = left;
  return GRect(left, box.origin.y, right - left, box.size.h);
#else
  int16_t left = lo->strip_x + lo->zone;
  int16_t right = lo->bounds.origin.x + lo->bounds.size.w - lo->margin;
  if (right < left) right = left;
  return GRect(left, box.origin.y, right - left, box.size.h);
#endif
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
#ifndef PBL_COLOR
  int16_t p_len = lo.notch_len * POINTER_LEN_PCT / 100;
#endif
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
  int16_t ptr_end = lo.ptr_tip + p_len;
  lo.zone = (label_end > ptr_end ? label_end : ptr_end) + margin;
#endif

#ifdef PBL_ROUND
  lo.arc_r = lo.radius - margin;
  // A quarter of the circumference. TRIG_MAX_ANGLE would be the long way round
  // for one constant.
  lo.track_px = (int16_t)((int32_t)lo.arc_r * 157 / 100);
#else
  lo.strip_x = bounds.origin.x + margin;
  lo.strip_top = bounds.origin.y + margin;
  lo.strip_h = bounds.size.h - 2 * margin;
  lo.track_px = lo.strip_h;
#endif

  // Representative strings, not the live ones: no row may shift as the day or the
  // countdown changes. "00:00" is also the widest the clock ever gets, and the clock
  // is what the whole column's width is budgeted against — Orbitron is a wide face
  // and five glyphs of it is the binding constraint on this layout.
  GRect measure = GRect(0, 0, bounds.size.w, bounds.size.h);
  GSize ns = graphics_text_layout_get_content_size(
      "00:00", num_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize ds = graphics_text_layout_get_content_size(
      "MON 22", date_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize ss = graphics_text_layout_get_content_size(
      "00:00", slot_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);

  int16_t span_top = bounds.origin.y + margin;
  int16_t span_bot = bounds.origin.y + bounds.size.h - margin;
#ifdef PBL_ROUND
  // The circle pinches the rows in as well as the strip; keep them where a chord is
  // still wide enough to hold text at all.
  int16_t min_half = lo.arc_r * 45 / 100;
  int16_t dy_max = isqrt32((int32_t)lo.arc_r * lo.arc_r - (int32_t)min_half * min_half);
  span_top = lo.center.y - dy_max;
  span_bot = lo.center.y + dy_max;
#endif

  // The progress bar under the countdown, reserved whether or not it is drawn. It is
  // only there when an appointment is running, and a row that changed height when it
  // appeared would move the countdown out from under the reader's eye at the one
  // moment they are watching it.
  //
  // A sixth put this on 3 px for all three displays — the floor caught flint and emery
  // both, and gabbro's own sixth rounded back down to 3 — so the one element that has to
  // read as a *bar* never grew past the thickness of an hour notch.
  //
  // Five is the floor now, and five is what all three land on, because an outlined track
  // is what draw_progress() draws and an outline needs an inside. At three the top and
  // bottom rules leave one pixel between them and the whole thing came off flint as a
  // solid slab with a scratch in it — measured, and worse than the hairline it replaced.
  // One pixel of rule, three of interior, one of rule is the smallest honest bar.
  lo.bar_h = stroke_px(ss.h / 4);
  if (lo.bar_h < 5) lo.bar_h = 5;

  int16_t num_h = ns.h + 6;
  int16_t date_h = ds.h + 4;
  int16_t slot_h = ss.h + 4;
  int16_t count_h = slot_h + PROGRESS_GAP + lo.bar_h;

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

  // Pinned at both ends. The clock's centre goes on the pointer; the warnings row
  // goes on the bottom.
  lo.warn_box = GRect(bounds.origin.x, span_bot - slot_h, bounds.size.w, slot_h);

  // The clock's *ink* goes on the pointer, not its content box.
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
  // And it goes on the *pointer*, not on the point of the track the pointer marks.
  //
  // Those are the same y on a rectangle, where the ray is horizontal and the mark
  // reaches straight in. On gabbro's arc they are not: the ray at the quarter mark
  // runs down and to the right, so the mark's body sits some way below the arc point
  // it touches, and a clock levelled with that point reads as sitting above the thing
  // it is supposed to line up with. The eye lines up with the shape, so the shape is
  // what to measure — whichever shape this display's "now" is. On colour that is a
  // rule struck across the strip and its middle is half its length in; on flint it is
  // the wedge, whose body starts past the notch zone.
  Track ptr = track_at(&lo, STRIP_BACK_S);
  int16_t p_mid = PBL_IF_COLOR_ELSE(lo.rule_len / 2, lo.ptr_tip + p_len / 2);
  int16_t py = step_in(ptr.p, ptr.a, p_mid).y;

  int16_t top = py - num_h / 2 - ns.h / 20;
  if (top < span_top) top = span_top;

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
