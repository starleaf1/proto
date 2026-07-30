#include "slots.h"
#include "events.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"
#include <stdio.h>

// ---------------------------------------------------------------------------
// Both slots share one grammar: [glyph] number suffix, centred as a pair.
//
// The bottom slot's task glyph is deliberately a miniature of the dial's point
// marker, so the face teaches that half of its vocabulary once instead of twice.
// The appointment glyph used to work the same way — a short bar, a band unrolled
// — and it did not survive contact: a rounded bar at slot size reads as a pill or
// a battery, not as anything to do with a calendar. It is Material Design's
// calendar mark now. The echo of the dial was worth less than being recognised.
//
// What did carry over is the progress reading: the running-appointment form fills
// the calendar's open half from the left, so the same ink still says how far in
// you are.
//
// Every glyph is drawn from primitives or a normalised point table rather than a
// bitmap. Three platforms means three sizes, and vectors stay crisp at all of
// them without spending anything from the resource budget.
// ---------------------------------------------------------------------------

#define PHONE_BATT_LOW   30   // percent
#define WATCH_BATT_LOW_H 24   // hours

// Maneuver glyphs live in a +/-36 grid, drawn as a thick polyline with a filled
// head on its end. One table per turn rather than one arrow rotated by an angle:
// the shape is what makes a sharp right read differently from a slight one at
// twenty pixels, and that does not survive being derived from a rotation.
//
// `dir` is the head's direction in tenths, so the diagonals carry (7,7) rather
// than (1,1) and every head comes out the same length.
typedef struct {
  int8_t n;
  int8_t pts[4][2];
  int8_t dir[2];
} ManeuverPath;

// The tables fill the grid, and the head is deliberately large relative to the
// shaft. A 21px glyph on flint leaves only two pixels of stroke, and a head that
// is merely a little wider than its own shaft disappears at that size — the first
// attempt drew a recognisable "turn right" that read as the letter Γ.
#define GRID 36
#define HEAD_LEN  32
#define HEAD_HALF 20

static const ManeuverPath MANEUVERS[] = {
  /* NAV_STRAIGHT     */ { 2, {{0,30},{0,-4}},                        {  0,-10} },
  /* NAV_LEFT         */ { 3, {{28,30},{28,-10},{6,-10}},             {-10,  0} },
  /* NAV_RIGHT        */ { 3, {{-28,30},{-28,-10},{-6,-10}},          { 10,  0} },
  /* NAV_SLIGHT_LEFT  */ { 3, {{16,32},{16,4},{-2,-14}},              { -7, -7} },
  /* NAV_SLIGHT_RIGHT */ { 3, {{-16,32},{-16,4},{2,-14}},             {  7, -7} },
  /* NAV_SHARP_LEFT   */ { 3, {{16,30},{16,-16},{-2,2}},              { -7,  7} },
  /* NAV_SHARP_RIGHT  */ { 3, {{-16,30},{-16,-16},{2,2}},             {  7,  7} },
  /* NAV_UTURN        */ { 4, {{14,32},{14,-14},{-14,-14},{-14,0}},   {  0, 10} },
};

static int16_t sw_for(int16_t side) {
  int16_t w = side / 9;
  return w < 2 ? 2 : w;
}

static GPoint grid_pt(GPoint c, int16_t side, int gx, int gy) {
  int16_t s = side / 2;
  return GPoint(c.x + (int16_t)((int32_t)gx * s / GRID),
                c.y + (int16_t)((int32_t)gy * s / GRID));
}

// ---------------------------------------------------------------------------
// Glyphs
// ---------------------------------------------------------------------------

