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
// flint by *shape* and by *position*: a running band is deeper than an upcoming
// one, the notches invert where a running band crosses them, and a point marker
// above the pointer is overdue while one below it is not. See strip.c.
// ---------------------------------------------------------------------------

#define COL_BG          GColorWhite
#define COL_INK         GColorBlack

// The clock, and the countdown's progress bar.
#define COL_ACCENT      PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack)

// Appointment bands — one hue for both states.
//
// The first pass gave "upcoming" its own pale tint, which failed twice over:
// GColorCeleste measures far too light to see on white, and any second tint is
// redundant anyway. Prominence is already carried by depth, which works identically
// on all three displays: a running band is drawn twice as deep into the notch zone as
// an upcoming one. On flint, where the band shares the notches' ink, the running
// band also inverts the notches crossing it. Depth says it; hue does not need to.
//
// Nor does position, which the strip added for free: a band that has started is
// necessarily crossing or above the pointer.
#define COL_BAND        PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack)

// The "now" pointer. Ink, not the accent, and deliberately so: the accent is the
// band's colour, and a pointer drawn in it disappeared entirely whenever now fell
// inside a running appointment — which is most of the time it matters.
#define COL_INDEX       GColorBlack

// Point-in-time markers: amber until due, orange once overdue.
#define COL_TASK_SOON   PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack)
#define COL_TASK_LATE   PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack)

// The warnings row. Red is reserved for the companion being gone — the only state
// on this face that should ever alarm you. GColorGreen/GColorYellow stay unused:
// on white they measure 1.4:1 and 1.1:1, so a free-floating shape in either is
// invisible. The darker Chrome/Orange pair is what unoutlined shapes get.
#define COL_ALERT       PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define COL_WARN        PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack)

// Three sizes per platform rather than one set scaled by the SDK, because Pebble fonts
// are fixed-pixel resources.
//
// Three *roles*, not two, because five rows have to share the height beside the strip
// and the conditional ones are what should give way. A smaller slot size also puts
// them below the date in the visual hierarchy, which is where they belong.
//
// Every size here came down when the clock became five glyphs instead of two.
// Orbitron is a wide face — "00:00" measures 3.55 em, so a digit is five sixths of the
// point size — and beside the strip the clock is what the whole column's width is
// budgeted against. **The ceiling is content_width / 3.55**, where the content width
// is what fit_row() leaves; going over does not wrap, it silently clips. The date and
// the slots then came down in turn to seat five rows in the height that is left, which
// is the tighter constraint on flint. Measure before changing any of them: what fits
// is a property of the layout, not of this table.
#if defined(PBL_PLATFORM_FLINT)
#  define RES_NUM_FONT   RESOURCE_ID_NUM_28
#  define RES_DATE_FONT  RESOURCE_ID_DATE_20
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_16
#elif defined(PBL_PLATFORM_EMERY)
#  define RES_NUM_FONT   RESOURCE_ID_NUM_40
#  define RES_DATE_FONT  RESOURCE_ID_DATE_28
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_22
#else   // gabbro
#  define RES_NUM_FONT   RESOURCE_ID_NUM_36
#  define RES_DATE_FONT  RESOURCE_ID_DATE_26
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_22
#endif
