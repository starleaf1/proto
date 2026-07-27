#include <pebble.h>

// ---------------------------------------------------------------------------
// proto — "Eclipse" watchface
//
// Big minute numeral above center, a 60-tick dial hugging the watch edge, and a
// small radial hour hand whose tip sits a consistent 1px inside the dial. The
// hand sweeps like an analog clock's — at 4:30 it points midway between the 4
// and 5 marks. A battery gauge rides just above the numeral. Day/date/month on a
// lighter line below, and a bottom row of two companion-driven status icons
// (phone-call state / unread message). Sporty 2000s-futurism vibe: electric-blue
// highlight, techno-geometric Orbitron numeral, tach-style dial.
//
// The battery gauge is the only always-on indicator. Both status icons come from
// the phone, so when the companion link is down the whole row is hidden rather
// than showing counts the watch can no longer trust.
//
// The phone icon is the only thing here that moves faster than a minute: a live
// call flashes, faster while ringing than once answered. The companion decides the
// call state; the watch decides what each state looks like on this particular
// display, which is why no color ever crosses the wire.
//
// All geometry derives from the root layer's bounds so it adapts to every
// target platform (rectangular + round, B&W + color) with no hardcoded sizes.
// ---------------------------------------------------------------------------

#define EDGE_MARGIN 3      // dial inset from the physical screen edge (px)
// Flash half-periods in ms, i.e. how long one phase lasts. 125 ms per phase is a
// 250 ms cycle = 4 Hz; 250 ms per phase is 2 Hz. Ringing is the faster, more
// urgent of the two.
#define FLASH_RING_MS 250    // ringing — 2 Hz
#define FLASH_CALL_MS 500    // call in progress — 1 Hz (B&W only; color shows steady green)
#define FLASH_MAX_MS  120000 // give up flashing after this; a dead companion must not drain us

// Phone-call state, decided by the companion (see docs/protocol.md). The watch
// maps the enum to a look; it never receives a color.
enum { PHONE_IDLE = 0, PHONE_ONGOING, PHONE_RINGING, PHONE_MISSED };

static Window *s_window;
static Layer  *s_root_layer;
static GFont   s_num_font;
static GFont   s_date_font;
static bool    s_custom_fonts = false;   // true if TTFs loaded (so we unload them)

static struct tm         s_now;
static BatteryChargeState s_batt;
static bool               s_connected = true;
static int                s_unread = 0;   // fed by companion via AppMessage; 0 = unlit
static int                s_missed = 0;   // fed by companion; a count, not the icon's look
static int                s_phone = PHONE_IDLE;  // fed by companion; drives the phone icon
static bool               s_phone_seen = false;  // companion has spoken PhoneState at least once
static bool               s_focused = true;      // false while a modal covers the face
static AppTimer          *s_flash_timer = NULL;  // non-NULL only while flashing
static bool               s_flash_on = false;    // which half of the flash we're in
static uint16_t           s_flash_period = 0;    // current half-period in ms; 0 = not flashing
static uint32_t           s_flash_ms = 0;        // elapsed flash time, see FLASH_MAX_MS

static char s_min_buf[4];    // "05" / "30"
static char s_date_buf[24];  // "MON 22 JUL"

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

#ifdef PBL_ROUND
// Point at radius r, angle a (12 o'clock = up, clockwise). int32 math before
// the int16 cast avoids overflow (sin_lookup*r can reach ~7M).
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
  int16_t ey2 = (dy > 0) ? bot : top;   // else it exits a horizontal edge (dy != 0 here)
  int32_t ex2 = c.x + (int32_t)(ey2 - c.y) * dx / dy;
  return GPoint((int16_t)ex2, ey2);
}
#endif

// The dial boundary point at angle a: a circle on round displays, the
// rectangular perimeter otherwise.
static GPoint dial_boundary(GRect dial, GPoint c, int32_t a) {
#ifdef PBL_ROUND
  int32_t rad = (dial.size.w < dial.size.h ? dial.size.w : dial.size.h) / 2;
  return point_on_circle(c, rad, a);
#else
  return ray_rect_boundary(dial, c, a);
#endif
}

// Move point p inward (toward center) along the ray at angle a by d px.
static GPoint step_in(GPoint p, int32_t a, int32_t d) {
  return GPoint(p.x - (int16_t)(sin_lookup(a) * d / TRIG_MAX_RATIO),
                p.y + (int16_t)(cos_lookup(a) * d / TRIG_MAX_RATIO));
}

