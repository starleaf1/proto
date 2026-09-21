#include "slots.h"
#include "events.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"
#include <stdio.h>

// ---------------------------------------------------------------------------
// The three conditional readings share one grammar: [glyph] number suffix.
//
// The countdown's task glyph is deliberately a miniature of the ring's point
// marker, so the face teaches that half of its vocabulary once instead of twice.
// The appointment glyph used to work the same way — a short bar, a band unrolled
// — and it did not survive contact: a rounded bar at slot size reads as a pill or
// a battery, not as anything to do with a calendar. It is Material Design's
// calendar mark now. The echo of the strip was worth less than being recognised.
//
// Which direction the count is running is carried by a sign in front of the
// digits — '+' inside something, '-' before it. That reading used to live inside
// the glyph, as a fill, and then under the digits as a progress bar; a sign is
// the first form of it that states the direction in both cases rather than
// leaving one of them to be inferred from an absence, and the only one that needs
// no space of its own beyond the line it is on. The '+' rides on the digits' cap
// line and the '-' on their baseline, so the sign says it twice — once by shape
// and once by height — inside the slack the line already carries.
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

// The tables fill the grid, and the head is larger relative to the shaft than an arrow
// drawn at any comfortable size would be. A head merely a little wider than its own
// shaft disappears here — the first attempt drew a recognisable "turn right" that read
// as the letter Γ.
//
// It used to be larger still, 32 long by 20 half-wide, and that was tuned against a
// shaft that turned out to be one pixel — see glyph_stroke. Against the three pixels
// the shaft is now, a head two thirds of the grid across swallowed the shaft whole and
// the glyph came off emery as a solid lump with no direction in it at all. These
// numbers keep the head about three times the shaft's width, which is the ratio that
// makes it read as a head, and no more.
#define GRID 36
#define HEAD_LEN  26
#define HEAD_HALF 14

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
//
// The divisor sets the stroke's ratio to the glyph; the floor is what makes the glyph
// exist at all. A slot glyph is `row_h * 85/100` — 14 px on flint, about 21 on emery
// and 21 on gabbro — so the divisor of nine this used to carry landed stroke_px() on 1
// for every one of them, on every display. Measured on emery, which is not even the
// small one: a sharp-left maneuver whose shaft is a single pixel under a filled head
// reads as a blob with a whisker, and the phone silhouette reads as an empty box.
//
// Three, because stroke_px() yields only odd widths and there is nothing between a
// hairline and three. The floor was at 16 px of glyph, which flint has never reached —
// its band glyph was 13 px under the Rajdhani resources and is 14 under Gothic, so the
// one display that most needs the weight was the one display not getting it. That was
// survivable while the text beside it was a condensed semibold TTF rasterised light;
// beside a system Gothic Bold a hairline maneuver reads as a different kind of mark
// from the number it qualifies.
//
// Fourteen now, measured rather than assumed: at 14 px of glyph with a 3 px stroke
// flint's phone silhouette keeps four pixels of interior and still reads as an outline.
// The battery and the calendar never reach this floor — they are mostly interior and
// drawn at a hairline on purpose.
static int16_t glyph_stroke(int16_t side, int16_t div) {
  int16_t w = stroke_px(side / div);
  if (w < 3 && side >= 14) w = 3;
  return w;
}

static int16_t sw_for(int16_t side) {
  return glyph_stroke(side, 6);
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
             ink, false);
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
  draw_tri(ctx, tip, b1, b2, ink, false);
}

