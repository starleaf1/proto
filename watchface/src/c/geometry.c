#include "geometry.h"
#include "theme.h"

#define EDGE_MARGIN_MIN 3

#ifdef PBL_ROUND
// Point at radius r, angle a. int32 math before the int16 cast avoids overflow —
// sin_lookup * r reaches ~7M at gabbro's 125px radius.
static GPoint point_on_circle(GPoint c, int32_t r, int32_t a) {
  return GPoint(c.x + (int16_t)(sin_lookup(a) * r / TRIG_MAX_RATIO),
                c.y - (int16_t)(cos_lookup(a) * r / TRIG_MAX_RATIO));
}
#else
// Where a ray from center c at angle a exits rectangle r (already edge-inset).
static GPoint ray_rect_boundary(GRect r, GPoint c, int32_t a) {
  int32_t dx = sin_lookup(a);    // +right
  int32_t dy = -cos_lookup(a);   // +down; a = 0 -> straight up
  int16_t left  = r.origin.x, right = r.origin.x + r.size.w - 1;
  int16_t top   = r.origin.y, bot   = r.origin.y + r.size.h - 1;
  if (dx != 0) {                 // try a vertical edge first
    int16_t ex = (dx > 0) ? right : left;
    int32_t ey = c.y + (int32_t)(ex - c.x) * dy / dx;
    if (ey >= top && ey <= bot) return GPoint(ex, (int16_t)ey);
  }
  int16_t ey2 = (dy > 0) ? bot : top;   // else it exits a horizontal edge
  int32_t ex2 = c.x + (int32_t)(ey2 - c.y) * dx / dy;
  return GPoint((int16_t)ex2, ey2);
}
#endif

// The dial hugs the screen: a circle on gabbro, the rectangular perimeter on flint
// and emery.
//
// That means equal spans of time occupy unequal arc lengths on the two rectangular
// platforms — a corner is 1.5x further from the centre than an edge midpoint, so a
// band crossing one covers more pixels than an identical band at three o'clock. The
// distortion is accepted deliberately; the angle, which is what encodes the time, is
// exact everywhere.
//
// What must *not* vary is how thick a marker looks. Every depth on the ring is a count
// of pixels measured perpendicular to the boundary — see depth_along_ray() below, which
// is what converts one into a distance to travel along the ray. Never a fraction of the
// distance to the boundary, and on a rectangle never a fixed distance along the ray
// either: that was the first version, and it left a band measuring a full tick zone at
// three o'clock and two thirds of one at the corner.
GPoint dial_boundary(GRect dial, GPoint c, int32_t a) {
#ifdef PBL_ROUND
  int32_t rad = (dial.size.w < dial.size.h ? dial.size.w : dial.size.h) / 2;
  return point_on_circle(c, rad, a);
#else
  return ray_rect_boundary(dial, c, a);
#endif
}

GPoint step_in(GPoint p, int32_t a, int32_t d) {
  return GPoint(p.x - (int16_t)(sin_lookup(a) * d / TRIG_MAX_RATIO),
                p.y + (int16_t)(cos_lookup(a) * d / TRIG_MAX_RATIO));
}

int32_t depth_along_ray(GRect dial, GPoint boundary, int32_t a, int32_t d) {
#ifdef PBL_ROUND
  (void)dial;
  (void)boundary;
  (void)a;
  return d;
#else
  // Which edge the ray left through decides the normal: a vertical one is crossed at
  // cos(theta) = |sin a|, a horizontal one at |cos a|. Corners satisfy both tests, and
  // are resolved the same way ray_rect_boundary() resolves them — vertical first — so
  // the two agree about where the boundary is.
  int32_t cos_t = (boundary.x == dial.origin.x ||
                   boundary.x == dial.origin.x + dial.size.w - 1)
                  ? sin_lookup(a) : cos_lookup(a);
  if (cos_t < 0) cos_t = -cos_t;

  // The real worst case is the corner, a touch over 1.5x on both rectangular displays.
  // The floor is a guard against a degenerate angle, not a working limit.
  if (cos_t < TRIG_MAX_RATIO / 2) cos_t = TRIG_MAX_RATIO / 2;
  return d * TRIG_MAX_RATIO / cos_t;
#endif
}

GPoint step_side(GPoint p, int32_t a, int32_t d) {
  return GPoint(p.x + (int16_t)(cos_lookup(a) * d / TRIG_MAX_RATIO),
                p.y + (int16_t)(sin_lookup(a) * d / TRIG_MAX_RATIO));
}

int deg_of_time(time_t t) {
  struct tm lt = *localtime(&t);
  int mins = (lt.tm_hour % 12) * 60 + lt.tm_min;   // 0..719
  return mins / 2;                                 // 1 degree = 2 minutes
}

