#include "wbatt.h"

#define WBATT_PERSIST_KEY 1
#define WBATT_VERSION     1

// Below this the sample is noise: the reported percentage moves in steps, and
// one step could as easily be a rounding boundary as real drain.
#define MIN_DROP_PCT 2

// Once the anchor is this far behind, move it up to the current sample. That
// keeps the measurement window to roughly the last fifth of a discharge — a few
// days on a multi-week watch — so a change in usage shows up instead of being
// averaged away against a fortnight-old reading.
#define REANCHOR_DROP_PCT 20

typedef struct {
  uint8_t version;
  uint8_t anchor_pct;
  uint8_t cur_pct;
  uint8_t charging;
  int32_t anchor_time;
} WBattState;

static WBattState s_st;

static void save(void) {
  persist_write_data(WBATT_PERSIST_KEY, &s_st, sizeof s_st);
}

static void anchor_now(uint8_t pct, time_t now) {
  s_st.anchor_pct = pct;
  s_st.anchor_time = (int32_t)now;
}

void wbatt_init(void) {
  BatteryChargeState bs = battery_state_service_peek();
  time_t now = time(NULL);

  if (persist_exists(WBATT_PERSIST_KEY)
      && persist_get_size(WBATT_PERSIST_KEY) == (int)sizeof s_st) {
    persist_read_data(WBATT_PERSIST_KEY, &s_st, sizeof s_st);
  }
  if (s_st.version != WBATT_VERSION) {
    s_st = (WBattState){ .version = WBATT_VERSION };
    anchor_now((uint8_t)bs.charge_percent, now);
  }
  wbatt_update(bs);
}

void wbatt_update(BatteryChargeState state) {
  uint8_t pct = (uint8_t)state.charge_percent;
  time_t now = time(NULL);
  bool charging = state.is_charging || state.is_plugged;

  s_st.cur_pct = pct;
  s_st.charging = charging ? 1 : 0;

  // Any of these invalidates the measurement: a charge in progress, a charge
  // that happened while we were not running, or a clock that has moved
  // backwards (a timezone change or a phone-driven correction).
  if (charging || pct > s_st.anchor_pct || (int32_t)now < s_st.anchor_time) {
    anchor_now(pct, now);
  } else if (s_st.anchor_pct - pct >= REANCHOR_DROP_PCT) {
    anchor_now(pct, now);
  }
  save();
}

int wbatt_hours(void) {
  if (s_st.charging) return -1;

  int drop = (int)s_st.anchor_pct - (int)s_st.cur_pct;
  if (drop < MIN_DROP_PCT) return -1;

  int32_t elapsed = (int32_t)time(NULL) - s_st.anchor_time;
  if (elapsed <= 0) return -1;

  // hours = remaining% / (drop% per hour). Worst case is 100 * ~2.6M seconds,
  // which stays inside int32.
  int32_t hours = (int32_t)s_st.cur_pct * elapsed / ((int32_t)drop * 3600);
  if (hours < 0) return -1;
  if (hours > 9999) hours = 9999;
  return (int)hours;
}
