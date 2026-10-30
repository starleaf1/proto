#include "icons.h"
#include "theme.h"
#include "wire.h"
#include <string.h>

// Loaded on first use and kept, a handful at a time: the rows show at most three
// glyphs at once, and an icon that has dropped out of the cache costs a resource load
// on the next paint and nothing else. Round-robin rather than least-recently-used,
// because at this size the bookkeeping would outweigh the misses.
#define CACHE_N 6

typedef struct {
  uint32_t res;
  bool mirror;
  GBitmap *bmp;
} Cached;

static Cached s_cache[CACHE_N];
static int s_next;

// One bit per pixel, row-major, set where the digit has ink.
static uint16_t s_digit[10];
static bool s_digit_loaded[10];

// ---------------------------------------------------------------------------
// Pixel access
//
// The SDK can draw a bitmap but not flip one, and it has no scaled draw either, so
// both the mirror and the digits read the pixels themselves. Palettised formats pack
// the leftmost pixel into the most significant bits of a byte; the one unpalettised
// one-bit format, a framebuffer's, packs it into the least.
// ---------------------------------------------------------------------------

static int bpp_of(GBitmapFormat f) {
  switch (f) {
    case GBitmapFormat1Bit:
    case GBitmapFormat1BitPalette: return 1;
    case GBitmapFormat2BitPalette: return 2;
    case GBitmapFormat4BitPalette: return 4;
    case GBitmapFormat8Bit:        return 8;
    default:                       return 0;   // a circular framebuffer, never a resource
  }
}

static int px_shift(GBitmapFormat f, int bpp, int x) {
  int bit = x * bpp;
  return f == GBitmapFormat1Bit ? (bit & 7) : 8 - bpp - (bit & 7);
}

static int px_get(const uint8_t *row, GBitmapFormat f, int bpp, int x) {
  return (row[(x * bpp) >> 3] >> px_shift(f, bpp, x)) & ((1 << bpp) - 1);
}

static void px_set(uint8_t *row, GBitmapFormat f, int bpp, int x, int v) {
  int s = px_shift(f, bpp, x);
  uint8_t mask = (uint8_t)(((1 << bpp) - 1) << s);
  uint8_t *b = &row[(x * bpp) >> 3];
  *b = (uint8_t)((*b & ~mask) | ((v << s) & mask));
}

// Whether pixel (x, y) has any ink in it: opaque in the palette, or for the one
// format with no alpha at all, black.
static bool px_inked(GBitmap *b, int x, int y) {
  GBitmapFormat f = gbitmap_get_format(b);
  int bpp = bpp_of(f);
  if (!bpp) return false;
  const uint8_t *row = gbitmap_get_data(b) + y * gbitmap_get_bytes_per_row(b);
  int v = px_get(row, f, bpp, x);
  if (f == GBitmapFormat1Bit) return v == 0;
  if (f == GBitmapFormat8Bit) return (v >> 6) != 0;
  GColor *pal = gbitmap_get_palette(b);
  return pal && pal[v].a != 0;
}

// A copy of `src` flipped left for right, in the same format and palette, or NULL if
// it could not be made. Never the unflipped original instead: an arrow pointing the
// wrong way is worse than no arrow.
static GBitmap *mirror_of(GBitmap *src) {
  GBitmapFormat f = gbitmap_get_format(src);
  int bpp = bpp_of(f);
  if (!bpp) return NULL;
  GSize sz = gbitmap_get_bounds(src).size;

  GBitmap *dst;
  GColor *pal = gbitmap_get_palette(src);
  if (pal) {
    size_t n = (size_t)(1 << bpp) * sizeof(GColor);
    GColor *copy = malloc(n);
    if (!copy) return NULL;
    memcpy(copy, pal, n);
    dst = gbitmap_create_blank_with_palette(sz, f, copy, true);
    if (!dst) {
      free(copy);
      return NULL;
    }
  } else {
    dst = gbitmap_create_blank(sz, f);
    if (!dst) return NULL;
  }

  const uint8_t *sd = gbitmap_get_data(src);
  uint8_t *dd = gbitmap_get_data(dst);
  uint16_t ss = gbitmap_get_bytes_per_row(src);
  uint16_t ds = gbitmap_get_bytes_per_row(dst);
  for (int y = 0; y < sz.h; y++) {
    for (int x = 0; x < sz.w; x++) {
      px_set(dd + y * ds, f, bpp, sz.w - 1 - x, px_get(sd + y * ss, f, bpp, x));
    }
  }
  return dst;
}

// ---------------------------------------------------------------------------
// The cache
// ---------------------------------------------------------------------------

static GBitmap *icon_get(uint32_t res, bool mirror) {
  for (int i = 0; i < CACHE_N; i++) {
    if (s_cache[i].bmp && s_cache[i].res == res && s_cache[i].mirror == mirror) {
      return s_cache[i].bmp;
    }
  }
  GBitmap *b = gbitmap_create_with_resource(res);
  if (b && mirror) {
    GBitmap *m = mirror_of(b);
    gbitmap_destroy(b);
    b = m;
  }
  if (!b) return NULL;

  Cached *c = &s_cache[s_next];
  s_next = (s_next + 1) % CACHE_N;
  if (c->bmp) gbitmap_destroy(c->bmp);
  *c = (Cached){ .res = res, .mirror = mirror, .bmp = b };
  return b;
}

