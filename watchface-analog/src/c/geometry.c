#include "geometry.h"
#include "theme.h"

#define EDGE_MARGIN_MIN 3

// How far outside a shape a halo reaches. Small on purpose: it exists to make
// an edge findable, not to clear a moat.
#define HALO_PX 2

// How much of a wedge's base is drawn as an annular sector rather than left to
// the polygon. See ring_wedge(): two pixels covers the chord's sagitta — under a
// pixel on every platform — and the pixel that a polygon fill and a radial fill
// disagree about at the same radius.
#define WEDGE_CAP_D 2

// Divide, rounding to the nearest integer instead of toward zero.
//
// Every stepped point on this face is a product scaled back down by a divisor,
// and C's truncation biases each one — never symmetrically, always back toward
// where the step started, by up to a whole pixel. Signed on both operands: the
// trig products below are direction cosines and are negative over half the dial.
static int32_t div_round(int32_t num, int32_t den) {
  if (den < 0) { num = -num; den = -den; }
  return (num >= 0) ? (num + den / 2) / den : (num - den / 2) / den;
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

int16_t stroke_px(int16_t w) {
  if (w < 1) return 1;
  return (w % 2) ? w : (int16_t)(w - 1);
}

GPoint point_step(GPoint p, int32_t a, int32_t d) {
  return GPoint(p.x + (int16_t)div_round(sin_lookup(a) * d, TRIG_MAX_RATIO),
                p.y - (int16_t)div_round(cos_lookup(a) * d, TRIG_MAX_RATIO));
}

GPoint point_on_circle(GPoint c, int32_t r, int32_t a) {
  return point_step(c, a, r);
}

// ---------------------------------------------------------------------------
// Angles
// ---------------------------------------------------------------------------

int32_t angle_of_span(int32_t minutes) {
  return div_round(minutes * TRIG_MAX_ANGLE, DIAL_SPAN_MIN);
}

int32_t span_of_angle(int32_t a) {
  return div_round(a * DIAL_SPAN_MIN, TRIG_MAX_ANGLE);
}

int32_t angle_of_time(time_t t) {
  struct tm lt = *localtime(&t);
  int32_t mins = (int32_t)lt.tm_hour * 60 + lt.tm_min;
  return angle_of_span(mins % DIAL_SPAN_MIN);
}

int32_t angle_of_px(int16_t px, int16_t r) {
  if (r <= 0) return 0;
  // 2*pi*r, to three decimal places. The rim is the longest thing on the face
  // and a percent of it is two pixels on gabbro.
  int32_t circ = div_round((int32_t)r * 6283, 1000);
  if (circ <= 0) return 0;
  return div_round((int32_t)px * TRIG_MAX_ANGLE, circ);
}

// ---------------------------------------------------------------------------
// Ring primitives
// ---------------------------------------------------------------------------

static void fill_sector(GContext *ctx, GPoint c, int16_t r_outer, int16_t depth,
                        int32_t a0, int32_t a1) {
  if (a1 <= a0) return;
  graphics_fill_radial(ctx,
                       GRect(c.x - r_outer, c.y - r_outer, 2 * r_outer, 2 * r_outer),
                       GOvalScaleModeFitCircle, depth, a0, a1);
}

void fill_ring_band(GContext *ctx, const Layout *lo, int32_t a0, int32_t a1,
                    int16_t r_outer, int16_t depth, GColor col) {
  if (a1 <= a0 || depth < 1 || r_outer < 1) return;
  int32_t span = a1 - a0;
  if (span > TRIG_MAX_ANGLE) span = TRIG_MAX_ANGLE;

  // Normalise the start into one turn and carry the span with it, rather than
  // reducing both ends independently — which would put the end *behind* the
  // start for every band that crosses twelve, and graphics_fill_radial draws
  // nothing at all when the start is the larger.
  a0 = ((a0 % TRIG_MAX_ANGLE) + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
  a1 = a0 + span;

  graphics_context_set_fill_color(ctx, col);
  if (a1 <= TRIG_MAX_ANGLE) {
    fill_sector(ctx, lo->center, r_outer, depth, a0, a1);
  } else {
    // Split at twelve. The window is half a turn and rides with the hour hand,
    // so it straddles the boundary for six hours out of every twelve; this is
    // the ordinary case, not the edge one.
    fill_sector(ctx, lo->center, r_outer, depth, a0, TRIG_MAX_ANGLE);
    fill_sector(ctx, lo->center, r_outer, depth, 0, a1 - TRIG_MAX_ANGLE);
  }
}

void draw_ring_arc(GContext *ctx, const Layout *lo, int32_t a0, int32_t a1,
                   int16_t r, GColor col) {
  // A one-pixel annulus rather than graphics_draw_arc: the rail has to line up
  // to the pixel with the inner edge of every band that sits on it, and the
  // only way to be sure of that is to draw it with the same primitive.
  fill_ring_band(ctx, lo, a0, a1, r, 1, col);
}

void draw_spoke(GContext *ctx, const Layout *lo, int32_t a,
                int16_t r0, int16_t r1, int16_t w, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, stroke_px(w));
  graphics_draw_line(ctx, point_on_circle(lo->center, r0, a),
                          point_on_circle(lo->center, r1, a));
}

// ---------------------------------------------------------------------------
// Polygons
// ---------------------------------------------------------------------------

// Move p away from c by d px. Returns p unchanged if they coincide.
static GPoint grow_from(GPoint p, GPoint c, int16_t d) {
  int32_t dx = p.x - c.x, dy = p.y - c.y;
  int16_t len = isqrt32(dx * dx + dy * dy);
  if (len == 0) return p;
  return GPoint(p.x + (int16_t)div_round(dx * d, len),
                p.y + (int16_t)div_round(dy * d, len));
}

void fill_poly(GContext *ctx, GPoint *pts, int n, GColor ink, bool halo) {
  if (n < 3 || n > 4) return;

  if (halo) {
    // The same shape grown away from its own centroid, filled in background and
    // then covered by the shape itself. See the header for why this is not a
    // wide background-coloured outline.
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

void draw_tri(GContext *ctx, GPoint p0, GPoint p1, GPoint p2, GColor ink, bool halo) {
  GPoint pts[3] = { p0, p1, p2 };
  fill_poly(ctx, pts, 3, ink, halo);
}

void ring_wedge(GContext *ctx, const Layout *lo, int32_t a,
                int32_t half_a, int32_t tip_a,
                int16_t r_base, int16_t r_apex, GColor ink, bool halo) {
  if (tip_a < 1) tip_a = 1;
  if (tip_a > half_a) tip_a = half_a;
  GPoint pts[4] = {
    point_on_circle(lo->center, r_base, a - half_a),
    point_on_circle(lo->center, r_base, a + half_a),
    point_on_circle(lo->center, r_apex, a + tip_a),
    point_on_circle(lo->center, r_apex, a - tip_a),
  };
  fill_poly(ctx, pts, 4, ink, halo);

  // Then cap the base with an annular sector in the same ink, drawn with the
  // band's own primitive.
  //
  // A polygon's base is a straight chord between two points *on* the circle, so
  // only its two corners are at r_base and everything between them falls short
  // by the sagitta; and where fill_radial feathers its outer edge, gpath's fill
  // has no antialiasing to feather with. Neither is a whole pixel on its own and
  // together they were enough: the wedge read as floating a pixel inside the band
  // it sits on, which is the one place on the ring two shapes have to share an
  // edge. Arithmetic cannot close that — the two edges come from two different
  // rasterisers — so the base is drawn by the same call the band's outer edge is,
  // at the same radius, the way draw_ring_arc() lines the rail up with the inner
  // edge of the bands sitting on it.
  int16_t cap_d = WEDGE_CAP_D;
  if (cap_d > r_base - r_apex) cap_d = r_base - r_apex;
  if (cap_d > 0) fill_ring_band(ctx, lo, a - half_a, a + half_a, r_base, cap_d, ink);
}

// ---------------------------------------------------------------------------
// Plates
// ---------------------------------------------------------------------------

void draw_disc(GContext *ctx, GPoint c, int16_t r) {
  if (r < 2) return;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_stroke_color(ctx, COL_INK);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, c, r);
}

int16_t chord_half(int16_t r, int16_t dy) {
  if (dy < 0) dy = -dy;
  if (dy >= r) return 0;
  return isqrt32((int32_t)r * r - (int32_t)dy * dy);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

Layout layout_compute(GRect bounds, GFont date_font, GFont slot_font) {
  Layout lo;
  lo.bounds = bounds;

  // The dial is the full screen width and flush to the top on a rectangle, so
  // its centre is half a *width* down rather than half a height. What is left
  // underneath is the notification band, and it is the screen's aspect ratio
  // that decides how much: 24 px on flint, 28 on emery. On the round display
  // there is nothing left over, which is why the band moves inside the dial.
#ifdef PBL_ROUND
  lo.radius = (bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h) / 2;
  lo.center = GPoint(bounds.origin.x + bounds.size.w / 2,
                     bounds.origin.y + bounds.size.h / 2);
  lo.band_inset = true;
#else
  lo.radius = bounds.size.w / 2;
  lo.center = GPoint(bounds.origin.x + bounds.size.w / 2,
                     bounds.origin.y + lo.radius);
  lo.band_inset = false;
#endif

  int16_t margin = lo.radius / 24;
  if (margin < EDGE_MARGIN_MIN) margin = EDGE_MARGIN_MIN;
  lo.margin = margin;

  // The radial ladder, rim inward. Every rung is a fraction of the radius, so
  // the proportions hold across a 144 px display and a 260 px one; the floors
  // are what keeps flint's end of it drawable.
  lo.r_out = lo.radius - 1;
  lo.ring_d = (int16_t)((lo.radius * 23 + 100) / 200);     // ~11.5% — 8/12/15
  if (lo.ring_d < 7) lo.ring_d = 7;
  lo.r_in = lo.r_out - lo.ring_d;

  // How far a task wedge's apex passes the ring's inner edge. The poke is what
  // separates a point in time from a span: a band stops at the rail, a wedge
  // goes through it.
  int16_t poke = (int16_t)((lo.radius * 11 + 100) / 200);  // 4/6/7
  if (poke < 3) poke = 3;
  lo.r_apex = lo.r_in - poke;

  // The clearance between the deepest a marker reaches and the tick lane. Five
  // percent rather than three, so a merged wedge — which is deeper as well as
  // wider and blunter — has somewhere to be deeper *into*. At three it had one
  // pixel on flint, and one pixel is not a difference a reader can see.
  int16_t gap = lo.radius * 5 / 100;
  if (gap < 3) gap = 3;
  lo.tick_out = lo.r_apex - gap;
  lo.tick_maj = lo.radius / 10;
  if (lo.tick_maj < 6) lo.tick_maj = 6;
  lo.tick_min = lo.tick_maj * 65 / 100;
  if (lo.tick_min < 4) lo.tick_min = 4;
  lo.r_text = lo.tick_out - lo.tick_maj - 2;

  // The hands. The minute hand runs to the rim; the hour hand ends flush with
  // the outer end of the ticks and never enters the ring.
  //
  // It reached the ring's mid-depth first, so that a running appointment had the
  // hand's tip physically inside its band. That is a true reading and it cost
  // more than it was worth: a broad hand lying in the marker lane competes with
  // the markers for the one lane they have, and on flint, where both are the
  // same ink, the tip and the band it was inside read as one shape. Stopping
  // short keeps the ring the markers' alone and the hand still points *at* the
  // band — which is all the reading ever needed, the ring running at the hand's
  // own rate.
  //
  // Where it stops is the tick lane's own outer end rather than a clearance off
  // the ring, which is what an hour hand on any analog dial does: the ticks are
  // the scale it reads against, so tip and graduation share an edge and the eye
  // has a line to read the hand to. It also puts the whole marker clearance
  // (`gap`) between the tip and the nearest thing a marker can reach, where
  // three pixels off r_in left the tip inside that clearance.
  //
  // Convention holds, the minute hand being the longer of the two, and they are
  // told apart by width, which is what flint has instead of hue.
  lo.hand_m_len = lo.r_out - 1;
  lo.hand_h_len = lo.tick_out;
  // Told apart by width alone, which is what flint has instead of hue, so the
  // ratio between them is what has to survive rather than either number. A
  // twentieth and a fortieth of the radius is seven pixels against three on
  // flint — the smallest pair that still reads as two different hands once both
  // carry a background halo.
  lo.hand_h_half = lo.radius / 20;
  if (lo.hand_h_half < 3) lo.hand_h_half = 3;
  lo.hand_m_half = lo.radius / 40;
  if (lo.hand_m_half < 1) lo.hand_m_half = 1;
  lo.hand_tail = lo.radius / 10;
  lo.hub_r = lo.radius / 26;
  if (lo.hub_r < 2) lo.hub_r = 2;

  // Representative strings, never the live ones: no row may change size as the
  // day, the countdown or the distance moves. "MON 22" is the widest a date line
  // gets and "+0:00" the widest a countdown gets — the sign is always drawn, so it
  // is measured; one digit of hours, not two, is what fits it into a disc this size. "000 KM" is the widest a distance
  // gets: three digits is what fmt_distance() switches to above ten units, and
  // KM is wider than MI, so the fraction case is never the binding one.
  GRect measure = GRect(0, 0, bounds.size.w, bounds.size.h);
  GSize ds = graphics_text_layout_get_content_size(
      "MON 22", date_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize cs = graphics_text_layout_get_content_size(
      "+0:00", slot_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize ns = graphics_text_layout_get_content_size(
      "000 KM", slot_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize ws = graphics_text_layout_get_content_size(
      "100%", slot_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);

  int16_t row_h = ROW_H(cs.h) + 4;

  // The countdown is an ordinary row of the disc now. It used to reserve a strip
  // under the digits for a progress bar — unconditionally, because the bar only
  // drew while an appointment was running and a disc that changed size when it did
  // would move every row in it at the one moment the reader is watching one. The
  // sign in front of the digits says the same thing on the line itself, so the
  // reservation is gone and the disc is a bar's height smaller on every platform.
  int16_t count_h = ROW_H(cs.h);

  // What each row needs in the one-row [glyph] number form slot_layout() draws.
  // The glyph is 85% of its row, and the gap after it is SLOT_GAP — half as much on
  // the countdown, whose number starts with a sign. 85% is slot_layout()'s own
  // number, repeated here because the disc has to be sized before there is
  // anything to lay out in it; the gap is shared through geometry.h.
  int16_t cg = cs.h * 85 / 100;
  int16_t bg = row_h * 85 / 100;
  int16_t count_w = cg + SLOT_GAP(cg, COUNT_GAP_DIV) + cs.w;
  int16_t nav_w = bg + SLOT_GAP(bg, SLOT_GAP_DIV) + ns.w;
  int16_t warn_w = bg + SLOT_GAP(bg, SLOT_GAP_DIV) + ws.w;

  // The rows of the disc, top to bottom, each with the gap that precedes it.
  //
  // Three on the round display, two on the rectangles. There the circle leaves a
  // strip of screen under the dial and the notification pair lives in it side by
  // side; here it does not, and a row of the disc is the most expensive place on
  // this face to put anything — so the pair is one row carrying one reading, and
  // the next turn and whatever is running out take strict priority over each
  // other rather than taking a lane each. Abreast they need 157 px of chord here
  // and there are 142; stacked they cost radius the hour hand wants back.
  typedef struct { int16_t gap, h, w; } Row;
  Row rows[3];
  int nrows = 0;
  rows[nrows++] = (Row){ 0, ROW_H(ds.h), ds.w };
  rows[nrows++] = (Row){ 0, count_h, count_w };
  if (lo.band_inset) {
    // Half a margin, not a whole one. The notification row is still part of the
    // same reading as the countdown above it — what is next, and what is wrong —
    // and a full margin read as a second block that had drifted down the disc.
    int16_t g = margin / 2;
    if (g < 2) g = 2;
    // Budgeted against the wider of the two readings it can hold — nav's, which
    // carries a unit as well as a number — so the plate does not resize at the
    // minute a maneuver gives way to a battery.
    rows[nrows++] = (Row){ g, row_h, nav_w > warn_w ? nav_w : warn_w };
  }

  int16_t total_h = 0;
  for (int i = 0; i < nrows; i++) total_h += rows[i].gap + rows[i].h;

  // How big the disc has to be. Each row is a rectangle inscribed in the circle,
  // so the binding corner is its far edge — the one further from the centre —
  // and the disc's inner radius is the largest of those corners' distances.
  // Solved rather than guessed, because the answer differs on every platform and
  // moves whenever a font size does.
  int16_t top = -(total_h / 2);
  int16_t need = 0;
  {
    int16_t y = top;
    for (int i = 0; i < nrows; i++) {
      y += rows[i].gap;
      int16_t a = y < 0 ? -y : y;
      int16_t b = (y + rows[i].h) < 0 ? -(y + rows[i].h) : (y + rows[i].h);
      int16_t far = a > b ? a : b;
      int16_t d = isqrt32((int32_t)far * far +
                          (int32_t)(rows[i].w / 2) * (rows[i].w / 2));
      if (d > need) need = d;
      y += rows[i].h;
    }
  }

  // One pad plus a pixel. It was two pads — one for the gap between the outermost
  // ink and the rim, one because `need` is the distance to a row's *corner* — and
  // that second allowance was paying twice for the same slack the rows now
  // reclaim through ROW_H. A row's corner is blank in every string this face
  // draws, so a pad and a pixel is the whole of what it needs.
  lo.plate_r = need + PLATE_PAD + 1;
  if (lo.plate_r > lo.r_text) lo.plate_r = lo.r_text;
  int16_t inner_r = lo.plate_r - PLATE_PAD;

  // Place the rows, each as wide as the chord at its own far edge, so a centred
  // string sits in the middle of the room it actually has.
  GRect boxes[3];
  {
    int16_t y = top;
    for (int i = 0; i < nrows; i++) {
      y += rows[i].gap;
      int16_t a = y < 0 ? -y : y;
      int16_t b = (y + rows[i].h) < 0 ? -(y + rows[i].h) : (y + rows[i].h);
      int16_t far = a > b ? a : b;
      int16_t hw = chord_half(inner_r, far);
      boxes[i] = GRect(lo.center.x - hw, lo.center.y + y, 2 * hw, rows[i].h);
      y += rows[i].h;
    }
  }
  lo.date_box = boxes[0];
  lo.count_box = boxes[1];

  if (lo.band_inset) {
    // One row, and both painters are pointed at it. They never contend for it:
    // slots_draw_warn() stands down while a maneuver is drawing, and the one
    // reading that outranks a maneuver — the companion being gone — has already
    // nulled it upstream. See slots_draw_warn().
    lo.nav_box = lo.warn_box = boxes[2];
  } else {
    // Under the dial, on plain background — nothing crosses it, so it needs no
    // plate of its own.
    //
    // Centred in the strip the circle leaves, and then pushed back down by the
    // lift. ROW_LIFT exists to close the gap between *stacked* rows; this row has
    // nothing above it to close a gap against, so applying it here only moved the
    // band toward the dial — measured on flint, three pixels of air above the
    // glyphs and thirteen below them.
    int16_t top = bounds.origin.y + 2 * lo.radius;
    int16_t bot = bounds.origin.y + bounds.size.h;
    int16_t bh = ROW_H(cs.h) + 2;
    if (bh > bot - top) bh = bot - top;
    if (bh < 0) bh = 0;
    int16_t by = top + (bot - top - ROW_H(cs.h)) / 2 + ROW_LIFT(cs.h);
    if (by + bh > bot) by = bot - bh;
    if (by < top) by = top;

    // Three fifths to nav, because it is the pair carrying a unit as well as a
    // number and is the wider of the two in every state they can *both* be in.
    // The one state that is wider than its share is the companion-down alert,
    // which spells itself out here — and that is the state nav cannot be in at
    // all, so it takes the whole band rather than a share of it. See
    // slots_draw_warn().
    int16_t bx = bounds.origin.x + margin;
    int16_t bw = bounds.size.w - 2 * margin;
    int16_t nw = bw * 3 / 5;
    lo.nav_box  = GRect(bx, by, nw, bh);
    lo.warn_box = GRect(bx + nw, by, bw - nw, bh);
  }

  return lo;
}