// A phone silhouette. Also the "whose battery" cue: this outline against the
// battery pictogram below is what lets both alerts drop the word "phone".
static void glyph_phone(GContext *ctx, GRect box, bool slashed, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;
  int16_t w = side * 58 / 100;
  int16_t h = side * 92 / 100;
  int16_t sw = glyph_stroke(side, 8);

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
//
// This one keeps the hairline the maneuvers and the phone gave up — see glyph_stroke.
// A cell is half the height of the glyph box, eight pixels of it on flint, so the 3 px
// floor would leave two pixels of interior and the outline would read as a solid slab.
// The nub is what makes this a battery and not a rectangle, and the nub survives at
// one pixel.
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
//
// Hairline outline here too, for the battery's reason — this shape is mostly interior,
// and it is the open lower half against the filled header band that makes it read as a
// calendar. Three pixels of stroke closes the half that has to stay open.
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

// A task: the ring's point marker, shrunk. It keeps the wedge's proportions —
// blunt-tipped, about two and a half times as wide as it tapers to — which is
// what makes it read as the same vocabulary. It points along the reading
// direction rather than at the centre, because in a plate there is no centre for
// it to mean anything about.
static void glyph_task(GContext *ctx, GRect box, GColor ink) {
  GPoint c = grect_center_point(&box);
  int16_t side = box.size.w < box.size.h ? box.size.w : box.size.h;
  int16_t hw = side * 40 / 100;
  int16_t hh = side * 35 / 100;
  draw_tri(ctx, GPoint(c.x + hw, c.y),
           GPoint(c.x - hw, c.y - hh), GPoint(c.x - hw, c.y + hh),
           ink, false);
}

// ---------------------------------------------------------------------------
// Row layout
// ---------------------------------------------------------------------------

// Places a glyph and its text as one group.
//
// Two forms, chosen by measurement rather than by platform. The one-row form —
// glyph, gap, number — is what the digital face uses everywhere, and it needs
// about fifty pixels at slot size. flint's plates are thirty-three wide, so it
// does not fit there and would silently clip; the fallback stacks the glyph over
// the number instead, which costs height a plate has and width it does not.
//
// Measuring rather than branching on PBL_PLATFORM_* is what keeps this honest
// when a font size changes: what fits is a property of the box, not of the
// display.
typedef struct {
  GRect glyph;
  GRect text;
} SlotBoxes;

// `gap_div` is the glyph-to-number gap as a fraction of the glyph — SLOT_GAP_DIV
// on the rows whose number starts with a digit, and the narrower COUNT_GAP_DIV on
// the countdown, whose number starts with a sign. Both live in geometry.h, because
// the plate is sized against the same numbers this lays out with.
static SlotBoxes slot_layout(GRect box, GFont font, const char *text,
                             GTextAlignment align, int16_t gap_div) {
  GSize ts = GSize(0, 0);
  if (text && text[0]) {
    ts = graphics_text_layout_get_content_size(text, font, box,
                                               GTextOverflowModeFill,
                                               GTextAlignmentLeft);
  }

  // 85% of the box's height, not all of it: the box carries the font's ascender
  // and descender, and a glyph matched to the cap height reads as the same size
  // as the digits beside it.
  int16_t side = box.size.h * 85 / 100;
  int16_t gap = ts.w > 0 ? SLOT_GAP(side, gap_div) : 0;
  int16_t total = side + gap + ts.w;

  // Too wide? Shrink the glyph before changing the shape of the row. The glyph
  // is the qualifier and the number is the reading, so the glyph is what should
  // give way — and a row that keeps its shape keeps its alignment with the one
  // opposite it. With a gap of side/div, side + gap is side*(div+1)/div and the
  // largest side that fits is div/(div+1) of what is left over.
  if (total > box.size.w) {
    int16_t avail = box.size.w - ts.w;
    int16_t want = avail * gap_div / (gap_div + 1);
    int16_t floor_side = box.size.h * 55 / 100;
    if (want >= floor_side) {
      side = want;
      gap = SLOT_GAP(side, gap_div);
      total = side + gap + ts.w;
    }
  }

  if (total <= box.size.w) {
    int16_t x = box.origin.x;
    if (align == GTextAlignmentCenter)     x += (box.size.w - total) / 2;
    else if (align == GTextAlignmentRight) x += box.size.w - total;
    return (SlotBoxes){
      .glyph = GRect(x, box.origin.y + (box.size.h - side) / 2, side, side),
      .text  = GRect(x + side + gap, box.origin.y - ROW_LIFT(ts.h),
                     ts.w + 4, ts.h),
    };
  }

  // Stacked. The glyph takes the shorter line, because it is the qualifier and
  // the number is the reading.
  int16_t top = box.size.h * 45 / 100;
  int16_t gside = top < box.size.w ? top : box.size.w;
  return (SlotBoxes){
    .glyph = GRect(box.origin.x + (box.size.w - gside) / 2, box.origin.y,
                   gside, gside),
    .text  = GRect(box.origin.x + (box.size.w - ts.w) / 2,
                   box.origin.y + top - ROW_LIFT(ts.h), ts.w + 4, ts.h),
  };
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
  // gone. An entry is timestamped and ages out on its own, so it does not go
  // stale the way a count does — and the notification band is already saying the
  // companion is unreachable, so nothing here is claiming to be complete.
  SlotPick p = events_pick_slot(now);
  if (!p.valid) return;

  // "%d:%02d", not the digital face's "%02d:%02d". One digit of hours rather than
  // two, which is what keeps the number narrow enough for flint's plate — and
  // nothing is lost, because the far tier reaches five hours and a count-up cannot
  // outrun a meeting. Clamped at 9:59 so it can never grow.
  int32_t mins = p.seconds / 60;
  if (mins > 9 * 60 + 59) mins = 9 * 60 + 59;
  char digits[12];
  snprintf(digits, sizeof digits, "%d:%02d", (int)(mins / 60), (int)(mins % 60));

  // The sign is the whole of the direction cue: '+' for time already spent inside
  // something running, '-' for time still to go before something starts. It replaced
  // a progress bar under the digits, which needed a strip of the disc reserved whether
  // or not it drew and still could not say which way the number was moving without
  // being present — an absent bar is not a readable state. A sign is one glyph on the
  // line it qualifies, it is the same shape on all three displays, and it reads with
  // no colour at all, which is what flint has. It costs the disc one glyph of width,
  // which is why the plate is measured against "+0:00" in layout_compute.
  //
  // The two signs also sit at two heights — the '+' up on the digits' cap line, the
  // '-' down on their baseline — so the cue is a position as well as a shape. See
  // SIGN_RISE.
  //
  // Laid out as though the sign were always '+', which is the wider of the two and
  // what the plate was budgeted against; the sign that actually draws goes
  // left-aligned into that width. So the digits keep their column and no part of the
  // row moves at the minute the count turns over.
  char sized[16];   // the sign, then H:MM; sized past the clamp to keep snprintf quiet
  snprintf(sized, sizeof sized, "+%s", digits);

  // COL_WARN, not COL_TASK_SOON. The ring's marker for this same entry is amber
  // and stays amber — it is a solid wedge and carries 1.9:1 fine — but these are
  // digits, and digits at slot size need the darker end of the same warm family.
  GColor ink = COL_INK;
  if (p.counting_up) ink = COL_ACCENT;
  else if (p.kind == EV_TASK) ink = COL_WARN;

  SlotBoxes b = slot_layout(lo->count_box, font, sized, GTextAlignmentCenter,
                            COUNT_GAP_DIV);

  if (p.kind == EV_TASK && !p.counting_up) glyph_task(ctx, b.glyph, ink);
  else glyph_calendar(ctx, b.glyph, ink);

  // What the sign is worth in width, taken as the difference it makes to the string
  // rather than measured on its own: a glyph measured alone carries both its side
  // bearings, and the two draws have to add up to exactly the width the row was laid
  // out for.
  GSize full = graphics_text_layout_get_content_size(sized, font, lo->count_box,
                                                     GTextOverflowModeFill,
                                                     GTextAlignmentLeft);
  GSize bare = graphics_text_layout_get_content_size(digits, font, lo->count_box,
                                                     GTextOverflowModeFill,
                                                     GTextAlignmentLeft);
  int16_t sign_w = full.w - bare.w;
  if (sign_w < 1) sign_w = 1;

  // Two draws, because the pair sits at two heights. The digits land where they
  // always did — the sign's width is reserved in front of them either way.
  const char sign[2] = { p.counting_up ? '+' : '-', '\0' };
  GRect sbox = b.text;
  sbox.origin.y += p.counting_up ? -SIGN_RISE(full.h) : SIGN_RISE(full.h);
  GRect dbox = b.text;
  dbox.origin.x += sign_w;

  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, sign, font, sbox, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, digits, font, dbox, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
}

void slots_draw_nav(GContext *ctx, const Layout *lo, GFont font) {
  if (!wire_nav_active()) return;

  char text[16] = "";
  fmt_distance(text, sizeof text, wire_nav_distance(), wire_nav_unit());

  SlotBoxes b = slot_layout(lo->nav_box, font, text, NAV_ALIGN, SLOT_GAP_DIV);

  glyph_maneuver(ctx, b.glyph, wire_nav_maneuver(), COL_INK);
  graphics_context_set_text_color(ctx, COL_INK);
  graphics_draw_text(ctx, text, font, b.text, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
}

// What the watch cannot vouch for, and what is about to run out.
//
// Strict priority — the first thing that is true is the only thing shown. A low
// phone battery therefore suppresses the watch's own warning, which is
// deliberate: one slot, one thing, and the phone is the half of the system that
// this face depends on and cannot see for itself.
//
// On the round display the maneuver is in that priority too, above both
// batteries, because there this row and nav's are the same row. A turn
// instruction is perishable — it is wrong within the minute if it is not acted
// on — where a battery reading is true all day and will still be there after the
// junction. So nav takes the slot while it has one, and the guard below is the
// whole of that rule; the state that outranks nav, the companion being gone,
// needs no guard because both roads into it null the maneuver upstream.
//
// On the rectangles nothing is suppressed: there the band is a strip of screen
// wide enough for two readings, and this one is the right of it, against nav's
// left. They are the only two things on this face that are not a time.
//
// The companion-down state is the one reading here that has no number, and on the
// rectangles it says so in words. The slashed phone alone is the only glyph on this
// face asked to carry a whole state with nothing beside it — the other two are
// qualifiers on a figure — and a reader who has not learnt it has nothing to learn it
// from. Where the band is a strip of screen there is room to spell it out.
//
// Not on the round display, and that is a width argument rather than a preference:
// there the pair are rows of the disc, layout_compute sizes the disc from the widest
// string each row can hold, and a seven-character label would grow it over the dial
// permanently to state something that is true for minutes at a time. The glyph carries
// that case alone there, as it did everywhere before.
void slots_draw_warn(GContext *ctx, const Layout *lo, GFont font) {
  // Where the slot is shared, the maneuver has it. See above.
  if (lo->band_inset && wire_nav_active()) return;

  char text[16] = "";
  GColor ink = COL_INK;
  enum { T_NONE, T_DOWN, T_PHONE, T_WATCH } which = T_NONE;

  if (!wire_companion_alive()) {
    which = T_DOWN;
    ink = COL_ALERT;
    if (!lo->band_inset) snprintf(text, sizeof text, "NO LINK");
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

  // The label is wider than warn's own share of the band, and it borrows nav's.
  //
  // Which is free rather than a gamble: both roads into this state put nav out of
  // it. The watchdog's expiry drops the maneuver (hb_expired) and so does losing
  // the link (wire_set_connected), so wire_nav_active() is false for as long as
  // this row is drawing and the half of the band to the left of it is empty by
  // construction. Right-aligned either way, so the pair sits where it sat when it
  // was a glyph on its own and grows leftward into the room rather than shifting
  // into it — nothing on the band moves when the label appears.
  GRect box = lo->warn_box;
  if (which == T_DOWN && !lo->band_inset) {
    int16_t x0 = lo->nav_box.origin.x;
    box = GRect(x0, box.origin.y,
                box.origin.x + box.size.w - x0, box.size.h);
  }

  SlotBoxes b = slot_layout(box, font, text, WARN_ALIGN, SLOT_GAP_DIV);

  switch (which) {
    case T_DOWN:  glyph_phone(ctx, b.glyph, true, ink); break;
    case T_PHONE: glyph_phone(ctx, b.glyph, false, ink); break;
    case T_WATCH: glyph_battery(ctx, b.glyph, ink); break;
    default: break;
  }
  if (text[0]) {
    graphics_context_set_text_color(ctx, ink);
    graphics_draw_text(ctx, text, font, b.text, GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
  }
}
