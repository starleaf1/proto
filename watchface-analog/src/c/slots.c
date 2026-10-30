#include "slots.h"
#include "digits.h"
#include "events.h"
#include "icons.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"
#include "wx.h"
#include "wxglyph.h"
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
// Whether the count is running out an appointment or running down to the next
// thing is carried by the row inverting: a filled box behind the glyph and the
// digits while something is under way, plain ink before anything starts. That
// reading has been a fill inside the glyph, a progress bar under the digits and a
// sign in front of them; the box is the first form that needs neither colour nor
// a glyph of its own, and it is the ring's own grammar at row size — a running
// band fills the ring, and a running count fills its row.
//
// Every glyph is a hand-drawn bitmap, one per display, drawn at its own size — see
// icons.h. They were drawn from primitives here once, which stayed crisp at three
// sizes and could not be drawn by hand; the glyph box each row lays out is exactly the
// size of the files.
// ---------------------------------------------------------------------------

#define PHONE_BATT_LOW   30   // percent
#define WATCH_BATT_LOW_H 24   // hours

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

// The glyph-to-number gap is SLOT_GAP, in geometry.h, because the plate is sized
// against the same numbers this lays out with.
//
// `ts` is the number's size, measured by the caller: text_size() for a string set in
// the slot font, digits_size() for the countdown's drawn digits.
static SlotBoxes slot_layout(GRect box, GSize ts, GTextAlignment align) {

  // 85% of the box's height, not all of it: the box carries the font's ascender
  // and descender, and a glyph matched to the cap height reads as the same size
  // as the digits beside it.
  int16_t side = box.size.h * 85 / 100;
  int16_t gap = ts.w > 0 ? SLOT_GAP(side) : 0;
  int16_t total = side + gap + ts.w;

  // Too wide? Shrink the glyph before changing the shape of the row. The glyph
  // is the qualifier and the number is the reading, so the glyph is what should
  // give way — and a row that keeps its shape keeps its alignment with the one
  // opposite it. With a gap of side/div, side + gap is side*(div+1)/div and the
  // largest side that fits is div/(div+1) of what is left over.
  if (total > box.size.w) {
    int16_t avail = box.size.w - ts.w;
    int16_t want = avail * SLOT_GAP_DIV / (SLOT_GAP_DIV + 1);
    int16_t floor_side = box.size.h * 55 / 100;
    if (want >= floor_side) {
      side = want;
      gap = SLOT_GAP(side);
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

static GSize text_size(GRect box, GFont font, const char *text) {
  if (!text || !text[0]) return GSize(0, 0);
  return graphics_text_layout_get_content_size(text, font, box, GTextOverflowModeFill,
                                               GTextAlignmentLeft);
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

void slots_draw_count(GContext *ctx, const Layout *lo, time_t now) {
  // Calendar markers and this countdown stay on show even when the companion is
  // gone. An entry is timestamped and ages out on its own, so it does not go
  // stale the way a count does — and the notification band is already saying the
  // companion is unreachable, so nothing here is claiming to be complete.
  SlotPick p = events_pick_slot(now);
  if (!p.valid) return;

  // "%d:%02d", not the digital face's "%02d:%02d". One digit of hours rather than
  // two, which is what keeps the number narrow enough for flint's plate — and
  // nothing is lost, because the far tier stops at five hours and all-day events
  // never cross the wire. Clamped at 9:59 so it can never grow.
  int32_t mins = p.seconds / 60;
  if (mins > 9 * 60 + 59) mins = 9 * 60 + 59;
  char digits[12];   // sized past the clamp to keep snprintf quiet
  snprintf(digits, sizeof digits, "%d:%02d", (int)(mins / 60), (int)(mins % 60));

  // COL_WARN, not COL_TASK_SOON. The ring's marker for this same entry is amber
  // and stays amber — it is a solid wedge and carries 1.9:1 fine — but these are
  // digits, and digits at slot size need the darker end of the same warm family.
  GColor ink = (p.kind == EV_TASK && !p.running) ? COL_WARN : COL_BAND;

  // Drawn, not set — see digits.h — so the ink is exactly digits_size() and the
  // row's centre is the ink's. The glyph, the digits and the running box all centre on
  // it, with none of the measured lift a font's box needs.
  GSize ts = digits_size(digits, lo->cell);
  SlotBoxes b = slot_layout(lo->count_box, ts, GTextAlignmentCenter);
  int16_t ink_mid = lo->count_box.origin.y + lo->count_box.size.h / 2;
  b.glyph.origin.y = ink_mid - b.glyph.size.h / 2;
  GPoint at = GPoint(b.text.origin.x, ink_mid - ts.h / 2);

  if (p.running) {
    // COUNT_PAD on every side, which layout_compute() reserved in both states, so
    // the content sits in the same place whether or not the box is behind it. The
    // row is the ink plus that pad above and below, so the box is the row.
    int16_t pad = COUNT_PAD(ts.h);
    int16_t bh = lo->count_box.size.h;
    int16_t x0 = b.glyph.origin.x - pad;
    int16_t x1 = at.x + ts.w + pad;
    GRect fill = GRect(x0, ink_mid - bh / 2, x1 - x0, bh);
    graphics_context_set_fill_color(ctx, COL_BAND);
    graphics_fill_rect(ctx, fill, pad, GCornersAll);
    ink = COL_BG;
  }

  // A running task is drawn as a running appointment, as it always was: the box is what
  // says "under way", and the glyph inside it is the one form drawn for the box.
  uint32_t res = p.running ? RESOURCE_ID_ICON_COUNT_RUNNING
               : p.kind == EV_TASK ? RESOURCE_ID_ICON_COUNT_TASK
               : RESOURCE_ID_ICON_COUNT_APPOINTMENT;
  icon_draw(ctx, b.glyph, res, false);

  digits_draw(ctx, digits, at, lo->cell, ink);
}

// ---------------------------------------------------------------------------
// Weather in the notification area
//
// An alert reaches the slot WX_NEAR_S before its onset and stays until it ends:
// plain ink counting down to the onset, then the countdown's own filled box
// counting down to the end — a running thing fills its row, here as there.
//
// It ranks below the maneuver and above both batteries. A turn is wrong within
// the minute; a storm is wrong within the hour; a battery is true all day.
// ---------------------------------------------------------------------------

enum { WX_AT_NONE, WX_AT_NAV, WX_AT_WARN };

// Which box the weather reading draws in, if any. One answer for both rows, so
// slots_draw_nav() and slots_draw_warn() cannot each decide it is theirs.
//
// On the round display the rows are one slot, and weather has it whenever the
// maneuver and the companion-down glyph do not. On the rectangles it takes nav's
// half when there is no turn to show, and warn's when there is — but only while
// the companion answers, because DISCONNECTED spells itself across nav's half and
// is the one reading that outranks it.
static int wx_at(const Layout *lo, const WxPick *p) {
  if (!p->alert || !wire_companion_alive()) return WX_AT_NONE;
  if (lo->band_inset) return wire_nav_active() ? WX_AT_NONE : WX_AT_WARN;
  return wire_nav_active() ? WX_AT_WARN : WX_AT_NAV;
}

static void fmt_wx(char *buf, size_t n, int32_t seconds) {
  // hh:mm, like a clock. Minutes rounded up, so an onset a few seconds away reads
  // "00:01" and never "00:00"; clamped at 99:59 so the string can never grow.
  int32_t mins = (seconds + 59) / 60;
  if (mins > 99 * 60 + 59) mins = 99 * 60 + 59;
  snprintf(buf, n, "%02d:%02d", (int)(mins / 60), (int)(mins % 60));
}

static void draw_wx_reading(GContext *ctx, GRect box, GTextAlignment align,
                            GFont font, const WxPick *p) {
  char text[12];   // sized past "99:59" to keep snprintf quiet
  fmt_wx(text, sizeof text, p->seconds);
  GSize ts = text_size(box, font, text);

  // The running box pads outward from the content, so the content gives up that
  // pad first; otherwise a left-aligned reading would push its box off the glass.
  int16_t pad = COUNT_PAD(ts.h);
  if (p->running) box = grect_inset(box, GEdgeInsets(0, pad));
  SlotBoxes b = slot_layout(box, ts, align);

  // Black, not the timeline's colours: the icon carries the hazard, and the number
  // beside it is a time, inked like every other time on the face.
  GColor ink = COL_INK;
  if (p->running) {
    int16_t x0 = b.glyph.origin.x - pad;
    int16_t x1 = b.text.origin.x + ts.w + pad;
    graphics_context_set_fill_color(ctx, COL_INK);
    graphics_fill_rect(ctx, GRect(x0, box.origin.y, x1 - x0, box.size.h), pad, GCornersAll);
    ink = COL_BG;
  }


  // The icons are bitmaps with their own inks, black on flint, so on the filled box
  // they would vanish into it. They sit on a tile of background instead, a pixel
  // proud of the icon: the box still says "running", and the icon still reads.
  if (p->running) {
    graphics_context_set_fill_color(ctx, COL_BG);
    graphics_fill_rect(ctx, grect_inset(b.glyph, GEdgeInsets(-1)), 2, GCornersAll);
  }
  wx_glyph_slot(ctx, b.glyph, p->alert->hazard);
  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, text, font, b.text, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
}

void slots_draw_nav(GContext *ctx, const Layout *lo, GFont font, time_t now) {
  if (!wire_nav_active()) {
    WxPick wp = wx_pick_slot(now);
    if (wx_at(lo, &wp) == WX_AT_NAV) draw_wx_reading(ctx, lo->nav_box, NAV_ALIGN, font, &wp);
    return;
  }

  char text[16] = "";
  fmt_distance(text, sizeof text, wire_nav_distance(), wire_nav_unit());

  SlotBoxes b = slot_layout(lo->nav_box, text_size(lo->nav_box, font, text), NAV_ALIGN);

  icon_nav(ctx, b.glyph);
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
// string each row can hold, and a twelve-character label would grow it over the dial
// permanently to state something that is true for minutes at a time. The glyph carries
// that case alone there, as it did everywhere before.
void slots_draw_warn(GContext *ctx, const Layout *lo, GFont font, time_t now) {
  // Where the slot is shared, the maneuver has it. See above.
  if (lo->band_inset && wire_nav_active()) return;

  // Then weather, where the resolver puts it here. Below the companion-down
  // alert, which wx_at() already defers to, and above both batteries.
  WxPick wp = wx_pick_slot(now);
  if (wx_at(lo, &wp) == WX_AT_WARN) {
    draw_wx_reading(ctx, lo->warn_box, WARN_ALIGN, font, &wp);
    return;
  }

  char text[16] = "";
  GColor ink = COL_INK;
  enum { T_NONE, T_DOWN, T_PHONE, T_WATCH } which = T_NONE;

  if (!wire_companion_alive()) {
    which = T_DOWN;
    ink = COL_ALERT;
    if (!lo->band_inset) snprintf(text, sizeof text, "DISCONNECTED");
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

  SlotBoxes b = slot_layout(box, text_size(box, font, text), WARN_ALIGN);

  switch (which) {
    case T_DOWN:  icon_draw(ctx, b.glyph, RESOURCE_ID_ICON_WARN_PHONE_DOWN, false); break;
    case T_PHONE: icon_draw(ctx, b.glyph, RESOURCE_ID_ICON_WARN_PHONE_BATTERY, false); break;
    case T_WATCH: icon_draw(ctx, b.glyph, RESOURCE_ID_ICON_WARN_WATCH_BATTERY, false); break;
    default: break;
  }
  if (text[0]) {
    graphics_context_set_text_color(ctx, ink);
    graphics_draw_text(ctx, text, font, b.text, GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
  }
}
