#include "wx.h"

static WxAlert s_wx[WX_MAX];

const WxAlert *wx_table(void) { return s_wx; }

void wx_clear(void) {
  for (int i = 0; i < WX_MAX; i++) s_wx[i].used = false;
}

void wx_add(time_t onset, uint16_t dur_min, uint8_t hazard) {
  if (hazard == WX_NONE) return;
  if (hazard > WX_HAZARD_MAX) hazard = WX_UNKNOWN;

  int free_slot = -1, furthest = -1;
  for (int i = 0; i < WX_MAX; i++) {
    if (!s_wx[i].used) {
      if (free_slot < 0) free_slot = i;
    } else if (furthest < 0 || s_wx[i].onset > s_wx[furthest].onset) {
      furthest = i;
    }
  }
  // Full: the near term is what renders, so the furthest-out alert gives way —
  // the same rule events_upsert() applies to the calendar.
  int slot = free_slot;
  if (slot < 0) {
    if (furthest < 0 || onset >= s_wx[furthest].onset) return;
    slot = furthest;
  }
  s_wx[slot] = (WxAlert){
    .onset = onset, .dur_min = dur_min, .hazard = hazard, .used = true,
  };
}

void wx_gc(time_t now) {
  for (int i = 0; i < WX_MAX; i++) {
    if (s_wx[i].used && wx_end(&s_wx[i]) <= now) s_wx[i].used = false;
  }
}

static bool same(const WxAlert *a, const WxAlert *b) {
  return a->onset == b->onset && a->dur_min == b->dur_min && a->hazard == b->hazard;
}

bool wx_same_as(const WxAlert *before) {
  // No ids, so match values, and never match one row of `before` twice — two
  // identical alerts are not one.
  bool taken[WX_MAX] = { false };
  int n_before = 0, n_now = 0;
  for (int i = 0; i < WX_MAX; i++) {
    if (before[i].used) n_before++;
    if (!s_wx[i].used) continue;
    n_now++;
    int j = 0;
    while (j < WX_MAX && (taken[j] || !before[j].used || !same(&before[j], &s_wx[i]))) j++;
    if (j == WX_MAX) return false;
    taken[j] = true;
  }
  return n_before == n_now;
}

WxPick wx_pick_slot(time_t now) {
  const WxAlert *running = NULL, *near = NULL;
  for (int i = 0; i < WX_MAX; i++) {
    const WxAlert *a = &s_wx[i];
    if (!a->used) continue;
    if (a->onset <= now) {
      if (now < wx_end(a) && (!running || wx_end(a) < wx_end(running))) running = a;
    } else if (a->onset - now <= WX_NEAR_S && (!near || a->onset < near->onset)) {
      near = a;
    }
  }

  // What is happening outranks what is about to. The phone sends alerts that do
  // not overlap, so the two meet only where one hazard hands over to the next,
  // and then the one outside is the one still to come.
  if (running) {
    return (WxPick){ .alert = running, .running = true,
                     .seconds = (int32_t)(wx_end(running) - now) };
  }
  if (near) {
    return (WxPick){ .alert = near, .running = false,
                     .seconds = (int32_t)(near->onset - now) };
  }
  return (WxPick){ .alert = NULL };
}

int wx_upcoming(time_t now, int32_t horizon_s, bool skip_slot,
                const WxAlert **out, int max) {
  const WxAlert *taken = skip_slot ? wx_pick_slot(now).alert : NULL;
  int n = 0;
  for (int i = 0; i < WX_MAX; i++) {
    const WxAlert *a = &s_wx[i];
    if (!a->used || a == taken || a->onset <= now) continue;
    if (a->onset - now > horizon_s) continue;

    // Insertion sort by onset: the table has no order of its own, and a face
    // resolving collisions wants neighbours on the timeline next to each other.
    // A full `out` keeps the soonest: the latest gives way, or the newcomer does.
    int k = n;
    if (n == max) {
      if (max == 0 || out[max - 1]->onset <= a->onset) continue;
      k = max - 1;
    } else {
      n++;
    }
    while (k > 0 && out[k - 1]->onset > a->onset) {
      out[k] = out[k - 1];
      k--;
    }
    out[k] = a;
  }
  return n;
}

// ---------------------------------------------------------------------------
// Across a relaunch. Key 3; 1 is wbatt's and 2 the calendar's.
// ---------------------------------------------------------------------------

#define WX_PERSIST_KEY 3
#define WX_PERSIST_VER 1

typedef struct {
  int32_t  onset;
  uint16_t dur_min;
  uint8_t  hazard;
} WxStoreRec;

typedef struct {
  uint8_t    version;
  uint8_t    count;
  WxStoreRec rec[WX_MAX];
} WxStore;

typedef char wx_store_fits[(sizeof(WxStore) <= PERSIST_DATA_MAX_LENGTH) ? 1 : -1];

void wx_save(void) {
  WxStore st = { .version = WX_PERSIST_VER, .count = 0 };
  for (int i = 0; i < WX_MAX; i++) {
    if (!s_wx[i].used) continue;
    st.rec[st.count++] = (WxStoreRec){
      .onset = (int32_t)s_wx[i].onset, .dur_min = s_wx[i].dur_min,
      .hazard = s_wx[i].hazard,
    };
  }
  // Written when empty too: "the last forecast was clear" must overwrite a
  // storm left over from the one before.
  persist_write_data(WX_PERSIST_KEY, &st, sizeof st);
}

void wx_load(void) {
  if (!persist_exists(WX_PERSIST_KEY)
      || persist_get_size(WX_PERSIST_KEY) != (int)sizeof(WxStore)) {
    return;
  }
  WxStore st;
  if (persist_read_data(WX_PERSIST_KEY, &st, sizeof st) != (int)sizeof st) return;
  if (st.version != WX_PERSIST_VER) return;

  int n = st.count < WX_MAX ? st.count : WX_MAX;
  for (int i = 0; i < n; i++) {
    wx_add((time_t)st.rec[i].onset, st.rec[i].dur_min, st.rec[i].hazard);
  }
}