static void fill_triangle(GContext *ctx, GPoint a, GPoint b, GPoint c) {
  GPoint pts[3] = { a, b, c };
  GPathInfo info = { .num_points = 3, .points = pts };
  GPath *path = gpath_create(&info);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// ---------------------------------------------------------------------------
// Time / date strings
// ---------------------------------------------------------------------------

static void update_buffers(void) {
  // Big numeral = minutes, zero-padded ("05", "30"). Same in 12h/24h, so no
  // clock-style branching; the hour hand carries the hour instead.
  strftime(s_min_buf, sizeof(s_min_buf), "%M", &s_now);
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", &s_now);
  for (char *p = s_date_buf; *p; p++) {  // uppercase in place (matches font subset)
    if (*p >= 'a' && *p <= 'z') *p -= 32;
  }
}

// ---------------------------------------------------------------------------
// Call flash
// ---------------------------------------------------------------------------

// The phone icon is the only thing on this face that animates; nothing else moves
// faster than a minute. One self-re-arming one-shot timer drives it, at a rate that
// depends on the state — so the rate itself carries meaning. It runs only during a
// call, a bounded window, so the extra repaints never become an always-on cost.
//
// flash_sync() is the sole owner of the timer's lifetime: every handler that can
// change s_phone, s_connected or s_focused calls it, and nothing else touches the
// handle. It is also what re-arms at a new rate when the state changes mid-call
// (ringing -> answered), which a plain "already running, leave it" guard would miss.

static void flash_tick(void *data);

// Half-period for the current state, or 0 when the icon should be static. On color
// only ringing animates — a call in progress is steady green there, and it is the
// B&W platforms, with no color to spend, that use rate to tell the two apart.
static uint16_t flash_period(void) {
  if (!s_connected || !s_focused) return 0;
  if (s_phone == PHONE_RINGING) return FLASH_RING_MS;
#ifndef PBL_COLOR
  if (s_phone == PHONE_ONGOING) return FLASH_CALL_MS;
#endif
  return 0;
}

static void flash_stop(void) {
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);   // handle is dead the moment this returns
    s_flash_timer = NULL;
  }
  s_flash_period = 0;
  s_flash_on = false;
}

static void flash_tick(void *data) {
  s_flash_timer = NULL;                // an elapsed handle must never be cancelled
  if (s_flash_period) {
    s_flash_on = !s_flash_on;
    s_flash_ms += s_flash_period;
    // Self-terminating: give up after FLASH_MAX_MS so a companion that dies
    // mid-call can't drain the watch. Giving up leaves the icon on its lit phase —
    // visible, just no longer moving.
    if (s_flash_ms < FLASH_MAX_MS) {
      s_flash_timer = app_timer_register(s_flash_period, flash_tick, NULL);
    } else {
      s_flash_period = 0;
      s_flash_on = true;
    }
  }
  layer_mark_dirty(s_root_layer);
}

static void flash_sync(void) {
  uint16_t want = flash_period();
  if (want == s_flash_period) return;  // already at the right rate (or already off)
  flash_stop();
  if (want) {
    s_flash_period = want;
    s_flash_on = true;                 // enter lit so the change is seen immediately
    s_flash_ms = 0;
    s_flash_timer = app_timer_register(want, flash_tick, NULL);
  }
  layer_mark_dirty(s_root_layer);
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

// Knock an already-drawn icon back to a barely-visible ghost. Pebble has no
// true alpha for fills/lines and only four grays, so "fainter than light gray"
// means dithering: erase most of the icon's pixels to white in a regular grid,
// leaving a sparse stipple. Works the same on color and B&W. Call after drawing
// the icon, over a rect that covers it (padding into white background is a
// no-op, so the rect can be generous).
static void fade_icon(GContext *ctx, GRect r) {
  graphics_context_set_stroke_color(ctx, GColorWhite);
  for (int16_t y = r.origin.y; y < r.origin.y + r.size.h; y++) {
    for (int16_t x = r.origin.x; x < r.origin.x + r.size.w; x++) {
      if ((x & 1) && (y & 1)) continue;         // keep 1 px per 2x2 block (~25%)
      graphics_draw_pixel(ctx, GPoint(x, y));
    }
  }
}

static void draw_battery(GContext *ctx, GPoint cp, int16_t hw, int16_t hh) {
  GRect body = GRect(cp.x - hw, cp.y - hh, 2 * hw, 2 * hh);
  // outline + terminal nub (always black)
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, body);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(body.origin.x + body.size.w, cp.y - hh / 2, 2, hh), 0, GCornerNone);
  // proportional fill, traffic-light color on color / black on B&W
  int16_t inner_w = body.size.w - 4;
  int16_t fill_w  = inner_w * s_batt.charge_percent / 100;
  GColor bc;