static void glyph_maneuver(GContext *ctx, GRect box, int man, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;
  int16_t sw = sw_for(side);
  int16_t s = side / 2;

  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, sw);

  if (man == NAV_ARRIVE) {
    // Destination: a ring around a solid centre. Nothing to point at any more.
    graphics_draw_circle(ctx, c, s * 3 / 4);
    graphics_fill_circle(ctx, c, s / 3);
    return;
  }
  if (man == NAV_ROUNDABOUT) {
    // The island, the approach, and one exit leaving it to the upper right.
    GPoint island = GPoint(c.x, c.y - s / 5);
    graphics_draw_circle(ctx, island, s / 2);
    graphics_draw_line(ctx, GPoint(c.x, c.y + s), GPoint(c.x, island.y + s / 2));
    GPoint exit_from = GPoint(island.x + s / 2, island.y);
    GPoint exit_to = GPoint(island.x + s, island.y - s / 2);
    graphics_draw_line(ctx, exit_from, exit_to);
    GPoint tip = GPoint(exit_to.x + s / 4, exit_to.y - s / 4);
    draw_tri(ctx, tip,
             GPoint(exit_to.x - s / 5, exit_to.y - s / 3),
             GPoint(exit_to.x + s / 3, exit_to.y + s / 5),
             ink, true, 1, false);
    return;
  }

  int idx = man - NAV_STRAIGHT;
  if (idx < 0 || idx >= (int)(sizeof MANEUVERS / sizeof MANEUVERS[0])) return;
  const ManeuverPath *m = &MANEUVERS[idx];

  for (int i = 0; i + 1 < m->n; i++) {
    graphics_draw_line(ctx, grid_pt(c, side, m->pts[i][0], m->pts[i][1]),
                            grid_pt(c, side, m->pts[i + 1][0], m->pts[i + 1][1]));
  }

  // Head: tip HEAD_LEN beyond the polyline's end along `dir`, base corners
  // HEAD_HALF either side of that end, perpendicular to it.
  int ex = m->pts[m->n - 1][0], ey = m->pts[m->n - 1][1];
  int dx = m->dir[0], dy = m->dir[1];
  GPoint tip = grid_pt(c, side, ex + dx * HEAD_LEN / 10, ey + dy * HEAD_LEN / 10);
  GPoint b1  = grid_pt(c, side, ex - dy * HEAD_HALF / 10, ey + dx * HEAD_HALF / 10);
  GPoint b2  = grid_pt(c, side, ex + dy * HEAD_HALF / 10, ey - dx * HEAD_HALF / 10);
  draw_tri(ctx, tip, b1, b2, ink, true, 1, false);
}

// A phone silhouette. Also the "whose battery" cue: this outline against the
// battery pictogram below is what lets both alerts drop the word "phone".
static void glyph_phone(GContext *ctx, GRect box, bool slashed, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;
  int16_t w = side * 58 / 100;
  int16_t h = side * 92 / 100;
  int16_t sw = side / 12;
  if (sw < 2) sw = 2;

  GRect body = GRect(c.x - w / 2, c.y - h / 2, w, h);
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, sw);
  graphics_draw_round_rect(ctx, body, side / 8);

  if (slashed) {
    GPoint from = GPoint(body.origin.x - sw, body.origin.y + body.size.h + sw);
    GPoint to   = GPoint(body.origin.x + body.size.w + sw, body.origin.y - sw);
    // A background-coloured underlay so the slash separates from the outline it
    // crosses instead of merging into a solid blob.
    graphics_context_set_stroke_color(ctx, COL_BG);
    graphics_context_set_stroke_width(ctx, sw + 2);
    graphics_draw_line(ctx, from, to);
    graphics_context_set_stroke_color(ctx, ink);
    graphics_context_set_stroke_width(ctx, sw);
    graphics_draw_line(ctx, from, to);
  }
}

// The classic cell pictogram, outline only: the slot's text carries the value,
// and a proportional fill would be claiming a percentage when what is shown is
// hours.
static void glyph_battery(GContext *ctx, GRect box, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;
  int16_t w = side * 78 / 100;
  int16_t h = side * 50 / 100;
  int16_t nub = side / 10;
  if (nub < 2) nub = 2;

  GRect body = GRect(c.x - w / 2 - nub / 2, c.y - h / 2, w, h);
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, side / 14 < 1 ? 1 : side / 14);
  graphics_draw_rect(ctx, body);
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, GRect(body.origin.x + body.size.w, c.y - h / 4, nub, h / 2),
                     0, GCornerNone);
}

