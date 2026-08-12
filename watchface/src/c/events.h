#pragma once
#include <pebble.h>
#include "geometry.h"

// ---------------------------------------------------------------------------
// The calendar entries the companion has told us about.
//
// A fixed table, no allocation: the companion caps a sync at what fits, and the
// strip can only usefully render a couple of dozen markers anyway. The watch
// persists nothing across launches — the companion re-flushes on every connect.
// ---------------------------------------------------------------------------

#define EVENTS_MAX 32

enum { EV_APPOINTMENT = 0, EV_TASK = 1 };

typedef struct {
  uint32_t id;
  time_t   start;
  uint16_t dur_min;   // 0 = point-in-time: a task or a reminder
  uint8_t  kind;
  bool     used;
} Event;

// How long an entry stays worth holding once it is past.
//
// A point entry lingers exactly as far back as the strip reaches, and that is not
// cosmetic: events_gc() only collects entries that are both past *and* invisible, so
// a linger longer than the window would keep rows alive that can never be drawn
// again. The strip's own extent is in geometry.h — STRIP_BACK_S and STRIP_AHEAD_S —
// because the track is what defines what "visible" means.
#define LONG_LINGER_S   (5 * 60)
#define SHORT_LINGER_S  STRIP_BACK_S

void events_clear(void);
void events_upsert(uint32_t id, time_t start, uint16_t dur_min, uint8_t kind);
void events_remove(uint32_t id);
void events_gc(time_t now);           // drop entries that can never show again

const Event *events_table(void);      // EVENTS_MAX entries; check .used

static inline bool event_is_long(const Event *e) { return e->dur_min > 0; }
static inline time_t event_end(const Event *e) {
  return e->start + (time_t)e->dur_min * 60;
}

bool event_visible(const Event *e, time_t now);
bool event_prominent(const Event *e, time_t now);

// What the bottom slot should say, resolved by the priority the face promises:
// anything inside 30 min first, then an appointment already running, then
// anything inside 3 h. Soonest wins within a tier.
typedef struct {
  bool    valid;
  uint8_t kind;
  bool    counting_up;   // an appointment in progress, counting since it began
  int32_t seconds;       // magnitude of the countdown or count-up
  int     pct;           // how far through an in-progress appointment, 0..100
} SlotPick;

SlotPick events_pick_slot(time_t now);
