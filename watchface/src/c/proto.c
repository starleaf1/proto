#include <pebble.h>

// ---------------------------------------------------------------------------
// proto — "Eclipse" watchface
//
// Big hour numeral above center, a 60-tick dial hugging the watch edge, and a
// small radial minute pointer whose tip sits a consistent 1px inside the dial.
// A battery gauge rides just above the hour. Day/date/month on a lighter line
// below, and a bottom row of three status icons (missed call / unread message /
// bluetooth). Sporty 2000s-futurism vibe: electric-blue highlight,
// techno-geometric Orbitron hour, tach-style dial.
//
// All geometry derives from the root layer's bounds so it adapts to every
// target platform (rectangular + round, B&W + color) with no hardcoded sizes.
// ---------------------------------------------------------------------------

#define EDGE_MARGIN 3   // dial inset from the physical screen edge (px)

static Window *s_window;
static Layer  *s_root_layer;
static GFont   s_hour_font;
static GFont   s_date_font;
static bool    s_custom_fonts = false;   // true if TTFs loaded (so we unload them)

static struct tm         s_now;
static BatteryChargeState s_batt;
static bool               s_connected = true;
static int                s_unread = 0;   // fed by companion via AppMessage; 0 = unlit
static int                s_missed = 0;   // fed by companion via AppMessage; 0 = unlit

static char s_hour_buf[4];   // "23" / "9"
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
  if (clock_is_24h_style()) {
    strftime(s_hour_buf, sizeof(s_hour_buf), "%H", &s_now);
  } else {
    strftime(s_hour_buf, sizeof(s_hour_buf), "%I", &s_now);
  }
  if (s_hour_buf[0] == '0') {            // strip leading zero on the big numeral
    s_hour_buf[0] = s_hour_buf[1];
    s_hour_buf[1] = s_hour_buf[2];
  }
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", &s_now);
  for (char *p = s_date_buf; *p; p++) {  // uppercase in place (matches font subset)
    if (*p >= 'a' && *p <= 'z') *p -= 32;
  }
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