GRect text_plate(GRect box, GFont font, const char *text) {
  if (!text || !text[0]) return GRect(box.origin.x, box.origin.y, 0, 0);
  GSize ts = graphics_text_layout_get_content_size(
      text, font, box, GTextOverflowModeFill, GTextAlignmentCenter);
  int16_t pad = 2;
  return GRect(box.origin.x + (box.size.w - ts.w) / 2 - pad, box.origin.y,
               ts.w + 2 * pad, box.size.h);
}

void knock_out(GContext *ctx, GRect r) {
  if (r.size.w <= 0 || r.size.h <= 0) return;
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
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

// How far outside the shape a halo reaches, in pixels. Small on purpose: it exists
// to make an edge findable, not to clear a moat.
#define HALO_PX 2

// Move p away from c by d px. Returns p unchanged if they coincide.
static GPoint grow_from(GPoint p, GPoint c, int16_t d) {
  int32_t dx = p.x - c.x, dy = p.y - c.y;
  int16_t len = isqrt32(dx * dx + dy * dy);
  if (len == 0) return p;
  return GPoint(p.x + (int16_t)(dx * d / len), p.y + (int16_t)(dy * d / len));
}

void draw_tri(GContext *ctx, GPoint p0, GPoint p1, GPoint p2,
              GColor ink, bool filled, int16_t stroke_w, bool halo) {
  GPoint pts[3] = { p0, p1, p2 };
  GPathInfo info = { .num_points = 3, .points = pts };
  GPath *path = gpath_create(&info);
  if (!path) return;

  if (halo && filled) {
    // The same triangle grown HALO_PX away from its own centroid, filled in the
    // background colour and then covered by the shape — which leaves a ring of about
    // that width around it, and never more.
    //
    // The obvious version of this is a background-coloured *outline* stroked wider
    // than the shape, and it is a trap. A stroked path miters its corners, and the
    // miter at a sharp vertex runs far past the vertex itself — at the hour index's
    // 42-degree tip, a 4px stroke overshoots by more than five pixels. That is enough
    // to reach past the tip's clearance and cut a background-coloured slot clean
    // through an appointment band, which is exactly what it did. Growing the vertices
    // instead bounds the halo by construction, at any angle and any sharpness.
    GPoint c = GPoint((p0.x + p1.x + p2.x) / 3, (p0.y + p1.y + p2.y) / 3);
    GPoint hp[3] = { grow_from(p0, c, HALO_PX), grow_from(p1, c, HALO_PX),
                     grow_from(p2, c, HALO_PX) };
    GPathInfo hinfo = { .num_points = 3, .points = hp };
    GPath *hpath = gpath_create(&hinfo);
    if (hpath) {
      graphics_context_set_fill_color(ctx, COL_BG);
      gpath_draw_filled(ctx, hpath);
      gpath_destroy(hpath);
    }
  } else if (halo) {
    // A hollow shape has to keep whatever is under it showing through, so its halo
    // stays an outline. The miter overshoot above is harmless here: the only hollow
    // haloed shape is flint's upcoming marker, whose sharp vertex points inward into
    // the free area rather than out at the ring.
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

void draw_radial_triangle(GContext *ctx, GRect dial, GPoint c, int32_t a,
                          int16_t depth, int16_t half_width,
                          GColor ink, bool filled, bool halo) {
  GPoint base = dial_boundary(dial, c, a);
  // `depth` is perpendicular to the boundary, like every other depth on the ring, so a
  // marker keeps the same reach past the notch inner-ends at a corner as at an edge
  // midpoint. half_width is a width along the perimeter and is left alone.
  GPoint apex = step_in(base, a, depth_along_ray(dial, base, a, depth));
  int16_t stroke_w = half_width / 3;
  if (stroke_w < 1) stroke_w = 1;
  draw_tri(ctx, apex,
           step_side(base, a,  half_width),
           step_side(base, a, -half_width),
           ink, filled, stroke_w, halo);
}


// Clamp a full-width row to whatever the dial leaves it at that height.
//
// On a round display that is the circle's chord: a row placed low enough has the
// circle pinching in on both sides of it, and text laid out to the screen's width
// runs straight out through the notches. On a rectangular one the dial hugs the
// perimeter, so it is a plain inset past the notch zone — which leaves noticeably
// more room, since the ring is not eating into the middle of the screen.
//
// On round, the reference height is the row's centre plus a quarter of its height
// rather than its far edge: a single line of text does not reach into the corners of
// its own box, and the far edge is pessimistic enough to strangle both slots.
static GRect fit_row(GRect box, GRect bounds, GPoint c, int16_t dial_r,
                     int16_t inner_pad) {
#ifdef PBL_ROUND
  (void)bounds;
  (void)inner_pad;
  int16_t mid = box.origin.y + box.size.h / 2;
  int16_t dy = mid - c.y;
  if (dy < 0) dy = -dy;
  dy += box.size.h / 4;
  int16_t half = (dy < dial_r)
      ? isqrt32((int32_t)dial_r * dial_r - (int32_t)dy * dy) : 0;
  return GRect(c.x - half, box.origin.y, 2 * half, box.size.h);
#else
  (void)c;
  (void)dial_r;
  return GRect(bounds.origin.x + inner_pad, box.origin.y,
               bounds.size.w - 2 * inner_pad, box.size.h);
#endif
}

Layout layout_compute(GRect bounds, GFont num_font, GFont date_font,
                      GFont slot_font, const char *num_text) {
  Layout lo;
  lo.bounds = bounds;
  lo.center = GPoint(bounds.origin.x + bounds.size.w / 2,
                     bounds.origin.y + bounds.size.h / 2);
  lo.radius = (bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h) / 2;

  int16_t margin = lo.radius / 24;
  if (margin < EDGE_MARGIN_MIN) margin = EDGE_MARGIN_MIN;
  lo.dial = grect_inset(bounds, GEdgeInsets(margin));
  lo.tick_len = lo.radius / 9;

  GRect measure = GRect(0, 0, bounds.size.w, bounds.size.h);
  GSize ns = graphics_text_layout_get_content_size(
      num_text, num_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  // Representative strings, not the live ones: no row may shift as the day or the
  // countdown changes.
  GSize ds = graphics_text_layout_get_content_size(
      "MON 22", date_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize ss = graphics_text_layout_get_content_size(
      "00:00", slot_font, measure, GTextOverflowModeFill, GTextAlignmentCenter);

  int16_t dial_r = (lo.dial.size.w < lo.dial.size.h
                    ? lo.dial.size.w : lo.dial.size.h) / 2;
  int16_t inner_pad = margin + lo.tick_len;

  // Four rows share the vertical span the dial leaves free. On a rectangle that is
  // the screen minus the notch zone top and bottom; on a circle it is the band over
  // which the chord is still wide enough to hold text at all.
  int16_t span_top, span_bot;
#ifdef PBL_ROUND
  int16_t min_half = dial_r * 45 / 100;
  int16_t dy_max = isqrt32((int32_t)dial_r * dial_r - (int32_t)min_half * min_half);
  span_top = lo.center.y - dy_max;
  span_bot = lo.center.y + dy_max;
#else
  span_top = bounds.origin.y + inner_pad;
  span_bot = bounds.origin.y + bounds.size.h - inner_pad;
#endif

  // Budgeted rather than pinned to fractions of the radius, so a font size that does
  // not fit shows up as rows abutting instead of text crossing the ring.
  //
  // The four rows do not share the slack equally. The date and the countdown belong
  // to the numeral — they qualify it — so they are pulled up under it and whatever
  // is left over collects below the countdown instead of being spread between the
  // rows. The top slot keeps the full gap: it is the one row that is unrelated to
  // the other three, and it is also the alarm, which wants the separation.
  //
  // A tuck comes off on top of that, because a content box is taller than the ink in
  // it — Orbitron's numeral in particular sits well inside its own box — so the rows
  // read as further apart than the gaps say they are. It is a tenth of the row above,
  // which scales with the font rather than with the display, and it is deliberately
  // less than the padding it is claiming back: every row knocks its own footprint out
  // before drawing, so a tuck that overruns does not crowd the numeral, it amputates
  // it.
  int16_t span = span_bot - span_top;
  int16_t need = ss.h + ns.h + ds.h + ss.h;
  int16_t gap = (span - need) / 3;
  if (gap < 0) gap = 0;
  int16_t tight = gap / 3;

  int16_t y = span_top;
  lo.top_box = GRect(bounds.origin.x, y, bounds.size.w, ss.h + 4);
  y += ss.h + gap;
  lo.num_box = GRect(bounds.origin.x, y, bounds.size.w, ns.h + 6);
  y += ns.h + tight - ns.h / 10;
  lo.date_box = GRect(bounds.origin.x, y, bounds.size.w, ds.h + 4);
  y += ds.h + tight - ds.h / 10;
  lo.bottom_box = GRect(bounds.origin.x, y, bounds.size.w, ss.h + 4);

  lo.top_box    = fit_row(lo.top_box,    bounds, lo.center, dial_r, inner_pad);
  lo.num_box    = fit_row(lo.num_box,    bounds, lo.center, dial_r, inner_pad);
  lo.date_box   = fit_row(lo.date_box,   bounds, lo.center, dial_r, inner_pad);
  lo.bottom_box = fit_row(lo.bottom_box, bounds, lo.center, dial_r, inner_pad);

  return lo;
}
