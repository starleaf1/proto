#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// How long the watch has left, in hours.
//
// Pebble exposes a percentage, not a time remaining, so this measures the actual
// drain rate instead of assuming one. A nominal full-charge lifetime would be
// useless for the alert this feeds: at 30 days nominal, "24 hours left" lands at
// about 3%, which the watch would essentially never reach.
//
// The anchor survives launches in persistent storage, because a watchface is
// relaunched every time the user visits another app and an in-RAM sample history
// would never accumulate.
// ---------------------------------------------------------------------------

void wbatt_init(void);
void wbatt_update(BatteryChargeState state);

// Hours of charge remaining at the measured rate, or -1 while the estimate is
// not yet trustworthy — on first run, or whenever the watch is charging. The
// alert stays silent rather than guessing.
int wbatt_hours(void);
