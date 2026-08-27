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

// "Now". Red where there is colour, and black on flint because that is all there is.
//
// It is a rule struck across the strip on the colour displays rather than a wedge beside
// it, and that shape only works in a hue nothing else on the strip uses: it lies over a
// band, over the notches and over a marker, so it has to be legible against cerulean,
// black and amber at once. Red is the only entry here that is none of them.
//
// It was the accent first, and that failed for the reason the band is the accent: a
// "now" mark in the band's own colour disappeared whenever now fell inside a running
// appointment, which is most of the time it matters. Ink came next and is what flint
// still uses. Note that red is spent twice on this face now — here and on the
// companion-down alert — where it used to be reserved for the alert alone. The two are
// never confusable in practice, being a line on the ruler and a glyph in the bottom row,
// but the reservation is gone and this is where it went.
#define COL_INDEX       PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)

// Point-in-time markers: amber until due, orange once overdue.
#define COL_TASK_SOON   PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack)
#define COL_TASK_LATE   PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack)

// The warnings row. Red for the companion being gone — the only *state* on this face
// that should ever alarm you; it is shared now with the "now" rule above, which is not a
// state at all. GColorGreen/GColorYellow stay unused: on white they measure 1.4:1 and
// 1.1:1, so a free-floating shape in either is invisible.
//
// COL_WARN is Windsor tan and not the Chrome yellow it was, because this row is *text*.
// The rejection of green and yellow above was measured and right, and it stopped one
// step short: Chrome yellow is 1.9:1, which a solid triangle on the strip survives and
// two glyphs of "28%" beside a battery outline do not. Windsor tan is the same warm
// family at 4.1:1. The rule the palette now follows is that an ink drawing text needs
// 4.5:1 and an ink drawing a large solid shape can live near 2.5:1 — which is why
// COL_BAND and COL_TASK_SOON keep their brighter tints and this one does not.
#define COL_ALERT       PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define COL_WARN        PBL_IF_COLOR_ELSE(GColorWindsorTan, GColorBlack)

// Four sizes per platform rather than one set scaled by the SDK, because Pebble fonts
// are fixed-pixel resources.
//
// Three *text* roles, not two, because five rows have to share the height beside the
// strip and the conditional ones are what should give way. A smaller slot size also
// puts them below the date in the visual hierarchy, which is where they belong.
//
// TICK_* is the fourth, and it is not a row: it is the strip's own hour labels, and it
// is the smallest thing on the face by some way. It is deliberately a size nobody would
// read a *word* in — a graduation label is read as a number in a lane, not as prose, and
// its width is what the strip's label lane is sized from, so every pixel of it is paid
// for out of the content column. Its subset is digits only.
//
// Except on flint, where it is paid for out of nothing. `zone` there is closed by the
// wedge and not by the labels — see layout_compute — so any lane up to about sixteen
// pixels wide is free, and TICK_10's "00" measured ten. It shares emery's TICK_14 now
// and the content column did not move. Six pixels of ink on a 144 px display was the
// least legible text on the face, and it was the least legible for no reason.
//
// Every size here came down when the clock became five glyphs instead of two, and
// emery's went back up by two: measured off the framebuffer, "00:00" in NUM_40 left
// fourteen pixels of the content column unused, and the ceiling below puts NUM_42
// inside it with a pixel to spare. flint is already within a point of its own ceiling
// and gabbro's row is chord-bound, so neither moved.
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
#  define RES_TICK_FONT  RESOURCE_ID_TICK_14
#elif defined(PBL_PLATFORM_EMERY)
#  define RES_NUM_FONT   RESOURCE_ID_NUM_42
#  define RES_DATE_FONT  RESOURCE_ID_DATE_28
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_22
#  define RES_TICK_FONT  RESOURCE_ID_TICK_14
#else   // gabbro
#  define RES_NUM_FONT   RESOURCE_ID_NUM_36
#  define RES_DATE_FONT  RESOURCE_ID_DATE_26
#  define RES_SLOT_FONT  RESOURCE_ID_SLOT_22
#  define RES_TICK_FONT  RESOURCE_ID_TICK_16
#endif
