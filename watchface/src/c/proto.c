#include <pebble.h>
#include "dial.h"
#include "events.h"
#include "geometry.h"
#include "slots.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"

// ---------------------------------------------------------------------------
// proto — a calendar-driven watchface.
//
// The dial is a six-hour timeline. Appointments in the next six hours are arcs
// spanning their duration, sitting in the notch zone with the notches cut
// through them; tasks and reminders are triangles poking inward from the notch
// they fall nearest. The hour wedge is what gives all of it a "now" to be
// relative to. See dial.c.
//
// A big minute numeral and the date sit in the middle. Two single-line slots sit
// inside the ring — the top one alerts, the bottom one counts down to whatever
// is next. See slots.c.
//
// The watch computes the time, the date and its own battery. Everything else —
// the calendar, the phone's battery, the next turn — comes from the companion,
// and the top slot's first priority is saying so when the companion stops
// answering. Uncertainty is something this face states rather than something it
// silently omits.
//
// All geometry derives from the root layer's bounds, so the same code lays out
// flint (144x168, one ink), emery (200x228) and gabbro (260x260, round).
// ---------------------------------------------------------------------------

static Window *s_window;
static Layer  *s_root_layer;
static GFont   s_num_font;
static GFont   s_date_font;
static GFont   s_slot_font;
static bool    s_custom_fonts = false;   // true if the TTFs loaded, so we unload

static struct tm s_tm;
static time_t    s_now = 0;

static char s_min_buf[4];    // "05" / "30"
static char s_date_buf[16];  // "MON 22" — see the width note in geometry.c

static void update_buffers(void) {
  // The big numeral is the minute; the hour is carried by the wedge on the dial,
  // which is also what the event markers are read against. Same in 12h and 24h
  // mode, so there is no clock-style branching here.
  strftime(s_min_buf, sizeof s_min_buf, "%M", &s_tm);
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
  Layout lo = layout_compute(b, s_num_font, s_date_font, s_slot_font, s_min_buf);

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  dial_draw(ctx, &lo, s_now);

  // Each row cuts its own footprint out of the dial before drawing, so a marker
  // that spikes this far inward stops at the text instead of crossing it.
  knock_out(ctx, text_plate(lo.num_box, s_num_font, s_min_buf));
  graphics_context_set_text_color(ctx, COL_ACCENT);
  graphics_draw_text(ctx, s_min_buf, s_num_font, lo.num_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  knock_out(ctx, text_plate(lo.date_box, s_date_font, s_date_buf));
  graphics_context_set_text_color(ctx, COL_INK);
  graphics_draw_text(ctx, s_date_buf, s_date_font, lo.date_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  slots_draw_top(ctx, &lo, s_slot_font);
  slots_draw_bottom(ctx, &lo, s_slot_font, s_now);

  dial_draw_now(ctx, &lo, &s_tm);   // last: nothing may cover the "now" wedge
}

// ---------------------------------------------------------------------------
// Service handlers
// ---------------------------------------------------------------------------

static void tick_handler(struct tm *t, TimeUnits units) {
  s_tm = *t;
  s_now = time(NULL);
  update_buffers();
  // Both slots and every marker are relative to now, so the minute tick is also
  // what advances the countdown and retires anything that has aged out. Nothing
  // on this face moves faster than a minute.
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
  events_upsert(1, now - 20 * 60,  90, EV_APPOINTMENT);  // running: solid band + count-up
  events_upsert(2, now + 150 * 60, 60, EV_APPOINTMENT);  // these two overlap and
  events_upsert(3, now + 180 * 60, 90, EV_APPOINTMENT);  // must flatten to one band
  events_upsert(4, now + 40 * 60,  45, EV_APPOINTMENT);  // inside 3 h, not 30 min
  events_upsert(5, now - 40 * 60,   0, EV_TASK);         // overdue: solid triangle
  events_upsert(6, now + 300 * 60,  0, EV_TASK);         // 6 min apart -> same notch
  events_upsert(7, now + 306 * 60,  0, EV_TASK);         // -> one deeper marker
  events_upsert(8, now + 200 * 60,  0, EV_TASK);         // sits on top of a band
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
  // Custom TTFs at the size this platform wants, falling back to system fonts if
  // the resources are missing.
  s_num_font = fonts_load_custom_font(resource_get_handle(RES_NUM_FONT));
  s_date_font = fonts_load_custom_font(resource_get_handle(RES_DATE_FONT));
  s_slot_font = fonts_load_custom_font(resource_get_handle(RES_SLOT_FONT));
  if (s_num_font && s_date_font && s_slot_font) {
    s_custom_fonts = true;
  } else {
    s_num_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    s_date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    s_slot_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  }

  s_now = time(NULL);
  s_tm = *localtime(&s_now);
  update_buffers();
  events_clear();
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
  wire_deinit();                 // before window_destroy: its timers repaint
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  if (s_custom_fonts) {
    fonts_unload_custom_font(s_num_font);
    fonts_unload_custom_font(s_date_font);
    fonts_unload_custom_font(s_slot_font);
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
