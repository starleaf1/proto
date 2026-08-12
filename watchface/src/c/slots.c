#include "slots.h"
#include "events.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"
#include <stdio.h>

// ---------------------------------------------------------------------------
// The three conditional rows under the clock share one grammar:
// [glyph] number suffix, centred as a pair.
//
// The countdown's task glyph is deliberately a miniature of the strip's point
// marker, so the face teaches that half of its vocabulary once instead of twice.
// The appointment glyph used to work the same way — a short bar, a band unrolled
// — and it did not survive contact: a rounded bar at slot size reads as a pill or
// a battery, not as anything to do with a calendar. It is Material Design's
// calendar mark now. The echo of the strip was worth less than being recognised.
//
// The progress reading that used to live *inside* that glyph is now a bar under
// the digits instead. It is what tells a count-up from a count-down, and it has
// to work on a display with one ink, where the calendar mark's own fill could
// only be read by comparing it against a calendar mark that was not there.
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

// Every glyph stroke goes through stroke_px(). These shapes are mostly axis-aligned —
// the maneuver shafts, the phone's outline, the battery's rect — and an even width puts
// each of those straight edges half a pixel off the grid on top of being quietly
// narrowed by the renderer. See geometry.h.
static int16_t sw_for(int16_t side) {
  return stroke_px(side / 9);
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
  int16_t sw = stroke_px(side / 12);

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
  graphics_context_set_stroke_width(ctx, stroke_px(side / 14));
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
// One form, not two. The running-appointment version used to fill this glyph's open
// half to say how far through you were, which asked the reader to compare it against
// an unfilled calendar mark that was nowhere on the screen. The bar under the digits
// says it instead, and says it against its own empty track.
static void glyph_calendar(GContext *ctx, GRect box, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;

  int16_t sw = stroke_px(side / 14);
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
}

// A task: the strip's point marker, shrunk. It points the way the marker points —
// inward, off the ruler — which on the strip is to the right rather than toward
// the centre of a dial.
static void glyph_task(GContext *ctx, GRect box, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;
  int16_t hw = side * 40 / 100;
  int16_t hh = side * 35 / 100;
  draw_tri(ctx, GPoint(c.x + hw, c.y),
           GPoint(c.x - hw, c.y - hh), GPoint(c.x - hw, c.y + hh),
           ink, true, 1, false);
}

// The count-up cue, and the only thing that distinguishes the two directions.
//
// A hairline track with a solid fill over it, rather than a fill alone: the
// unfilled part has to be visible for the filled part to mean anything, and on
// flint they are the same black. Weight is the channel, not colour — the same
// device the hollow-against-solid marker uses on that display.
//
// Counting *down* draws no bar at all. That asymmetry is the point: a bar is a
// thing that fills up, so its presence already says the number is climbing, and a
// second empty bar under a countdown would only invite reading it as a countdown
// bar running the other way.
static void draw_progress(GContext *ctx, GRect box, int pct, GColor ink) {
  if (box.size.w <= 0 || box.size.h <= 0) return;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, GRect(box.origin.x, box.origin.y + box.size.h / 2,
                                box.size.w, 1), 0, GCornerNone);
  int16_t fw = (int16_t)((int32_t)box.size.w * pct / 100);
  if (fw > 0) {
    graphics_fill_rect(ctx, GRect(box.origin.x, box.origin.y, fw, box.size.h),
                       0, GCornerNone);
  }
}

// ---------------------------------------------------------------------------
// Row layout
// ---------------------------------------------------------------------------

// Places a glyph and its text as one centred group. The glyph is square and
// most of the row's height, which lines its optical centre up with the text's
// without having to know the font's internal padding.
static void slot_layout(GRect box, GFont font, const char *text,
                        GRect *glyph_box, GRect *text_box) {
  // 85% of the row's height, not all of it: the box carries the font's ascender
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

// Clear the pair's footprint before drawing into it, for the same reason the clock
// and the date do: a marker deep enough to reach this far in stops at the text
// instead of crossing it.
// Kept to the glyph's vertical band rather than the row box's full height: the box
// carries padding, and clearing all of it takes a bite out of whatever notch sits
// just past the row's edge.
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

// ---------------------------------------------------------------------------
// The rows
// ---------------------------------------------------------------------------

void slots_draw_count(GContext *ctx, const Layout *lo, GFont font, time_t now) {
  // Calendar markers and this countdown stay on show even when the companion is
  // gone, unlike the notification counts this face used to carry. An entry is
  // timestamped and ages out on its own, so it does not go stale the way a count
  // does — and the warnings row is already saying the companion is unreachable, so
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

  // The digits sit in the row above the bar's reserved strip, so they hold still
  // whichever direction the count is running.
  GRect row = lo->count_box;
  row.size.h -= PROGRESS_GAP + lo->bar_h;

  GRect gbox, tbox;
  slot_layout(row, font, text, &gbox, &tbox);
  slot_knock_out(ctx, gbox, tbox);

  if (p.kind == EV_TASK && !p.counting_up) glyph_task(ctx, gbox, ink);
  else glyph_calendar(ctx, gbox, ink);

  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, text, font, tbox, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);

  if (p.counting_up) {
    GRect bar = GRect(tbox.origin.x,
                      lo->count_box.origin.y + lo->count_box.size.h - lo->bar_h,
                      tbox.size.w - 4, lo->bar_h);
    knock_out(ctx, bar);
    draw_progress(ctx, bar, p.pct, ink);
  }
}

void slots_draw_nav(GContext *ctx, const Layout *lo, GFont font) {
  if (!wire_nav_active()) return;

  char text[16] = "";
  fmt_distance(text, sizeof text, wire_nav_distance(), wire_nav_unit());

  GRect gbox, tbox;
  slot_layout(lo->nav_box, font, text, &gbox, &tbox);
  slot_knock_out(ctx, gbox, tbox);

  glyph_maneuver(ctx, gbox, wire_nav_maneuver(), COL_INK);
  graphics_context_set_text_color(ctx, COL_INK);
  graphics_draw_text(ctx, text, font, tbox, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
}

// The bottom row: what the watch cannot vouch for, and what is about to run out.
//
// Strict priority — the first thing that is true is the only thing shown. A low
// phone battery therefore suppresses the watch's own warning, which is deliberate:
// one row, one thing, and the phone is the half of the system that this face
// depends on and cannot see for itself.
void slots_draw_warn(GContext *ctx, const Layout *lo, GFont font) {
  char text[16] = "";
  GColor ink = COL_INK;
  enum { T_NONE, T_DOWN, T_PHONE, T_WATCH } which = T_NONE;

  if (!wire_companion_alive()) {
    which = T_DOWN;
    ink = COL_ALERT;
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
  slot_layout(lo->warn_box, font, text, &gbox, &tbox);
  slot_knock_out(ctx, gbox, tbox);

  switch (which) {
    case T_DOWN:  glyph_phone(ctx, gbox, true, ink); break;
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