// Missed-call indicator — the Material Design "call" handset, traced as a
// filled polygon: earpiece top-left, mouthpiece bottom-right, joined by a
// diagonal grip with a concave inner edge. Lit (a call was missed) fills bold
// red on color / black on B&W; unlit fades to a ghost like the other icons.
static void draw_missed_call(GContext *ctx, GPoint cp, int16_t hw, int16_t hh) {
  bool lit = s_missed > 0;
  GColor col;
#ifdef PBL_COLOR
  col = lit ? GColorRed : GColorLightGray;   // red = missed-call alert
#else
  col = GColorBlack;
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

  if (!lit) {                                // barely-visible when nothing missed
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

static void draw_bluetooth(GContext *ctx, GPoint cp, int16_t hw, int16_t hh) {
  bool lit = !s_connected;
  GColor col;
  uint8_t sw = hh / 5;         // stroke scales with the icon (thicker on hi-res)
  if (sw < 1) sw = 1;
  if (lit) sw += 1;            // disconnected reads bolder
#ifdef PBL_COLOR
  col = lit ? GColorRed : GColorLightGray;   // red = "warning" when disconnected
#else
  col = GColorBlack;
#endif
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, sw);
  // Bluetooth rune. Wide right triangles + shallow left notches, whose inner
  // diagonals cross just right of the spine — the classic bluetooth look.
  // (Equal-depth tips render as a stacked "8"; a separate spine reads as an
  // asterisk — so neither.)
  int16_t q  = hh / 2;
  int16_t lw = hw / 2;                       // left notches shallower than right knees
  GPoint A  = GPoint(cp.x, cp.y - hh);       // top
  GPoint B  = GPoint(cp.x, cp.y + hh);       // bottom
  GPoint P  = GPoint(cp.x + hw, cp.y - q);   // upper-right knee
  GPoint Q  = GPoint(cp.x + hw, cp.y + q);   // lower-right knee
  GPoint Lp = GPoint(cp.x - lw, cp.y - q);   // upper-left notch
  GPoint Lq = GPoint(cp.x - lw, cp.y + q);   // lower-left notch
  graphics_draw_line(ctx, A, B);   // spine
  graphics_draw_line(ctx, A, P);
  graphics_draw_line(ctx, P, Lq);
  graphics_draw_line(ctx, B, Q);
  graphics_draw_line(ctx, Q, Lp);

  // Exclamation mark right of the rune — always present (root_update_proc
  // reserves its slot so the row stays centered). Warning color + bold when
  // disconnected; faded along with the rune when connected.
  int16_t ex_cx  = cp.x + hw + hw / 2;        // sits in the reserved slot
  int16_t bw     = sw + 1;                    // stem a touch bolder than the rune
  int16_t stem_h = (hh * 6) / 5;              // stem from the top, ~60% of full height
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, GRect(ex_cx - bw / 2, cp.y - hh, bw, stem_h), 0, GCornerNone);
  graphics_fill_circle(ctx, GPoint(ex_cx, cp.y + hh - bw), bw / 2 + 1);

  if (!lit) {                                 // barely-visible when connected
    fade_icon(ctx, GRect(cp.x - lw - 1, cp.y - hh - 1, 2 * hw + 4, 2 * hh + 2));
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

  // 1. Dial: 60 radial ticks, uniform length; multiples of 5 are thicker.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i < 60; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 60;
    GPoint outer = dial_boundary(dial, c, a);
    GPoint inner = step_in(outer, a, tick_len);
    graphics_context_set_stroke_width(ctx, (i % 5 == 0) ? 3 : 1);
    graphics_draw_line(ctx, outer, inner);
  }

  // 2. Hour numeral — highlight color, centered, slightly above middle.
  GRect  m = GRect(0, 0, b.size.w, b.size.h);
  GSize  hs = graphics_text_layout_get_content_size(s_hour_buf, s_hour_font, m,
                                                     GTextOverflowModeFill, GTextAlignmentCenter);
  int16_t hour_cy = c.y - R / 6;
  GRect  hour_box = GRect(0, hour_cy - hs.h / 2, b.size.w, hs.h + 6);
  graphics_context_set_text_color(ctx, hi);
  graphics_draw_text(ctx, s_hour_buf, s_hour_font, hour_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // 2b. Battery gauge — centered just above the hour numeral. The -1 x nudge
  // re-centers the icon against its 2px terminal nub.
  int16_t batt_hw = R / 7, batt_hh = R / 16;
  int16_t batt_cy = hour_box.origin.y - batt_hh - 3;
  draw_battery(ctx, GPoint(c.x - 1, batt_cy), batt_hw, batt_hh);

  // 3. Minute pointer — radial arrow, apex 1px inside the tick inner-ends.
  int32_t a_min = TRIG_MAX_ANGLE * s_now.tm_min / 60;
  int32_t plen = (R * 2) / 9;   // pointer length toward center
  int32_t phw  = R / 12;        // half-width at the base
  if (phw < 5) phw = 5;
  GPoint apex = step_in(dial_boundary(dial, c, a_min), a_min, tick_len + 1);
  GPoint base = step_in(apex, a_min, plen);
  int32_t perp_x = cos_lookup(a_min);   // perpendicular to the ray
  int32_t perp_y = sin_lookup(a_min);
  GPoint b1 = GPoint(base.x + (int16_t)(perp_x * phw / TRIG_MAX_RATIO),
                     base.y + (int16_t)(perp_y * phw / TRIG_MAX_RATIO));
  GPoint b2 = GPoint(base.x - (int16_t)(perp_x * phw / TRIG_MAX_RATIO),
                     base.y - (int16_t)(perp_y * phw / TRIG_MAX_RATIO));
  graphics_context_set_fill_color(ctx, hi);
  fill_triangle(ctx, apex, b1, b2);

  // 4. Date line — lighter font, black, under the hour.
  int16_t date_y = hour_box.origin.y + hs.h - 2;
  GRect  date_box = GRect(0, date_y, b.size.w, 26);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_date_buf, s_date_font, date_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // 5. Icon row — missed call / envelope / bluetooth, always all rendered.
  // Each glyph has a different width (and bluetooth is drawn off-center from
  // its own point), so equal center spacing leaves bluetooth adrift. Instead
  // lay them out by their true left/right visual extents with a uniform gap,
  // then center the whole group's span on c.x.
  int16_t icon_cy = c.y + (R * 3) / 5;
  int16_t mc_hw = R / 8,   env_hw = R / 8,  bt_hw = R / 12;
  int16_t mc_l  = mc_hw,   mc_r  = mc_hw;
  int16_t env_l = env_hw,  env_r = env_hw;
  int16_t bt_l  = bt_hw / 2, bt_r = bt_hw + bt_hw;  // rune + always-present "!" to its right
  int16_t gap = R / 6;
  int16_t total = mc_l + mc_r + gap + env_l + env_r + gap + bt_l + bt_r;
  int16_t x = c.x - total / 2;                       // left edge of the centered row
  int16_t mc_cx  = x + mc_l;   x += mc_l + mc_r + gap;
  int16_t env_cx = x + env_l;  x += env_l + env_r + gap;
  int16_t bt_cx  = x + bt_l;
  draw_missed_call(ctx, GPoint(mc_cx,  icon_cy), mc_hw,  R / 8);
  draw_envelope(ctx,    GPoint(env_cx, icon_cy), env_hw, R / 12);
  draw_bluetooth(ctx,   GPoint(bt_cx,  icon_cy), bt_hw,  R / 8);
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
  layer_mark_dirty(s_root_layer);
}

static void inbox_received(DictionaryIterator *iter, void *ctx) {
  bool dirty = false;
  Tuple *u = dict_find(iter, MESSAGE_KEY_UnreadCount);
  if (u) { s_unread = u->value->int32; dirty = true; }   // drives the envelope
  Tuple *m = dict_find(iter, MESSAGE_KEY_MissedCount);
  if (m) { s_missed = m->value->int32; dirty = true; }   // drives the missed-call icon
  if (dirty) layer_mark_dirty(s_root_layer);
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
  s_hour_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ORBITRON_54));
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_RAJDHANI_LIGHT_22));
  if (s_hour_font && s_date_font) {
    s_custom_fonts = true;
  } else {
    s_hour_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
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

  app_message_register_inbox_received(inbox_received);
  app_message_open(64, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  if (s_custom_fonts) {
    fonts_unload_custom_font(s_hour_font);
    fonts_unload_custom_font(s_date_font);
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