void icon_draw(GContext *ctx, GRect box, uint32_t res, bool mirror) {
  GBitmap *b = icon_get(res, mirror);
  if (!b) return;
  GSize sz = gbitmap_get_bounds(b).size;
  GPoint c = grect_center_point(&box);
  // GCompOpSet honours the icons' transparency, so whatever is under them shows
  // through where they are empty.
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, b, GRect(c.x - sz.w / 2, c.y - sz.h / 2, sz.w, sz.h));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

// ---------------------------------------------------------------------------
// The exit number
// ---------------------------------------------------------------------------

static const uint32_t DIGIT_RES[10] = {
  0,
  RESOURCE_ID_ICON_DIGIT_1, RESOURCE_ID_ICON_DIGIT_2, RESOURCE_ID_ICON_DIGIT_3,
  RESOURCE_ID_ICON_DIGIT_4, RESOURCE_ID_ICON_DIGIT_5, RESOURCE_ID_ICON_DIGIT_6,
  RESOURCE_ID_ICON_DIGIT_7, RESOURCE_ID_ICON_DIGIT_8, RESOURCE_ID_ICON_DIGIT_9,
};

// Read once into a mask and the bitmap let go: fifteen bits is all a digit is, and a
// mask can be drawn at any whole scale and in any ink, which a bitmap cannot.
static uint16_t digit_mask(int d) {
  if (s_digit_loaded[d]) return s_digit[d];
  uint16_t m = 0;
  GBitmap *b = gbitmap_create_with_resource(DIGIT_RES[d]);
  if (b) {
    GSize sz = gbitmap_get_bounds(b).size;
    for (int y = 0; y < ICON_DIGIT_H && y < sz.h; y++) {
      for (int x = 0; x < ICON_DIGIT_W && x < sz.w; x++) {
        if (px_inked(b, x, y)) m |= (uint16_t)(1 << (y * ICON_DIGIT_W + x));
      }
    }
    gbitmap_destroy(b);
  }
  s_digit[d] = m;
  s_digit_loaded[d] = true;
  return m;
}

// Centred on `c`, which is the icon's own centre: the artwork leaves the middle of
// the island clear for it. One digit only — a two-digit exit would not fit inside an
// island at these sizes, and the shape still says which way to go without it.
static void draw_digit(GContext *ctx, GPoint c, int d, GColor ink) {
  if (d < 1 || d > 9) return;
  uint16_t m = digit_mask(d);
  int k = ICON_DIGIT_SCALE;
  int x0 = c.x - ICON_DIGIT_W * k / 2;
  int y0 = c.y - ICON_DIGIT_H * k / 2;
  graphics_context_set_fill_color(ctx, ink);
  for (int y = 0; y < ICON_DIGIT_H; y++) {
    for (int x = 0; x < ICON_DIGIT_W; x++) {
      if (m & (1 << (y * ICON_DIGIT_W + x))) {
        graphics_fill_rect(ctx, GRect(x0 + x * k, y0 + y * k, k, k), 0, GCornerNone);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// The maneuver
// ---------------------------------------------------------------------------

static uint32_t roundabout_res(int rb) {
  switch (rb) {
    case NAV_RB_EARLY:  return RESOURCE_ID_ICON_NAV_RB_EARLY;
    case NAV_RB_MIDDLE: return RESOURCE_ID_ICON_NAV_RB_MIDDLE;
    case NAV_RB_LATE:   return RESOURCE_ID_ICON_NAV_RB_LATE;
    case NAV_RB_LAP:    return RESOURCE_ID_ICON_NAV_RB_LAP;
    default:            return RESOURCE_ID_ICON_NAV_RB_UNKNOWN;
  }
}

void icon_nav(GContext *ctx, GRect box) {
  int man = wire_nav_maneuver();
  bool rh = wire_nav_right_hand();
  uint32_t res;
  bool flip = false;

  switch (man) {
    case NAV_STRAIGHT:     res = RESOURCE_ID_ICON_NAV_STRAIGHT; break;
    case NAV_LEFT:         res = RESOURCE_ID_ICON_NAV_LEFT; break;
    case NAV_RIGHT:        res = RESOURCE_ID_ICON_NAV_LEFT; flip = true; break;
    case NAV_SLIGHT_LEFT:  res = RESOURCE_ID_ICON_NAV_SLIGHT_LEFT; break;
    case NAV_SLIGHT_RIGHT: res = RESOURCE_ID_ICON_NAV_SLIGHT_LEFT; flip = true; break;
    case NAV_SHARP_LEFT:   res = RESOURCE_ID_ICON_NAV_SHARP_LEFT; break;
    case NAV_SHARP_RIGHT:  res = RESOURCE_ID_ICON_NAV_SHARP_LEFT; flip = true; break;
    case NAV_UTURN:        res = RESOURCE_ID_ICON_NAV_UTURN; flip = rh; break;
    case NAV_ROUNDABOUT:   res = roundabout_res(wire_nav_roundabout()); flip = rh; break;
    case NAV_ARRIVE:       res = RESOURCE_ID_ICON_NAV_ARRIVE; break;
    default:               return;
  }

  icon_draw(ctx, box, res, flip);
  if (man == NAV_ROUNDABOUT) draw_digit(ctx, grect_center_point(&box), wire_nav_exit(), COL_INK);
}

void icons_deinit(void) {
  for (int i = 0; i < CACHE_N; i++) {
    if (s_cache[i].bmp) gbitmap_destroy(s_cache[i].bmp);
    s_cache[i] = (Cached){ 0 };
  }
  s_next = 0;
}
