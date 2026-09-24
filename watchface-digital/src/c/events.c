#include "events.h"

#define SLOT_NEAR_S  (30 * 60)

// The far tier reaches exactly as far as the strip does, deliberately: the
// countdown should never name something the reader cannot also see a marker for.
#define SLOT_FAR_S   STRIP_AHEAD_S

static Event s_events[EVENTS_MAX];

const Event *events_table(void) { return s_events; }

// ---------------------------------------------------------------------------
// Across a relaunch
//
// The table used to be RAM only, on the reasoning that the companion re-flushes
// on its periodic tick. It does — but that tick is the slow tier's 900 s (see
// ../../docs/protocol.md), so coming back from a glance at a notification left
// the strip blank for up to a quarter of an hour. Blank does not read as "waiting",
// it reads as "nothing scheduled", which is the one thing these faces must never
// say by accident. Nothing else on the display makes that mistake: the bottom
// slot says the companion is gone, and an empty strip looks exactly like a free
// afternoon.
//
// Storing it is safe for the reason the face already keeps drawing markers while
// the companion is unreachable: an entry carries an absolute UTC start, so it
// ages on its own. event_visible() is a function of `now` and events_gc() drops
// whatever can no longer be drawn, both on the first paint after the restore.
//
// What a restore cannot know is whether an entry was cancelled while the app was
// away. The next flush corrects that, and one period of a stale marker is a
// better reading than one period of an empty strip.
// ---------------------------------------------------------------------------

#define EVENTS_PERSIST_KEY 2      // 1 is wbatt's
#define EVENTS_PERSIST_VER 1

// One entry as it sits in flash, which is not the in-RAM Event: `used` is implied
// by the count, and a layout of its own is what makes the size check in
// events_load() mean anything.
typedef struct {
  uint32_t id;
  int32_t  start;
  uint16_t dur_min;
  uint8_t  kind;
} StoreRec;

// How many fit one key. Solved rather than chosen: a key holds
// PERSIST_DATA_MAX_LENGTH bytes, the two header bytes cost four once the records'
// own alignment is paid, and the rest is records — 21 of them.
#define STORE_CAP ((PERSIST_DATA_MAX_LENGTH - 4) / (int)sizeof(StoreRec))

typedef struct {
  uint8_t  version;
  uint8_t  count;
  StoreRec rec[STORE_CAP];
} EventStore;

// A key that will not hold the struct written to it fails at the write and reads
// back as nothing, which is a blank strip and no other symptom. Break the build
// instead: a field added to StoreRec has to be paid for out of STORE_CAP.
typedef char events_store_fits[(sizeof(EventStore) <= PERSIST_DATA_MAX_LENGTH) ? 1 : -1];

void events_save(void) {
  EventStore st = { .version = EVENTS_PERSIST_VER, .count = 0 };

  // Soonest first, so it is the furthest-future entries that fall off the end
  // when the table holds more than a key does — the companion caps a sync at 24
  // and this keeps 21. The same call events_upsert() makes on a full table, for
  // the same reason: the near term is what renders, and the far end comes back on
  // the next flush.
  for (int i = 0; i < EVENTS_MAX; i++) {
    const Event *e = &s_events[i];
    if (!e->used) continue;

    int at = st.count;
    if (at == STORE_CAP) {
      int furthest = 0;
      for (int j = 1; j < STORE_CAP; j++) {
        if (st.rec[j].start > st.rec[furthest].start) furthest = j;
      }
      if (st.rec[furthest].start <= (int32_t)e->start) continue;
      at = furthest;
    } else {
      st.count++;
    }

    st.rec[at] = (StoreRec){
      .id = e->id, .start = (int32_t)e->start,
      .dur_min = e->dur_min, .kind = e->kind,
    };
  }

  // Written even when nothing is used, because "the last thing the companion said
  // was that the next six hours are empty" is a reading too, and a stale table
  // left in flash would contradict it.
  persist_write_data(EVENTS_PERSIST_KEY, &st, sizeof st);
}

void events_load(void) {
  if (!persist_exists(EVENTS_PERSIST_KEY)
      || persist_get_size(EVENTS_PERSIST_KEY) != (int)sizeof(EventStore)) {
    return;
  }

  EventStore st;
  if (persist_read_data(EVENTS_PERSIST_KEY, &st, sizeof st) != (int)sizeof st) return;
  if (st.version != EVENTS_PERSIST_VER) return;

  // Back in through the front door: events_upsert() owns what a row means, so a
  // restore cannot install a duplicate id or a row the table would not have
  // accepted from the wire.
  int n = st.count < STORE_CAP ? st.count : STORE_CAP;
  for (int i = 0; i < n; i++) {
    events_upsert(st.rec[i].id, (time_t)st.rec[i].start, st.rec[i].dur_min,
                  st.rec[i].kind);
  }
}

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

static const Event *find_in(const Event *table, uint32_t id) {
  for (int i = 0; i < EVENTS_MAX; i++) {
    if (table[i].used && table[i].id == id) return &table[i];
  }
  return NULL;
}

bool events_same_as(const Event *before) {
  int n_before = 0, n_now = 0;
  for (int i = 0; i < EVENTS_MAX; i++) {
    if (before[i].used) n_before++;
    if (!s_events[i].used) continue;
    n_now++;
    // Field by field, not memcmp: struct padding is not guaranteed to agree.
    const Event *e = &s_events[i];
    const Event *o = find_in(before, e->id);
    if (!o || o->start != e->start || o->dur_min != e->dur_min || o->kind != e->kind) {
      return false;
    }
  }
  // Ids are unique within a table, so equal counts with every current entry
  // found means nothing was dropped either.
  return n_before == n_now;
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
  if (e->start > now + STRIP_AHEAD_S) return false;        // below the strip, for now
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
    // Only past entries are collectable. A future one that is merely below the
    // strip is not dead — it scrolls into view as the window slides.
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
  // Both cases count down to the next change: the start of what is coming, or the
  // end of what is running. Time already spent inside a meeting is not something
  // anyone acts on; time until it lets them go is.
  if (pick == running) {
    p.running = true;
    p.seconds = (int32_t)(event_end(pick) - now);
  } else {
    p.running = false;
    p.seconds = (int32_t)(pick->start - now);
  }
  return p;
}
