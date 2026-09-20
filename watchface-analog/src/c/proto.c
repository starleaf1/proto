#include <pebble.h>
#include "dial.h"
#include "events.h"
#include "geometry.h"
#include "slots.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"
#include <string.h>

// ---------------------------------------------------------------------------
// proto analog — a calendar-driven analog watchface.
//
// The rim is a six-hour timeline: one hour behind, five ahead, laid on the dial
// at the clock angle of each entry's own time. A three o'clock meeting is at the
// 3. Six hours is half a turn of a twelve-hour dial, so the ring runs at thirty
// degrees to the hour — the hour hand's own rate — and the hand is therefore the
// "now" mark by construction. It stops three pixels short of the ring, which stays
// the markers' lane alone.
//
// Appointments are arcs, tasks and reminders are wedges that cross the rail and
// point at the middle of the face. The date and the countdown to the next entry
// sit in a disc at the centre, drawn over the hands: it covers the half of each
// hand that carries no reading, and it is the only place on the face where text
// is safe from a hand at every minute. The next turn and whatever is running
// out share a band under the dial, or the disc's bottom row where the glass is
// round and there is no under.
//
// The watch computes the time, the date and its own battery. Everything else —
// the calendar, the phone's battery, the next turn — comes from the companion,
// and the band says so when the companion stops answering. Uncertainty is
// something this face states rather than something it silently omits.
//
// Nothing animates. The only tick is MINUTE_UNIT: the minute hand steps six
// degrees, the hour hand and the whole window half a degree, and recomputing
// the face is what moves them.
// ---------------------------------------------------------------------------

static Window *s_window;
static Layer  *s_root_layer;
static GFont   s_date_font;
static GFont   s_slot_font;

static struct tm s_tm;
static time_t    s_now = 0;

static char s_date_buf[16];  // "MON 22"

static void update_buffers(void) {
  // "%a %d", not "%a %d %b". The month is the one part of a date that a reader
  // already knows, and the disc's top row is sized from this string.
  strftime(s_date_buf, sizeof s_date_buf, "%a %d", &s_tm);
  for (char *p = s_date_buf; *p; p++) {   // the font subset has no lowercase
    if (*p >= 'a' && *p <= 'z') *p -= 32;
  }
}

static void mark_dirty(void) {
  if (s_root_layer) layer_mark_dirty(s_root_layer);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

static void root_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  Layout lo = layout_compute(b, s_date_font, s_slot_font);

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  dial_draw(ctx, &lo, s_now);
  dial_draw_hands(ctx, &lo, &s_tm);

  // The disc goes over the hands, which is the one ordering this face inverts
  // from the digital one. There, nothing was allowed to cover the "now" mark;
  // here "now" is the hour hand, and text a hand crosses twice an hour is
  // exactly what the disc is for. What it covers is the half of each hand
  // nearest the pivot, which carries no reading — the tips do — so the clock
  // loses nothing and the text is legible at every minute of the day.
  draw_disc(ctx, lo.center, lo.plate_r);

  // The box is the reserved row; the text is drawn at its full measured height
  // and lifted into it, which is what puts the ink where the row says it is. See
  // ROW_H in geometry.h.
  GSize dsz = graphics_text_layout_get_content_size(
      s_date_buf, s_date_font, b, GTextOverflowModeFill, GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, COL_INK);
  graphics_draw_text(ctx, s_date_buf, s_date_font,
                     GRect(lo.date_box.origin.x,
                           lo.date_box.origin.y - ROW_LIFT(dsz.h),
                           lo.date_box.size.w, dsz.h),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  slots_draw_count(ctx, &lo, s_slot_font, s_now);
  slots_draw_nav(ctx, &lo, s_slot_font);
  slots_draw_warn(ctx, &lo, s_slot_font);
}

// ---------------------------------------------------------------------------
// Service handlers
// ---------------------------------------------------------------------------

static void tick_handler(struct tm *t, TimeUnits units) {
  s_tm = *t;
  s_now = time(NULL);
  update_buffers();
  // Both hands, the window and every marker are functions of now, so the minute
  // tick is also what advances the countdown and retires anything that has aged
  // out. Nothing on this face moves faster than a minute.
  events_gc(s_now);
  mark_dirty();
}

static void battery_handler(BatteryChargeState state) {
  wbatt_update(state);
  mark_dirty();
}

static void conn_handler(bool connected) {
  wire_set_connected(connected);   // which calls back into mark_dirty
}

// ---------------------------------------------------------------------------
// Demo seed
// ---------------------------------------------------------------------------

#ifdef PROTO_DEMO
// Build with PROTO_DEMO=1 to seed a set covering every marker case at once.
// tools/send-demo-events.py sends the same set over the wire and is the better
// tool — it exercises the decode path in wire.c rather than bypassing it — so
// this is the fallback for tooling without `send-app-message --bytes`. The two
// must stay in step. See ../../CONTRIBUTING.md.
static void demo_seed(time_t now) {
  events_upsert(1,  now -  20 * 60, 90, EV_APPOINTMENT); // running: the hour hand is inside it
  events_upsert(2,  now + 100 * 60, 40, EV_APPOINTMENT); // these two overlap and
  events_upsert(3,  now + 120 * 60, 45, EV_APPOINTMENT); // must flatten to one band
  events_upsert(4,  now +  40 * 60, 45, EV_APPOINTMENT); // ordinary upcoming: half depth
  events_upsert(5,  now -  40 * 60,  0, EV_TASK);        // overdue: behind the hour hand
  events_upsert(6,  now + 160 * 60,  0, EV_TASK);        // 4 min apart -> too close
  events_upsert(7,  now + 164 * 60,  0, EV_TASK);        // -> one deeper, blunter wedge
  events_upsert(8,  now + 110 * 60,  0, EV_TASK);        // sits on top of a band
  events_upsert(9,  now + 270 * 60, 90, EV_APPOINTMENT); // runs past the horizon: clips
  events_upsert(10, now + 295 * 60,  0, EV_TASK);        // just inside the horizon
  events_upsert(11, now + 355 * 60,  0, EV_TASK);        // past it: must not draw
}
#endif

// ---------------------------------------------------------------------------
// Window / app lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_root_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_root_layer, root_update_proc);
  layer_add_child(root, s_root_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_root_layer);
  s_root_layer = NULL;
}

static void init(void) {
  // The firmware's own, at the size this platform wants — see theme.h. These are
  // handles into the firmware, not allocations, so there is nothing to unload and
  // nothing to fail: a bad key returns the fallback font rather than NULL.
  s_date_font = fonts_get_system_font(FONT_DATE);
  s_slot_font = fonts_get_system_font(FONT_SLOT);

  s_now = time(NULL);
  s_tm = *localtime(&s_now);
  update_buffers();
  events_clear();
  events_load();                 // what was on the ring when we were last closed
  wbatt_init();
#ifdef PROTO_DEMO
  demo_seed(s_now);
#endif

  s_window = window_create();
  window_set_background_color(s_window, COL_BG);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = conn_handler,
    .pebblekit_connection_handler = NULL,
  });
  wire_init(mark_dirty);
}

static void deinit(void) {
  // The firmware kills a watchface whenever the user opens anything else, so this
  // runs on every excursion, not just at the end of the day. Saving here is what
  // puts the ring back the way they left it when they come back.
  events_save();
  wire_deinit();                 // before window_destroy: its timers repaint
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
