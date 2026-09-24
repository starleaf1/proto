#pragma once
#include <pebble.h>
#include "geometry.h"

// ---------------------------------------------------------------------------
// The calendar entries the companion has told us about.
//
// A fixed table, no allocation: the companion caps a sync at what fits, and the
// ring can only usefully render a couple of dozen markers anyway. The table
// survives a relaunch in persistent storage — see events_save() — because a
// watchface is killed and restarted every time the user glances at another app,
// and the companion's re-flush is up to a period away.
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
// A point entry lingers exactly as far behind now as the ring reaches, and that is
// not cosmetic: events_gc() only collects entries that are both past *and* invisible,
// so a linger longer than the window would keep rows alive that can never be drawn
// again. The ring's own extent is in geometry.h — RING_BACK_S and RING_AHEAD_S —
// because the window is what defines what "visible" means. An overdue reminder is
// therefore on screen until the trailing end cap sweeps past it.
#define LONG_LINGER_S   (5 * 60)
#define SHORT_LINGER_S  RING_BACK_S

void events_clear(void);
void events_upsert(uint32_t id, time_t start, uint16_t dur_min, uint8_t kind);
void events_remove(uint32_t id);
void events_gc(time_t now);           // drop entries that can never show again

// Whether the table holds the same entries as `before`, an earlier copy of
// events_table(). By id rather than by slot: a flush refills the table in wire
// order, so an unchanged set can land in different slots once events_gc() has
// freed one, and that is not a change anyone can see.
bool events_same_as(const Event *before);

const Event *events_table(void);      // EVENTS_MAX entries; check .used

// The table across a relaunch: written at exit, read back at launch.
//
// A watchface is not a resident program — the firmware kills it whenever the user
// opens anything else and starts it again when they come back, which is the
// ordinary way these faces are used, and an in-RAM table meant the ring came back
// empty every time. The same reason wbatt.h gives for persisting its anchor.
void events_save(void);
void events_load(void);

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
  bool    counting_up;   // an appointment under way, counting since it began
  int32_t seconds;       // magnitude of the countdown or count-up; the sign the
                         // slot prints comes from counting_up, not from this
} SlotPick;

SlotPick events_pick_slot(time_t now);
