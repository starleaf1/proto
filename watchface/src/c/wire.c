#include "wire.h"
#include "events.h"

// Companion liveness watchdog. The phone declares its own cadence in Heartbeat
// (seconds until it next expects to check in) rather than the two sides agreeing
// a constant, so it can run fast during navigation and slow the rest of the time
// without a watchface update. We allow 2.5 periods: one miss is ordinary on a
// scheduler Android throttles, two in a row is a dead companion.
#define HB_GRACE_NUM 5
#define HB_GRACE_DEN 2
#define HB_MIN_S     15      // clamp a garbled period into something survivable
#define HB_MAX_S     3600
#define HB_DEFAULT_S 900     // assumed cadence before the companion declares one

// A turn instruction is the one thing here that decays on its own. The general
// watchdog is too slow to catch it: a phantom "right in 250 m" is worse than no
// instruction at all, so nav expires on its own clock.
#define NAV_STALE_MS 120000

#define CAL_VERSION   1
#define CAL_HEADER    6
#define CAL_RECORD    12
#define CAL_FLUSH     0x1
#define CAL_MORE      0x2
#define CAL_OP_UPSERT 0
#define CAL_OP_REMOVE 1

// The inbox has to hold a full sync message: 6 header bytes plus 12 per record,
// and the companion caps a message at 24 records. An oversized AppMessage is not
// truncated, it fails to transmit entirely, so this is sized with headroom. The
// outbox is vestigial — the watch never sends app data.
#define INBOX_SIZE  512
#define OUTBOX_SIZE 64

static void (*s_on_change)(void) = NULL;

static bool      s_connected = true;
static bool      s_companion = true;
static AppTimer *s_hb_timer = NULL;
static uint32_t  s_hb_grace_ms = 0;

static int       s_nav_man = NAV_NONE;
static int       s_nav_dist = 0;
static int       s_nav_unit = NAV_UNIT_M;
static AppTimer *s_nav_timer = NULL;

static int       s_phone_batt = -1;

static void changed(void) {
  if (s_on_change) s_on_change();
}

// ---------------------------------------------------------------------------
// Watchdogs
// ---------------------------------------------------------------------------

static uint32_t hb_grace_from(int32_t period_s) {
  if (period_s < HB_MIN_S) period_s = HB_MIN_S;
  if (period_s > HB_MAX_S) period_s = HB_MAX_S;
  return (uint32_t)period_s * 1000u * HB_GRACE_NUM / HB_GRACE_DEN;
}

static void hb_stop(void) {
  if (s_hb_timer) {
    app_timer_cancel(s_hb_timer);   // the handle is dead the moment this returns
    s_hb_timer = NULL;
  }
}

static void hb_expired(void *data) {
  s_hb_timer = NULL;                // an elapsed handle must never be cancelled
  s_companion = false;
  // Drop nav with the companion. Everything else is either the watch's own or
  // hidden behind the companion-down alert anyway.
  s_nav_man = NAV_NONE;
  changed();
}

// Sole owner of the watchdog's lifetime: every path that hears from the
// companion or changes s_connected calls this, and nothing else touches the
// handle.
static void hb_sync(void) {
  hb_stop();
  if (!s_connected) return;         // no link, no heartbeat expected
  s_hb_timer = app_timer_register(s_hb_grace_ms, hb_expired, NULL);
}

static void nav_expired(void *data) {
  s_nav_timer = NULL;
  s_nav_man = NAV_NONE;
  changed();
}

// Same single-owner shape as hb_sync: one place decides whether the timer should
// be running, so a fresh instruction always re-arms rather than inheriting the
// remainder of the previous one's window.
static void nav_sync(void) {
  if (s_nav_timer) {
    app_timer_cancel(s_nav_timer);
    s_nav_timer = NULL;
  }
  if (s_nav_man != NAV_NONE) {
    s_nav_timer = app_timer_register(NAV_STALE_MS, nav_expired, NULL);
  }
}

// ---------------------------------------------------------------------------
// Wire decoding
// ---------------------------------------------------------------------------