#ifdef PBL_COLOR
  bc = s_batt.charge_percent > 50 ? GColorGreen
     : s_batt.charge_percent > 20 ? GColorYellow
     : GColorRed;
#else
  bc = GColorBlack;
#endif
  if (fill_w > 0) {
    graphics_context_set_fill_color(ctx, bc);
    graphics_fill_rect(ctx, GRect(body.origin.x + 2, body.origin.y + 2, fill_w, body.size.h - 4), 0, GCornerNone);
  }
}

// Phone-call indicator — the Material Design "call" handset, traced as a
// filled polygon: earpiece top-left, mouthpiece bottom-right, joined by a
// diagonal grip with a concave inner edge. The companion owns the state, the
// watch owns the look. Color: green in a call, green/amber flash while ringing,
// red for a missed call, ghost when idle. B&W has no color to spend, so **rate**
// carries the state there instead: ghost = idle, solid and steady = missed call,
// flashing at 2 Hz = call in progress, flashing at 4 Hz = ringing. Both flashes
// swing all the way to the ghost, because with rate doing the discriminating the
// only thing the swing has to do is be unmistakably visible.
static void draw_phone(GContext *ctx, GPoint cp, int16_t hw, int16_t hh) {
  GColor col;
  bool   ghost = false;                    // fade the icon back after drawing it
#ifdef PBL_COLOR
  switch (s_phone) {
    // GColorGreen/GColorYellow are near-invisible on white (1.4:1 and 1.1:1) —
    // fine for the battery gauge, which sits inside a black outline, but this
    // gpath has no outline to give it edges. Use the darker pair.
    case PHONE_ONGOING: col = GColorIslamicGreen; break;                // call in progress
    case PHONE_RINGING: col = s_flash_on ? GColorIslamicGreen
                                         : GColorChromeYellow; break;   // alternates
    case PHONE_MISSED:  col = GColorRed; break;                         // missed-call alert
    default:            col = GColorLightGray; ghost = true; break;     // idle
  }
#else
  col = GColorBlack;                       // no color: rate carries the state
  switch (s_phone) {
    case PHONE_ONGOING:                                      // 2 Hz, see flash_period
    case PHONE_RINGING: ghost = !s_flash_on; break;          // 4 Hz
    case PHONE_MISSED:  break;                               // solid and steady
    default:            ghost = true; break;                 // idle
  }
#endif
  // Outline of Material's `call` glyph in a grid centred on the icon and spanning
  // ~±36; scaled to the box at runtime (denominator < 48 makes it fill more).
  static const int8_t PTS[][2] = {
    {-22,-5}, {-13, 6}, {-5,15}, { 5,22}, {14,13}, {18,12}, {25,12}, {32,14},
    {36,18}, {36,32}, {32,36}, { 6,31}, {-16,16}, {-31,-6}, {-36,-32}, {-32,-36},
    {-18,-36}, {-14,-32}, {-12,-18}, {-13,-14}
  };
  const int16_t D = 42;
  GPoint pts[sizeof(PTS) / sizeof(PTS[0])];
  for (unsigned i = 0; i < sizeof(PTS) / sizeof(PTS[0]); i++) {
    pts[i] = GPoint(cp.x + (int16_t)((int32_t)PTS[i][0] * hw / D),
                    cp.y + (int16_t)((int32_t)PTS[i][1] * hh / D));
  }
  GPathInfo info = { .num_points = sizeof(PTS) / sizeof(PTS[0]), .points = pts };
  GPath *path = gpath_create(&info);
  graphics_context_set_fill_color(ctx, col);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);

  if (ghost) {
    fade_icon(ctx, GRect(cp.x - hw - 2, cp.y - hh - 2, 2 * (hw + 2), 2 * (hh + 2)));
  }
}

static void draw_envelope(GContext *ctx, GPoint cp, int16_t hw, int16_t hh) {
  bool lit = s_unread > 0;
  GRect body = GRect(cp.x - hw, cp.y - hh, 2 * hw, 2 * hh);
  GPoint tl = GPoint(body.origin.x, body.origin.y);
  GPoint tr = GPoint(body.origin.x + body.size.w, body.origin.y);
  GPoint mid = GPoint(cp.x, cp.y);
  graphics_context_set_stroke_width(ctx, 1);
  if (lit) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, body, 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorWhite);   // flap reads against solid body
  } else {
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorLightGray);  // translucent look
    graphics_fill_rect(ctx, body, 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorWhite);