// An appointment: Material Design's calendar mark, at slot size. Two tabs standing
// on a body rectangle, a filled header band beneath them, an open lower half.
//
// Reduced to those three parts on purpose. The tabs and the band are the whole of
// what makes the MD icon read as a calendar rather than as a picture frame, and the
// date grid inside the real thing is illegible at twenty pixels — flint's glyph is
// about that.
//
// `progress` fills the open half from the left to `pct`. The in-progress form still
// has to say how far into the appointment you are, and the area under the header is
// exactly the rectangle that was already doing it.
static void glyph_calendar(GContext *ctx, GRect box, bool progress, int pct, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;

  int16_t sw = side / 14;
  if (sw < 1) sw = 1;
  int16_t tab_h = side / 7;
  if (tab_h < 2) tab_h = 2;
  int16_t tab_w = sw < 2 ? 2 : sw;

  int16_t w = side * 90 / 100;
  int16_t h = side - tab_h;
  GRect body = GRect(c.x - w / 2, c.y - side / 2 + tab_h, w, h);

  // Deep enough that the band still reads as a band once the outline has eaten a
  // stroke off its top edge.
  int16_t head_h = h * 32 / 100;
  if (head_h < sw + 2) head_h = sw + 2;

  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, sw);

  // Tabs first, running down into the header so the two merge rather than meeting
  // at a seam that a single pixel of rounding could break.
  for (int i = 0; i < 2; i++) {
    int16_t tx = body.origin.x + (i ? w * 3 / 4 : w / 4) - tab_w / 2;
    graphics_fill_rect(ctx, GRect(tx, body.origin.y - tab_h, tab_w, tab_h + sw),
                       0, GCornerNone);
  }

  graphics_fill_rect(ctx, GRect(body.origin.x, body.origin.y, w, head_h),
                     0, GCornerNone);
  graphics_draw_rect(ctx, body);

  if (!progress) return;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  GRect open = GRect(body.origin.x + sw, body.origin.y + head_h,
                     w - 2 * sw, h - head_h - sw);
  int16_t fill_w = open.size.w * pct / 100;
  if (fill_w > 0 && open.size.h > 0) {
    graphics_fill_rect(ctx, GRect(open.origin.x, open.origin.y, fill_w, open.size.h),
                       0, GCornerNone);
  }
}

// A task: the dial's point marker, shrunk.
static void glyph_task(GContext *ctx, GRect box, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;
  int16_t hw = side * 35 / 100;
  int16_t hh = side * 40 / 100;
  draw_tri(ctx, GPoint(c.x, c.y - hh),
           GPoint(c.x - hw, c.y + hh), GPoint(c.x + hw, c.y + hh),
           ink, true, 1, false);
}

// ---------------------------------------------------------------------------
// Slot layout
// ---------------------------------------------------------------------------

// Places a glyph and its text as one centred group. The glyph is square and
// three-quarters of the slot's height, which lines its optical centre up with
// the text's without having to know the font's internal padding.
static void slot_layout(GRect box, GFont font, const char *text,
                        GRect *glyph_box, GRect *text_box) {
  // 85% of the slot's height, not all of it: the box carries the font's ascender
  // and descender, and a glyph matched to the cap height reads as the same size
  // as the digits beside it.
  int16_t side = box.size.h * 85 / 100;
  GSize ts = GSize(0, 0);
  if (text && text[0]) {
    ts = graphics_text_layout_get_content_size(text, font, box,
                                               GTextOverflowModeFill,
                                               GTextAlignmentLeft);
  }
  int16_t gap = ts.w > 0 ? side / 3 : 0;
  int16_t total = side + gap + ts.w;
  int16_t x = box.origin.x + (box.size.w - total) / 2;

  *glyph_box = GRect(x, box.origin.y + (box.size.h - side) / 2, side, side);
  *text_box = GRect(x + side + gap, box.origin.y, ts.w + 4, box.size.h);
}

// Clear the pair's footprint before drawing into it, for the same reason the
// numeral and date do: both slots sit at twelve and six o'clock, which is exactly
// where a marker at the top or bottom of the dial points inward.
// Kept to the glyph's vertical band rather than the slot box's full height: the
// box carries padding, and clearing all of it takes a bite out of the notches at
// six o'clock that sit just past the slot's lower edge.
static void slot_knock_out(GContext *ctx, GRect glyph_box, GRect text_box) {
  int16_t x0 = glyph_box.origin.x - 2;
  int16_t x1 = text_box.size.w > 4 ? text_box.origin.x + text_box.size.w
                                   : glyph_box.origin.x + glyph_box.size.w + 2;
  knock_out(ctx, GRect(x0, glyph_box.origin.y, x1 - x0, glyph_box.size.h));
}

