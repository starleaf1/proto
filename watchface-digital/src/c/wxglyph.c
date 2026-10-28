#include "wxglyph.h"
#include "wx.h"

// Loaded on first use and kept for the life of the app: a paint draws up to eight
// icons a minute, and seven twelve-pixel bitmaps are a few hundred bytes of heap. The
// last slot is the hazard this watch does not know.
#define ICON_UNKNOWN (WX_HAZARD_MAX + 1)
static GBitmap *s_icon[ICON_UNKNOWN + 1];

static uint32_t resource_for(int slot) {
  switch (slot) {
    case WX_THUNDER: return RESOURCE_ID_WX_THUNDER;
    case WX_RAIN:    return RESOURCE_ID_WX_RAIN;
    case WX_SNOW:    return RESOURCE_ID_WX_SNOW;
    // Freezing rain has no icon of its own in the set, and the hail icon is the one
    // that says "ice" — a crystal, not a flake. Folding it into snow would hide the
    // one hazard on this list that is worse than it looks out of a window.
    case WX_ICE:     return RESOURCE_ID_WX_HAIL;
    case WX_GUSTS:   return RESOURCE_ID_WX_WIND;
    case WX_HEAT:    return RESOURCE_ID_WX_HEAT;
    // A hazard this watch predates: the warning triangle. Something is coming, and the
    // face says so without claiming to know what.
    default:         return RESOURCE_ID_WX_OTHER;
  }
}

void wx_glyph(GContext *ctx, GRect box, uint8_t hazard) {
  int slot = (hazard >= 1 && hazard <= WX_HAZARD_MAX) ? hazard : ICON_UNKNOWN;
  if (!s_icon[slot]) s_icon[slot] = gbitmap_create_with_resource(resource_for(slot));
  if (!s_icon[slot]) return;

  GPoint c = grect_center_point(&box);
  // GCompOpSet honours the icons' transparency, so whatever is under them shows
  // through where they are empty.
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_icon[slot],
      GRect(c.x - WX_ICON_PX / 2, c.y - WX_ICON_PX / 2, WX_ICON_PX, WX_ICON_PX));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

void wx_glyph_deinit(void) {
  for (int i = 0; i <= ICON_UNKNOWN; i++) {
    if (s_icon[i]) {
      gbitmap_destroy(s_icon[i]);
      s_icon[i] = NULL;
    }
  }
}