#else
    graphics_context_set_stroke_color(ctx, GColorBlack);    // outline only on B&W
    graphics_draw_rect(ctx, body);
#endif
  }
  // Flap — a touch bolder than the body, scaling up on higher-res displays.
  int16_t fw = hw / 4;
  if (fw < 1) fw = 1;
  graphics_context_set_stroke_width(ctx, fw);
  graphics_draw_line(ctx, tl, mid);
  graphics_draw_line(ctx, tr, mid);
  if (!lit) {                                  // barely-visible when no unread
    fade_icon(ctx, GRect(body.origin.x - fw, body.origin.y - fw,
                          body.size.w + 2 * fw, body.size.h + 2 * fw));
  }
}

// ---------------------------------------------------------------------------
// Root render
// ---------------------------------------------------------------------------

static void root_update_proc(Layer *layer, GContext *ctx) {
  GRect  b = layer_get_bounds(layer);
  GPoint c = GPoint(b.origin.x + b.size.w / 2, b.origin.y + b.size.h / 2);
  int32_t R = (b.size.w < b.size.h ? b.size.w : b.size.h) / 2;
  GColor hi = PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack);

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  GRect   dial = grect_inset(b, GEdgeInsets(EDGE_MARGIN));
  int32_t tick_len = R / 9;
  if (tick_len < 6) tick_len = 6;

  // 1. Dial: 60 radial ticks; multiples of 5 are thicker, and every marker
  // except the four quarters (00/15/30/45) runs 25% shorter.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i < 60; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 60;
    int32_t len = (i % 15 == 0) ? tick_len : (tick_len * 75) / 100;
    GPoint outer = dial_boundary(dial, c, a);
    GPoint inner = step_in(outer, a, len);
    graphics_context_set_stroke_width(ctx, (i % 5 == 0) ? 3 : 1);
    graphics_draw_line(ctx, outer, inner);
  }

  // 2. Minute numeral — highlight color, centered, slightly above middle.
  GRect  m = GRect(0, 0, b.size.w, b.size.h);
  GSize  ns = graphics_text_layout_get_content_size(s_min_buf, s_num_font, m,
                                                     GTextOverflowModeFill, GTextAlignmentCenter);
  int16_t num_cy = c.y - R / 6;
  GRect  num_box = GRect(0, num_cy - ns.h / 2, b.size.w, ns.h + 6);
  graphics_context_set_text_color(ctx, hi);
  graphics_draw_text(ctx, s_min_buf, s_num_font, num_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // 2b. Battery gauge — centered just above the minute numeral. The -1 x nudge
  // re-centers the icon against its 2px terminal nub.
  int16_t batt_hw = R / 7, batt_hh = R / 16;
  int16_t batt_cy = num_box.origin.y - batt_hh - 3;
  draw_battery(ctx, GPoint(c.x - 1, batt_cy), batt_hw, batt_hh);

  // 3. Hour hand — radial arrow, apex 1px inside the tick inner-ends. Sweeps
  // like an analog clock: the 12-hour mark plus the fraction of the hour already
  // elapsed, so at 4:30 the tip sits midway between the 4 and 5 marks.
  int32_t a_hour = TRIG_MAX_ANGLE * ((s_now.tm_hour % 12) * 60 + s_now.tm_min) / (12 * 60);
  int32_t plen = (R * 2) / 9;   // pointer length toward center
  int32_t phw  = R / 12;        // half-width at the base
  if (phw < 5) phw = 5;
  GPoint apex = step_in(dial_boundary(dial, c, a_hour), a_hour, tick_len + 1);
  GPoint base = step_in(apex, a_hour, plen);
  int32_t perp_x = cos_lookup(a_hour);   // perpendicular to the ray
  int32_t perp_y = sin_lookup(a_hour);
  GPoint b1 = GPoint(base.x + (int16_t)(perp_x * phw / TRIG_MAX_RATIO),
                     base.y + (int16_t)(perp_y * phw / TRIG_MAX_RATIO));
  GPoint b2 = GPoint(base.x - (int16_t)(perp_x * phw / TRIG_MAX_RATIO),
                     base.y - (int16_t)(perp_y * phw / TRIG_MAX_RATIO));
  graphics_context_set_fill_color(ctx, hi);
  fill_triangle(ctx, apex, b1, b2);

  // 4. Date line — lighter font, black, under the numeral.
  int16_t date_y = num_box.origin.y + ns.h - 2;
  GRect  date_box = GRect(0, date_y, b.size.w, 26);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_date_buf, s_date_font, date_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // 5. Icon row — phone call + envelope, laid out by their visual extents with
  // a uniform gap and centered as a pair on c.x. Both states come from the
  // companion over AppMessage, so a dropped link means the watch has no current
  // value for either: hide the row entirely rather than show a stale count. The
  // battery gauge above stays put — it is the one indicator the watch owns.
  if (s_connected) {
    int16_t icon_cy = c.y + (R * 3) / 5;
    int16_t mc_hw = R / 8, env_hw = R / 8;
    int16_t gap = R / 6;
    int16_t total = 2 * mc_hw + gap + 2 * env_hw;
    int16_t x = c.x - total / 2;                     // left edge of the centered row
    int16_t mc_cx  = x + mc_hw;   x += 2 * mc_hw + gap;
    int16_t env_cx = x + env_hw;
    draw_phone(ctx, GPoint(mc_cx,  icon_cy), mc_hw,  R / 8);
    draw_envelope(ctx,    GPoint(env_cx, icon_cy), env_hw, R / 12);
  }
}

