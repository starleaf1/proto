#include "slots.h"
#include "events.h"
#include "icons.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"
#include "wx.h"
#include "wxglyph.h"
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
// Whether the count is running out an appointment or running down to the next
// thing is carried by the row inverting: a filled box behind the glyph and the
// digits while something is under way, plain ink before anything starts. That
// reading has been a fill inside the glyph, a progress bar under the digits and a
// sign in front of them; the box is the first form that needs neither colour nor
// a glyph of its own, and it is the strip's own grammar at row size — a running
// band is the deeper one, and a running count is the filled one.
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

// Places a glyph and its text as one group, aligned the way every other row is —
// against the strip. See ROW_ALIGN. The glyph leads: it is what says which kind of row
// this is, so it belongs on the edge the eye arrives at.
//
// The glyph is square and most of the row's height, which lines its optical centre up
// with the text's without having to know the font's internal padding.
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
  int16_t x = box.origin.x;

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
  char digits[12];   // sized past the clamp to keep snprintf quiet
  snprintf(digits, sizeof digits, "%02d:%02d", (int)(mins / 60), (int)(mins % 60));

  // COL_WARN, not COL_TASK_SOON. The strip's marker for this same entry is amber and
  // stays amber — it is a solid triangle and carries 1.9:1 fine — but these are digits,
  // and digits at slot size need the darker end of the same warm family. See theme.h.
  GColor ink = (p.kind == EV_TASK && !p.running) ? COL_WARN : COL_BAND;

  GRect gbox, tbox;
  slot_layout(lo->count_box, font, digits, &gbox, &tbox);

  // The glyph is centred on the digits' ink rather than on the row. Gothic sets its
  // baseline on the text box's last row and digits stand five eighths of the text
  // height above it, so the ink sits low in a row that carries four pixels of pad —
  // two and a half below the row's centre at 24 px. The box has to clear both the
  // glyph and the digits evenly, which it cannot do while they are centred apart.
  // Applied in both states, so nothing moves when the box appears.
  GSize ts = graphics_text_layout_get_content_size(digits, font, lo->count_box,
                                                   GTextOverflowModeFill,
                                                   GTextAlignmentLeft);
  int16_t pad = COUNT_PAD(ts.h);
  gbox.origin.x += pad;
  tbox.origin.x += pad;
  int16_t ink_bot = tbox.origin.y + ts.h;
  int16_t ink_mid = (ink_bot - ts.h * 5 / 8 + ink_bot) / 2;
  gbox.origin.y = ink_mid - gbox.size.h / 2;

  if (p.running) {
    // Sized off the glyph, which is the taller of the two here — most of a row that
    // carries pad — with COUNT_PAD on every side. It replaces the knock-out: it covers
    // more than the knock-out did, and in COL_BAND rather than COL_BG.
    int16_t x0 = gbox.origin.x - pad;
    int16_t x1 = tbox.origin.x + ts.w + pad;
    GRect fill = GRect(x0, gbox.origin.y - pad, x1 - x0, gbox.size.h + 2 * pad);
    graphics_context_set_fill_color(ctx, COL_BAND);
    graphics_fill_rect(ctx, fill, pad, GCornersAll);
    ink = COL_BG;
  } else {
    slot_knock_out(ctx, gbox, tbox);
  }

  // A running task is drawn as a running appointment, as it always was: the box is what
  // says "under way", and the glyph inside it is the one form drawn for the box.
  uint32_t res = p.running ? RESOURCE_ID_ICON_COUNT_RUNNING
               : p.kind == EV_TASK ? RESOURCE_ID_ICON_COUNT_TASK
               : RESOURCE_ID_ICON_COUNT_APPOINTMENT;
  icon_draw(ctx, gbox, res, false);

  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, digits, font, tbox, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
}

// ---------------------------------------------------------------------------
// Severe weather
//
// From WX_NEAR_S before its onset until it ends: plain ink counting down to the
// onset, then the countdown's own filled box counting down to the end — a running
// thing is the filled one, here as on the strip.
//
// It ranks below the maneuver and above both batteries. A turn is wrong within the
// minute, a storm within the hour, a battery all day. The rows here are stacked rather
// than sharing a strip, so the order needs one rule and no resolver: nav's row carries
// the weather while there is no turn, and the bottom row carries it when there is.
// It stays while the companion is gone — like the countdown, it is timestamped and
// ends on its own, and the bottom row is the one saying the companion is gone.
// ---------------------------------------------------------------------------

