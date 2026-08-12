#include <pebble.h>
#include "events.h"
#include "geometry.h"
#include "slots.h"
#include "strip.h"
#include "theme.h"
#include "wbatt.h"
#include "wire.h"
#include <string.h>

// ---------------------------------------------------------------------------
// proto — a calendar-driven watchface.
//
// The left edge is a four-hour timeline, read downward: one hour behind, three
// ahead, and a pointer at the quarter mark that never moves. Appointments are
// bands spanning their duration with the quarter-hour notches cut through them;
// tasks and reminders are wedges poking inward off the ruler. See strip.c.
//
// The clock is plain digits, centred on the pointer — the strip says where in
// the day you are, so the clock only has to say what time it is. The date, the
// countdown, the next turn and whatever is running out stack beneath it. See
// slots.c.
//
// The watch computes the time, the date and its own battery. Everything else —
// the calendar, the phone's battery, the next turn — comes from the companion,
// and the bottom row says so when the companion stops answering. Uncertainty is
// something this face states rather than something it silently omits.
//
// All geometry derives from the root layer's bounds, so the same code lays out
// flint (144x168, one ink), emery (200x228) and gabbro (260x260, round, where
// the strip follows the left arc instead of the left edge).
// ---------------------------------------------------------------------------

static Window *s_window;
static Layer  *s_root_layer;
static GFont   s_num_font;
static GFont   s_date_font;
static GFont   s_slot_font;
static bool    s_custom_fonts = false;   // true if the TTFs loaded, so we unload

static struct tm s_tm;
static time_t    s_now = 0;

static char s_time_buf[8];   // "14:32" / "9:05"
static char s_date_buf[16];  // "MON 22" — see the width note in geometry.c

static void update_buffers(void) {
  // The clock is the hour and the minute both, which the dial version could not
  // be: there, the hour was the hand's angle and the numeral had only the minute
  // to show. A stationary pointer cannot carry an hour, so the digits do.
  //
  // No AM/PM. There is no room for it beside the strip at any readable size, and
  // the strip is already showing four hours of context around now — which is a
  // better answer to "morning or evening?" than two letters.
  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof s_time_buf, "%H:%M", &s_tm);
  } else {
    strftime(s_time_buf, sizeof s_time_buf, "%I:%M", &s_tm);
    // "01:05" is not how anyone reads a 12-hour clock. The row is sized from
    // "00:00" and the digits are centred in it, so dropping the zero shifts them
    // half a glyph once a day at ten o'clock and never otherwise.
    if (s_time_buf[0] == '0') memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf));
  }
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
  Layout lo = layout_compute(b, s_num_font, s_date_font, s_slot_font);

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  strip_draw(ctx, &lo, s_now);

  // Each row cuts its own footprint out of the strip before drawing, so a marker
  // that spikes this far inward stops at the text instead of crossing it.
  knock_out(ctx, text_plate(lo.num_box, s_num_font, s_time_buf));
  graphics_context_set_text_color(ctx, COL_ACCENT);
  graphics_draw_text(ctx, s_time_buf, s_num_font, lo.num_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  knock_out(ctx, text_plate(lo.date_box, s_date_font, s_date_buf));
  graphics_context_set_text_color(ctx, COL_INK);
  graphics_draw_text(ctx, s_date_buf, s_date_font, lo.date_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  slots_draw_count(ctx, &lo, s_slot_font, s_now);
  slots_draw_nav(ctx, &lo, s_slot_font);
  slots_draw_warn(ctx, &lo, s_slot_font);

  strip_draw_now(ctx, &lo);   // last: nothing may cover the "now" pointer
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
  events_upsert(1, now - 20 * 60,  90, EV_APPOINTMENT);  // running: deep band + count-up
  events_upsert(2, now + 100 * 60, 40, EV_APPOINTMENT);  // these two overlap and
  events_upsert(3, now + 120 * 60, 45, EV_APPOINTMENT);  // must flatten to one band
  events_upsert(4, now + 40 * 60,  45, EV_APPOINTMENT);  // inside 3 h, not 30 min
  events_upsert(5, now - 40 * 60,   0, EV_TASK);         // overdue: solid wedge
  events_upsert(6, now + 160 * 60,  0, EV_TASK);         // 4 min apart -> too close
  events_upsert(7, now + 164 * 60,  0, EV_TASK);         // -> one deeper marker
  events_upsert(8, now + 110 * 60,  0, EV_TASK);         // sits on top of a band
  events_upsert(9, now + 200 * 60,  0, EV_TASK);         // past the horizon: must not draw
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
