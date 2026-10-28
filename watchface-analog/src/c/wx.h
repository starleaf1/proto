#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Severe weather the companion has forecast. See ../../docs/protocol.md.
//
// The phone decides what counts as severe — the thresholds are policy, and the
// policy is the phone's — and sends a hazard, an absolute onset and a duration.
// The watch decides what that looks like, which is why the hazard is an enum and
// not a glyph.
//
// Not part of the calendar table, on purpose. A calendar flush clears that table,
// and these arrive on their own key with their own replace-whole semantics; and a
// face that predates this key ignores it, where a new calendar `kind` would have
// been folded to an appointment and drawn as a meeting.
//
// Shared by copy with the other face, and identical there: nothing in here knows
// how far ahead its face can see. Each face passes its own horizon in.
// ---------------------------------------------------------------------------

#define WX_MAX 8

// Thirty minutes out, an alert leaves the timeline and takes the slot, counting
// down to its onset. On the analog face that is what keeps it out from under the
// hour hand, which reaches a glyph at onset and starts covering it about twenty
// minutes before; the same rule on both faces keeps the slot's reading the same.
#define WX_NEAR_S (30 * 60)

enum {
  WX_NONE = 0,       // never stored: the decoder skips it
  WX_THUNDER,
  WX_RAIN,
  WX_SNOW,
  WX_ICE,
  WX_GUSTS,
  WX_HEAT,
  WX_HAZARD_MAX = WX_HEAT,
  // A hazard this watch was built before. Drawn as a generic mark rather than
  // dropped: something severe is coming, and saying so without the detail is a
  // truer reading than silence.
  WX_UNKNOWN = 255,
};

typedef struct {
  time_t   onset;
  uint16_t dur_min;
  uint8_t  hazard;
  bool     used;
} WxAlert;

static inline time_t wx_end(const WxAlert *a) {
  return a->onset + (time_t)a->dur_min * 60;
}

void wx_clear(void);
void wx_add(time_t onset, uint16_t dur_min, uint8_t hazard);
void wx_gc(time_t now);               // drop whatever has ended

const WxAlert *wx_table(void);        // WX_MAX entries; check .used

// Whether the table holds the same alerts as `before`, an earlier copy of
// wx_table(). Alerts carry no id, so this compares them as a set of values.
bool wx_same_as(const WxAlert *before);

// What the slot should say: an alert under way, counting to its end, or failing
// that the next one inside WX_NEAR_S, counting to its onset.
typedef struct {
  const WxAlert *alert;   // NULL when there is nothing to say
  bool    running;
  int32_t seconds;        // until the end if running, until the onset if not
} WxPick;

WxPick wx_pick_slot(time_t now);

// The alerts for the timeline: not yet begun and at most `horizon_s` away, soonest
// first. With `skip_slot`, less the one wx_pick_slot() has taken — the analog face
// moves an imminent alert off the dial, where the hour hand would cover it; the
// digital face leaves it on the strip, where nothing does. Returns how many were
// written to `out`.
int wx_upcoming(time_t now, int32_t horizon_s, bool skip_slot,
                const WxAlert **out, int max);

// The table across a relaunch, for the reason events.h gives.
void wx_save(void);
void wx_load(void);