static void fmt_wx(char *buf, size_t n, int32_t seconds) {
  // hh:mm, like a clock. Minutes rounded up, so an onset a few seconds away reads
  // "00:01" and never "00:00"; clamped at 99:59 so the string can never grow.
  int32_t mins = (seconds + 59) / 60;
  if (mins > 99 * 60 + 59) mins = 99 * 60 + 59;
  snprintf(buf, n, "%02d:%02d", (int)(mins / 60), (int)(mins % 60));
}

static void draw_wx_reading(GContext *ctx, GRect row, GFont font, const WxPick *p) {
  char text[12];   // sized past "99:59" to keep snprintf quiet
  fmt_wx(text, sizeof text, p->seconds);

  GRect gbox, tbox;
  slot_layout(row, font, text, &gbox, &tbox);
  GSize ts = graphics_text_layout_get_content_size(text, font, row,
                                                   GTextOverflowModeFill,
                                                   GTextAlignmentLeft);
  // The countdown's geometry exactly, so a running storm's box and a running meeting's
  // are the same shape: inset by the pad, glyph centred on the text's ink.
  int16_t pad = COUNT_PAD(ts.h);
  gbox.origin.x += pad;
  tbox.origin.x += pad;
  int16_t ink_bot = tbox.origin.y + ts.h;
  int16_t ink_mid = (ink_bot - ts.h * 5 / 8 + ink_bot) / 2;
  gbox.origin.y = ink_mid - gbox.size.h / 2;

  // Black, not the timeline's colours: the icon carries the hazard, and the number
  // beside it is a time, inked like every other time on the face.
  GColor ink = COL_INK;
  if (p->running) {
    int16_t x0 = gbox.origin.x - pad;
    int16_t x1 = tbox.origin.x + ts.w + pad;
    GRect fill = GRect(x0, gbox.origin.y - pad, x1 - x0, gbox.size.h + 2 * pad);
    graphics_context_set_fill_color(ctx, COL_INK);
    graphics_fill_rect(ctx, fill, pad, GCornersAll);
    ink = COL_BG;
  } else {
    slot_knock_out(ctx, gbox, tbox);
  }


  // The icons are bitmaps with their own inks, black on flint, so on the filled box
  // they would vanish into it. They sit on a tile of background instead, a pixel
  // proud of the icon: the box still says "running", and the icon still reads.
  if (p->running) {
    graphics_context_set_fill_color(ctx, COL_BG);
    graphics_fill_rect(ctx, grect_inset(gbox, GEdgeInsets(-1)), 2, GCornersAll);
  }
  wx_glyph_slot(ctx, gbox, p->alert->hazard);
  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, text, font, tbox, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
}

void slots_draw_nav(GContext *ctx, const Layout *lo, GFont font, time_t now) {
  if (!wire_nav_active()) {
    WxPick wp = wx_pick_slot(now);
    if (wp.alert) draw_wx_reading(ctx, lo->nav_box, font, &wp);
    return;
  }

  char text[16] = "";
  fmt_distance(text, sizeof text, wire_nav_distance(), wire_nav_unit());

  GRect gbox, tbox;
  slot_layout(lo->nav_box, font, text, &gbox, &tbox);
  slot_knock_out(ctx, gbox, tbox);

  icon_nav(ctx, gbox);
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
void slots_draw_warn(GContext *ctx, const Layout *lo, GFont font, time_t now) {
  // Weather, when nav has its row: below the companion-down alert, above both
  // batteries.
  if (wire_companion_alive() && wire_nav_active()) {
    WxPick wp = wx_pick_slot(now);
    if (wp.alert) {
      draw_wx_reading(ctx, lo->warn_box, font, &wp);
      return;
    }
  }

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
    case T_DOWN:  icon_draw(ctx, gbox, RESOURCE_ID_ICON_WARN_PHONE_DOWN, false); break;
    case T_PHONE: icon_draw(ctx, gbox, RESOURCE_ID_ICON_WARN_PHONE_BATTERY, false); break;
    case T_WATCH: icon_draw(ctx, gbox, RESOURCE_ID_ICON_WARN_WATCH_BATTERY, false); break;
    default: break;
  }
  if (text[0]) {
    graphics_context_set_text_color(ctx, ink);
    graphics_draw_text(ctx, text, font, tbox, GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
  }
}
