#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Everything that arrives from the phone, and the two watchdogs that decide how
// long to keep believing it. See ../../docs/protocol.md.
//
// The companion sends meaning, never pixels: a maneuver enum rather than an
// arrow, a distance and a unit rather than a formatted string, a percentage
// rather than a warning. One of the three target displays has a single ink, and
// the phone cannot know which one it is talking to.
// ---------------------------------------------------------------------------

enum {
  NAV_NONE = 0,
  NAV_STRAIGHT,
  NAV_LEFT,
  NAV_RIGHT,
  NAV_SLIGHT_LEFT,
  NAV_SLIGHT_RIGHT,
  NAV_SHARP_LEFT,
  NAV_SHARP_RIGHT,
  NAV_UTURN,
  NAV_ROUNDABOUT,
  NAV_ARRIVE,
  NAV_MANEUVER_MAX = NAV_ARRIVE,
};

enum { NAV_UNIT_M = 0, NAV_UNIT_KM, NAV_UNIT_FT, NAV_UNIT_MI, NAV_UNIT_MAX = NAV_UNIT_MI };

// `on_change` is called whenever anything below changes value; the app uses it
// to mark the root layer dirty. Called from timer and AppMessage callbacks, so
// always on the app's own thread.
void wire_init(void (*on_change)(void));
void wire_deinit(void);

// The connection service's verdict, pushed in rather than polled: the watchdog
// has to stand down while the link is down, because a companion that was never
// given a chance to check in must not be judged for staying quiet.
void wire_set_connected(bool connected);

// False when the watch cannot vouch for anything phone-fed — either Bluetooth is
// down (it detects that itself) or the companion has stopped checking in (only a
// heartbeat reveals that). This is the top slot's highest priority.
bool wire_companion_alive(void);

bool wire_nav_active(void);
int  wire_nav_maneuver(void);
int  wire_nav_distance(void);
int  wire_nav_unit(void);
int  wire_phone_battery(void);   // 0..100, or -1 when the phone has not said
