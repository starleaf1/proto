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

// The clock, and the count-up — its sign and its digits while it is running.
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

// Four fonts, all of them the firmware's own.
//
// They were Orbitron and Rajdhani SemiBold, compiled in as subsetted TTF resources at
// a size per platform. What the SDK does with a TTF is rasterise it once at build time,
// and a condensed semibold stem at sixteen pixels wants to be about 1.6 px wide: the
// rasteriser has to choose 1 or 2, it chose 1, and where two strokes converged it
// dropped one. The face came off the display lighter than the weight asked for and
// unevenly so. A system font is a hand-fitted 1-bit bitmap per size, blitted at integer
// offsets, so every stem is the width it was drawn at and every glyph is on the pixel
// grid by construction. That is the whole of why these are keys and not resource ids.
//
// The set is identical on all three platforms — diffed across the SDK's three
// pebble_fonts.h — so what varies below is the size, not what is available.
//
// Three *text* roles, not two, because five rows have to share the height beside the
// strip and the conditional ones are what should give way. A smaller slot size also
// puts them below the date in the visual hierarchy, which is where they belong.
//
// TICK_* is the fourth, and it is not a row: it is the strip's own hour labels, and it
// is the smallest thing on the face by some way. It is deliberately a size nobody would
// read a *word* in — a graduation label is read as a number in a lane, not as prose, and
// its width is what the strip's label lane is sized from, so every pixel of it is paid
// for out of the content column. It is the one role left in the regular weight: it sits
// below the date on purpose, and bolding it would take that back.
//
// Except on flint, where the lane is paid for out of nothing. `zone` there is closed by
// the wedge and not by the labels — see layout_compute — so any lane up to about sixteen
// pixels wide is free, and Gothic 14's "00" measures twelve against the Rajdhani it
// replaced at fourteen. The content column did not move on flint and gained two pixels
// on emery.
//
// Measured, on the emulator, with graphics_text_layout_get_content_size — which is what
// layout_compute asks and therefore the only number worth having:
//
//                     "00:00"      "MON 22"    "+00:00"     "00"
//   was  flint      98 x 28      62 x 20     42 x 16      14 x 14
//   now  flint      89 x 32      50 x 18     36 x 14      12 x 14
//   was  emery     149 x 42      86 x 28     59 x 22      14 x 14
//   now  emery     107 x 38      67 x 28     54 x 24      12 x 14
//
// A system font's number is its **pixel** size, and the content height comes back equal
// to it exactly — unlike the TTF resources, whose number was an em and whose height was
// not. So a row's height is now readable off this table.
//
// Two traps the Orbitron ceiling left behind, both gone:
//
//  - The clock no longer has a width ceiling worth the name. Orbitron put "00:00" at
//    3.55 em, which made the clock the binding constraint on the whole column and flint
//    within a point of its own limit; LECO puts it at 2.8, so the same column holds a
//    clock four points larger. flint has 22 px spare at LECO_32 and emery 49 at LECO_38.
//  - LECO's digits are tabular, where Orbitron's were not — a "1" was half the width of
//    a "0" there, so "14:21" fit a box "20:08" overflowed and the current time was never
//    the honest test. Every digit is the same width now.
//
// What still holds: over the column width graphics_draw_text does not complain, it wraps
// the minutes onto a second line or ellipsises them. And the date and the slots are what
// seat five rows in the height that is left, which is the tighter constraint on flint.
// Measure before changing any of them — what fits is a property of the layout, not of
// this table.
#if defined(PBL_PLATFORM_FLINT)
#  define FONT_NUM   FONT_KEY_LECO_32_BOLD_NUMBERS
#  define FONT_DATE  FONT_KEY_GOTHIC_18_BOLD
#  define FONT_SLOT  FONT_KEY_GOTHIC_14_BOLD
#  define FONT_TICK  FONT_KEY_GOTHIC_14
#elif defined(PBL_PLATFORM_EMERY)
#  define FONT_NUM   FONT_KEY_LECO_38_BOLD_NUMBERS
#  define FONT_DATE  FONT_KEY_GOTHIC_28_BOLD
#  define FONT_SLOT  FONT_KEY_GOTHIC_24_BOLD
#  define FONT_TICK  FONT_KEY_GOTHIC_14
#else   // gabbro
// One rung below emery's clock, as before: emery's row is the full width of the screen
// and this one is a chord of it.
//
// That chord is the binding constraint now, not an aside. Since the arc became half a
// turn the clock sits as high as its own width lets it — layout_compute solves the floor
// from ns.w and `zone` — and the row it lands on has about nine pixels to spare. So
// FONT_NUM cannot go up, and FONT_TICK is not free either: label_w is inside `zone`, so
// every pixel the hour lane gains comes straight out of the clock's slack. Change either
// and re-measure on the emulator before trusting it; the failure is silent, graphics_
// draw_text wrapping the minutes onto a second line rather than complaining.
#  define FONT_NUM   FONT_KEY_LECO_36_BOLD_NUMBERS
#  define FONT_DATE  FONT_KEY_GOTHIC_28_BOLD
#  define FONT_SLOT  FONT_KEY_GOTHIC_24_BOLD
// The one lane that is bigger here than on the rectangles, as it was before: the label
// is read against a 260 px display, and Gothic 18's "00" still measures narrower than
// the Rajdhani 16 it replaces.
#  define FONT_TICK  FONT_KEY_GOTHIC_18
#endif
