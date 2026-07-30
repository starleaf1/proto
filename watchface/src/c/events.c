#include "events.h"

#define SLOT_NEAR_S  (30 * 60)
#define SLOT_FAR_S   (3 * 60 * 60)

static Event s_events[EVENTS_MAX];

const Event *events_table(void) { return s_events; }

void events_clear(void) {
  for (int i = 0; i < EVENTS_MAX; i++) s_events[i].used = false;
}

void events_upsert(uint32_t id, time_t start, uint16_t dur_min, uint8_t kind) {
  int free_slot = -1;
  int furthest = -1;                 // fallback victim if the table is full
  for (int i = 0; i < EVENTS_MAX; i++) {
    Event *e = &s_events[i];
    if (e->used && e->id == id) {    // an update to something we already hold
      e->start = start;
      e->dur_min = dur_min;
      e->kind = kind;
      return;
    }
    if (!e->used) {
      if (free_slot < 0) free_slot = i;
    } else if (furthest < 0 || e->start > s_events[furthest].start) {
      furthest = i;
    }
  }
  // A full table evicts the entry furthest in the future rather than dropping
  // the new one: the near term is what actually renders, and the companion
  // re-sends everything on the next flush anyway.
  int slot = (free_slot >= 0) ? free_slot : furthest;
  if (slot < 0) return;
  if (free_slot < 0 && start >= s_events[furthest].start) return;  // new one is the least urgent

  s_events[slot] = (Event){
    .id = id, .start = start, .dur_min = dur_min, .kind = kind, .used = true,
  };
}

void events_remove(uint32_t id) {
  for (int i = 0; i < EVENTS_MAX; i++) {
    if (s_events[i].used && s_events[i].id == id) {
      s_events[i].used = false;
      return;
    }
  }
}

bool event_visible(const Event *e, time_t now) {
  if (!e->used) return false;
  if (e->start > now + WINDOW_AHEAD_S) return false;       // beyond the horizon
  if (event_is_long(e)) return now <= event_end(e) + LONG_LINGER_S;
  return now <= e->start + SHORT_LINGER_S;
}

bool event_prominent(const Event *e, time_t now) {
  if (event_is_long(e)) return now >= e->start && now <= event_end(e);
  return now >= e->start;                                  // an overdue reminder
}

void events_gc(time_t now) {
  for (int i = 0; i < EVENTS_MAX; i++) {
    Event *e = &s_events[i];
    // Only past entries are collectable. A future one that is merely beyond the
    // 6 h horizon is not dead — it becomes visible as the window slides.
    if (e->used && e->start <= now && !event_visible(e, now)) e->used = false;
  }
}

SlotPick events_pick_slot(time_t now) {
  SlotPick p = { .valid = false };
  const Event *near = NULL, *far = NULL, *running = NULL;

  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &s_events[i];
    if (!event_visible(e, now)) continue;

    if (e->start > now) {
      int32_t in = (int32_t)(e->start - now);
      if (in <= SLOT_NEAR_S && (!near || e->start < near->start)) near = e;
      if (in <= SLOT_FAR_S  && (!far  || e->start < far->start))  far  = e;
    } else if (event_is_long(e) && now < event_end(e)) {
      // Several can overlap; the one finishing soonest is the actionable one.
      if (!running || event_end(e) < event_end(running)) running = e;
    }
  }

  const Event *pick = near ? near : (running ? running : far);
  if (!pick) return p;

  p.valid = true;
  p.kind = pick->kind;
  if (pick == running) {
    p.counting_up = true;
    p.seconds = (int32_t)(now - pick->start);
    int32_t total = (int32_t)pick->dur_min * 60;
    p.pct = (total > 0) ? (int)(p.seconds * 100 / total) : 0;
    if (p.pct > 100) p.pct = 100;
  } else {
    p.counting_up = false;
    p.seconds = (int32_t)(pick->start - now);
  }
  return p;
}