// ---------------------------------------------------------------------------
// Service handlers
// ---------------------------------------------------------------------------

static void tick_handler(struct tm *t, TimeUnits units) {
  s_now = *t;
  update_buffers();
  layer_mark_dirty(s_root_layer);
}

static void battery_handler(BatteryChargeState state) {
  s_batt = state;
  layer_mark_dirty(s_root_layer);
}

static void conn_handler(bool connected) {
  s_connected = connected;
  // Drop the call state with the link: otherwise a blip during a ring leaves us
  // flashing a phantom the moment it returns. The counts survive — clearing them
  // would blank the envelope on every blip, which the row's own gate handles.
  if (!connected) s_phone = PHONE_IDLE;
  flash_sync();                            // no link, no flash
  layer_mark_dirty(s_root_layer);
}

// A modal (notification, quick view) covering the face makes the icon row
// invisible; there is no point repainting it twice a second underneath.
static void focus_handler(bool in_focus) {
  s_focused = in_focus;
  flash_sync();
}

static void inbox_received(DictionaryIterator *iter, void *ctx) {
  bool dirty = false;
  Tuple *u = dict_find(iter, MESSAGE_KEY_UnreadCount);
  if (u) { s_unread = u->value->int32; dirty = true; }   // drives the envelope
  Tuple *m = dict_find(iter, MESSAGE_KEY_MissedCount);
  if (m) { s_missed = m->value->int32; dirty = true; }   // a count; see PhoneState
  Tuple *p = dict_find(iter, MESSAGE_KEY_PhoneState);
  if (p) {                                 // authoritative once ever sent
    int32_t v = p->value->int32;
    s_phone = (v >= PHONE_IDLE && v <= PHONE_MISSED) ? (int)v : PHONE_IDLE;   // clamp
    s_phone_seen = true;
    dirty = true;
  } else if (m && !s_phone_seen) {         // pre-PhoneState companion: count drives the icon
    s_phone = s_missed > 0 ? PHONE_MISSED : PHONE_IDLE;
  }
  if (dirty) {
    flash_sync();                          // one decision point for the timer
    layer_mark_dirty(s_root_layer);
  }
}

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
}

static void init(void) {
  // Fonts: custom TTFs, falling back to system fonts if resources are absent.
  s_num_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ORBITRON_43));
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_RAJDHANI_LIGHT_22));
  if (s_num_font && s_date_font) {
    s_custom_fonts = true;
  } else {
    s_num_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    s_date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  }

  // Seed state before first paint.
  time_t now = time(NULL);
  s_now = *localtime(&now);
  update_buffers();
  s_batt = battery_state_service_peek();
  s_connected = connection_service_peek_pebble_app_connection();

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
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
  app_focus_service_subscribe(focus_handler);   // pause the flash under a modal

  app_message_register_inbox_received(inbox_received);
  app_message_open(64, 64);
}

static void deinit(void) {
  flash_stop();                  // before window_destroy — the callback holds s_root_layer
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  app_focus_service_unsubscribe();
  if (s_custom_fonts) {
    fonts_unload_custom_font(s_num_font);
    fonts_unload_custom_font(s_date_font);
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
