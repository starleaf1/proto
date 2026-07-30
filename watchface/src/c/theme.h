#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// The whole palette and both font choices, in one place.
//
// Two hues, two tints each, and red spent on exactly one thing. Cool means
// time-bound and passive; warm means it wants something from you. That single
// rule covers every marker state without needing a legend, which is where the
// low-clutter feel comes from.
//
// flint has one ink, so hue is unavailable there and every COL_* below folds to
// black. The prominence that colour carries on emery and gabbro is carried on
// flint by *shape* — an upcoming task marker is hollow where a colour platform
// draws it solid, and the notch ring changes depth in the stack. See dial.c;
// those are the only two places the platforms diverge.
// ---------------------------------------------------------------------------

#define COL_BG          GColorWhite
#define COL_INK         GColorBlack

// The minute numeral, and the bottom slot's in-progress bar.
#define COL_ACCENT      PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack)

// Appointment bands — one hue for both states.
//
// The first pass gave "upcoming" its own pale tint, which failed twice over:
// GColorCeleste measures far too light to see on white, and any second tint is
// redundant anyway. Prominence is already carried by depth, which works identically
// on all three displays: a running band is drawn twice as deep into the notch zone as
// an upcoming one. On flint, where the band shares the notches' ink, the running
// band also inverts the notches crossing it. Depth says it; hue does not need to.
#define COL_BAND        PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack)

// The hour index. Ink, not the accent, and deliberately so: the accent is the
// band's colour, and an index drawn in it disappeared entirely whenever the
// current hour fell inside a running appointment.
#define COL_INDEX       GColorBlack

// Point-in-time markers: amber until due, orange once overdue.
#define COL_TASK_SOON   PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack)
#define COL_TASK_LATE   PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack)

// Top slot. Red is reserved for the companion being gone — the only state on
// this face that should ever alarm you. GColorGreen/GColorYellow stay unused:
// on white they measure 1.4:1 and 1.1:1, so a free-floating shape in either is
// invisible. The darker Chrome/Orange pair is what unoutlined shapes get.
#define COL_ALERT       PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define COL_WARN        PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack)

// Three sizes per platform rather than one set scaled by the SDK, because Pebble fonts
// are fixed-pixel resources.
//
// Three *roles*, not two, because four text rows have to share the space inside the
// notch ring and the slots are the ones that should give way. A smaller slot size also
// puts them below the date in the visual hierarchy, which is where they belong.
#if defined(PBL_PLATFORM_FLINT)
#  define RES_NUM_FONT   RESOURCE_ID_NUM_40
#  define RES_DATE_FONT  RESOURCE_ID_DATE_22
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_18
#elif defined(PBL_PLATFORM_EMERY)
#  define RES_NUM_FONT   RESOURCE_ID_NUM_58
#  define RES_DATE_FONT  RESOURCE_ID_DATE_30
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_24
#else   // gabbro
#  define RES_NUM_FONT   RESOURCE_ID_NUM_76
#  define RES_DATE_FONT  RESOURCE_ID_DATE_38
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_30
#endif