static void fmt_distance(char *buf, size_t n, int tenths, int unit) {
  static const char *const ABBR[] = { "M", "KM", "FT", "MI" };
  const char *u = ABBR[(unit >= NAV_UNIT_M && unit <= NAV_UNIT_MAX) ? unit : NAV_UNIT_M];
  if (tenths < 0) tenths = 0;
  // Under ten units the tenth is the informative digit ("0.3 MI"); above it the
  // fraction is noise on a display read at a glance.
  if (tenths < 100) snprintf(buf, n, "%d.%d %s", tenths / 10, tenths % 10, u);
  else snprintf(buf, n, "%d %s", tenths / 10, u);
}

void slots_draw_top(GContext *ctx, const Layout *lo, GFont font) {
  char text[16] = "";
  GColor ink = COL_INK;
  enum { T_NONE, T_DOWN, T_NAV, T_PHONE, T_WATCH } which = T_NONE;

  // Strict priority: the first thing that is true is the only thing shown.
  if (!wire_companion_alive()) {
    which = T_DOWN;
    ink = COL_ALERT;
  } else if (wire_nav_active()) {
    which = T_NAV;
    fmt_distance(text, sizeof text, wire_nav_distance(), wire_nav_unit());
  } else {
    int pb = wire_phone_battery();
    int wh = wbatt_hours();
    if (pb >= 0 && pb <= PHONE_BATT_LOW) {
      which = T_PHONE;
      ink = COL_WARN;
      snprintf(text, sizeof text, "%d%%", pb);
    } else if (wh >= 0 && wh <= WATCH_BATT_LOW_H) {
      which = T_WATCH;
      ink = COL_WARN;
      snprintf(text, sizeof text, "%dH", wh);
    }
  }
  if (which == T_NONE) return;

  GRect gbox, tbox;
  slot_layout(lo->top_box, font, text, &gbox, &tbox);
  slot_knock_out(ctx, gbox, tbox);

  switch (which) {
    case T_DOWN:  glyph_phone(ctx, gbox, true, ink); break;
    case T_NAV:   glyph_maneuver(ctx, gbox, wire_nav_maneuver(), ink); break;
    case T_PHONE: glyph_phone(ctx, gbox, false, ink); break;
    case T_WATCH: glyph_battery(ctx, gbox, ink); break;
    default: break;
  }
  if (text[0]) {
    graphics_context_set_text_color(ctx, ink);
    graphics_draw_text(ctx, text, font, tbox, GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
  }
}

void slots_draw_bottom(GContext *ctx, const Layout *lo, GFont font, time_t now) {
  // Calendar markers and this countdown stay on show even when the companion is
  // gone, unlike the notification counts this face used to carry. An entry is
  // timestamped and ages out on its own, so it does not go stale the way a count
  // does — and the top slot is already saying the companion is unreachable, so
  // nothing here is claiming to be complete.
  SlotPick p = events_pick_slot(now);
  if (!p.valid) return;

  int32_t mins = p.seconds / 60;
  if (mins > 99 * 60 + 59) mins = 99 * 60 + 59;
  char text[12];
  snprintf(text, sizeof text, "%02d:%02d", (int)(mins / 60), (int)(mins % 60));

  GColor ink = COL_INK;
  if (p.counting_up) ink = COL_ACCENT;
  else if (p.kind == EV_TASK) ink = COL_TASK_SOON;

  GRect gbox, tbox;
  slot_layout(lo->bottom_box, font, text, &gbox, &tbox);
  slot_knock_out(ctx, gbox, tbox);

  if (p.counting_up) {
    glyph_calendar(ctx, gbox, true, p.pct, ink);
  } else if (p.kind == EV_TASK) {
    glyph_task(ctx, gbox, ink);
  } else {
    glyph_calendar(ctx, gbox, false, 0, ink);
  }

  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, text, font, tbox, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
}