// Little-endian, byte at a time. The blob's alignment inside the dictionary is
// not guaranteed, and a cast-and-deref would be an unaligned load.
static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool handle_calendar(DictionaryIterator *iter) {
  Tuple *f = dict_find(iter, MESSAGE_KEY_CalFlags);
  Tuple *b = dict_find(iter, MESSAGE_KEY_CalEvents);
  if (!f && !b) return false;

  int32_t flags = f ? f->value->int32 : 0;
  // Clear before the payload check, not after: a flush carrying no records is
  // how the companion says the next six hours are empty.
  if (flags & CAL_FLUSH) events_clear();

  if (!b || b->type != TUPLE_BYTE_ARRAY || b->length < CAL_HEADER) {
    return (flags & CAL_FLUSH) != 0;
  }
  const uint8_t *p = b->value->data;
  if (p[0] != CAL_VERSION) return (flags & CAL_FLUSH) != 0;

  // Trust the tuple's length over the header's count — a mismatch means a
  // truncated or malformed message, and reading past the buffer is worse than
  // dropping the tail.
  int count = p[1];
  int fits = (b->length - CAL_HEADER) / CAL_RECORD;
  if (count > fits) count = fits;

  for (int i = 0; i < count; i++) {
    const uint8_t *r = p + CAL_HEADER + i * CAL_RECORD;
    uint32_t id    = rd_u32(r);
    int32_t  start = (int32_t)rd_u32(r + 4);   // absolute UTC seconds
    uint16_t dur   = rd_u16(r + 8);
    uint8_t  kind  = r[10];
    uint8_t  op    = r[11];

    if (op == CAL_OP_REMOVE) {
      events_remove(id);
    } else {
      events_upsert(id, (time_t)start, dur, kind > EV_TASK ? EV_APPOINTMENT : kind);
    }
  }
  return true;
}

static bool handle_nav(DictionaryIterator *iter) {
  Tuple *m = dict_find(iter, MESSAGE_KEY_NavManeuver);
  if (!m) return false;

  int32_t v = m->value->int32;
  s_nav_man = (v > NAV_NONE && v <= NAV_MANEUVER_MAX) ? (int)v : NAV_NONE;

  Tuple *d = dict_find(iter, MESSAGE_KEY_NavDistance);
  if (d) s_nav_dist = d->value->int32 < 0 ? 0 : (int)d->value->int32;

  Tuple *u = dict_find(iter, MESSAGE_KEY_NavUnit);
  if (u) {
    int32_t uv = u->value->int32;
    s_nav_unit = (uv >= NAV_UNIT_M && uv <= NAV_UNIT_MAX) ? (int)uv : NAV_UNIT_M;
  }
  nav_sync();
  return true;
}

static void inbox_received(DictionaryIterator *iter, void *ctx) {
  bool dirty = false;

  if (handle_calendar(iter)) dirty = true;
  if (handle_nav(iter)) dirty = true;

  Tuple *pb = dict_find(iter, MESSAGE_KEY_PhoneBattery);
  if (pb) {
    int32_t v = pb->value->int32;
    int next = (v >= 0 && v <= 100) ? (int)v : -1;
    if (next != s_phone_batt) {
      s_phone_batt = next;
      dirty = true;
    }
  }

  Tuple *h = dict_find(iter, MESSAGE_KEY_Heartbeat);
  if (h) s_hb_grace_ms = hb_grace_from(h->value->int32);

  // Arriving at all is the proof, whatever the message carried. The explicit
  // Heartbeat key exists only so a companion with no news can still speak.
  if (!s_companion) {
    s_companion = true;
    dirty = true;
  }
  hb_sync();

  if (dirty) changed();
}

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

void wire_set_connected(bool connected) {
  s_connected = connected;
  if (!connected) {
    // A turn instruction must not survive the gap; the rest either belongs to
    // the watch or is hidden behind the companion-down alert regardless.
    s_nav_man = NAV_NONE;
    nav_sync();
    // Clear the liveness verdict rather than carry it across. The companion was
    // never given a chance to check in while the link was down, so holding
    // "dead" against it would keep the alert up after a reconnect until the next
    // beat. Reset the grace too: a 30 s navigation cadence must not be inherited
    // into a reconnect where the companion is back on its slow tier.
    s_companion = true;
    s_hb_grace_ms = hb_grace_from(HB_DEFAULT_S);
  }
  hb_sync();                        // stands down while down, re-arms on return
  changed();
}

bool wire_companion_alive(void) { return s_connected && s_companion; }
bool wire_nav_active(void)      { return s_nav_man != NAV_NONE; }
int  wire_nav_maneuver(void)    { return s_nav_man; }
int  wire_nav_distance(void)    { return s_nav_dist; }
int  wire_nav_unit(void)        { return s_nav_unit; }
int  wire_phone_battery(void)   { return s_phone_batt; }

void wire_init(void (*on_change)(void)) {
  s_on_change = on_change;
  s_connected = connection_service_peek_pebble_app_connection();
  // Assume the companion is alive until the watchdog says otherwise. A watchface
  // relaunch raises no event the phone can see, so starting with the alert up
  // would flash "companion down" after every excursion into another app.
  s_companion = true;
  s_hb_grace_ms = hb_grace_from(HB_DEFAULT_S);

  app_message_register_inbox_received(inbox_received);
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  hb_sync();
}

void wire_deinit(void) {
  hb_stop();
  if (s_nav_timer) {
    app_timer_cancel(s_nav_timer);
    s_nav_timer = NULL;
  }
  s_on_change = NULL;
}
