#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// The whole palette and both font choices, in one place.
//
// Two hues, two tints each, and red spent on exactly one thing. Cool means
// time-bound and passive; warm means it wants something from you. That single
// rule covers every marker state without needing a legend.
//
// flint has one ink, so hue is unavailable there and every COL_* below folds to
// black. The prominence that colour carries on emery and gabbro is carried on
// flint by *shape* and by *position*: a running band is twice the depth of an
// upcoming one, a task is a wedge and an appointment is an arc, and the two
// hands differ in width rather than in colour. See dial.c.
// ---------------------------------------------------------------------------

#define COL_BG          GColorWhite
#define COL_INK         GColorBlack

// Appointment bands — one hue for both states.
//
// Prominence is carried by depth, which works identically on all three
// displays: a running band fills the ring and an upcoming one fills its outer
// half. A second, paler tint would be redundant where there is colour and
// absent where there is not.
#define COL_BAND        PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack)

// The countdown wears the band too, since it is the band's own reading: cerulean
// digits on white before an appointment starts, white on a cerulean box while it
// runs. Below the 4.5:1 this palette asks of text elsewhere — white on cerulean
// measures 2.6:1 — and chosen on sight rather than by that rule. On flint the band
// is black and this is plain ink and its inversion.

// "Now" — which on this face is the hour hand, and nothing else.
//
// The digital face spends red on a rule struck across its strip. Here there is
// no separate now mark to spend it on, because the hand *is* the mark: the ring
// runs at thirty degrees to the hour, which is the hour hand's own rate, so the
// hand points at the present position on the timeline by construction. Red goes
// on the hand for the same reason it went on the rule — it is the one element
// everything else is measured against, and it has to stay findable where it
// crosses a cerulean band, a black tick and an amber wedge in turn.
//
// On flint it folds to black and the hand is told from the minute hand by being
// broader and shorter, which is the distinction that display can actually make.
#define COL_INDEX       PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)

// Point-in-time markers: amber until due, orange once overdue.
#define COL_TASK_SOON   PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack)
#define COL_TASK_LATE   PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack)

// The notification band. Red for the companion being gone — the only *state* on
// this face that should ever alarm you. GColorGreen/GColorYellow stay unused: on
// white they measure 1.4:1 and 1.1:1, so a free-floating shape in either is
// invisible.
//
// COL_WARN is Windsor tan and not Chrome yellow, because this row is text.
// Chrome yellow is 1.9:1, which a solid wedge on the ring survives and two
// glyphs of "28%" beside a battery outline do not; Windsor tan is the same warm
// family at 4.1:1.
#define COL_ALERT       PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define COL_WARN        PBL_IF_COLOR_ELSE(GColorWindsorTan, GColorBlack)

// Two text roles, not the digital face's four, and both of them the firmware's own.
//
// There is no clock numeral here — the hands are the clock — so the digital face's
// LECO ladder is not needed, and with it the constraint that sizes every other row
// over there. The dial carries ticks rather than numerals, so there is no hour-label
// lane either. What is left is the day-of-month, which is the largest thing the disc
// holds, and one slot size for the countdown and the two notification pairs.
//
// They were Rajdhani SemiBold, a subsetted TTF at a size per platform, and this face
// felt the cost of that harder than the digital one did: every string here is between
// 14 and 28 px, which is the whole range in which the SDK's build-time rasteriser has
// to round a condensed semibold stem to one pixel or two. It rounded down. A system
// font is a hand-fitted 1-bit bitmap per size, so the stem is the width it was drawn
// at and the glyph is on the pixel grid by construction.
//
// Rajdhani being condensed *was* load-bearing — a Gothic at the same nominal size is
// about a quarter wider, and this disc is sized from the widest string in it. What
// pays for it is that a Gothic is also shorter for the same reading: measured, "MON 22"
// is 50 x 18 in GOTHIC_18_BOLD against 62 x 20 in the DATE_20 it replaces on flint, so
// the row is narrower *and* shorter and layout_compute solves a smaller disc, not a
// larger one. The numbers, all measured with graphics_text_layout_get_content_size:
//
//                     "MON 22"     "+0:00"    "000 KM"     disc
//   was  flint      62 x 20      34 x 16     48 x 16      32 px
//   now  flint      50 x 18      38 x 18     48 x 18      34 px
//   was  emery      86 x 28      47 x 22     69 x 22      44 px
//   now  emery      67 x 28      44 x 24     54 x 24      44 px
//        gabbro     67 x 28      44 x 24     54 x 24      58 px
//
// flint's slot row measures at 18 and not 14 because both of its rows are the date's
// size — see below. gabbro measures identically to emery and still solves a disc
// half again as large, because it has one more row in it.
//
// A system font's number is its pixel size, and the content height comes back equal to
// it exactly — which the TTF resources' em numbers never did.
// One size for both rows, on all three displays. emery and gabbro kept the date a
// rung above the slot at 28 px, and it read as the loudest thing on the dial for the
// one row that never changes; at 24 the disc came out 42 px on emery rather than 44.
// gabbro's stayed at 57, because its disc is sized by the notification row, not the
// date. flint came to it first and for a harder reason. It is the display where the
// disc's radius is dearest per row:
// it is solved from these two rows, it is drawn over the hands, and the hour hand is
// 60 px long to begin with. Measured, a 24 px date puts the disc at 37 and leaves 23
// px of hour hand outside it — a stub — where 18 px puts it at 34 and leaves 26. The
// difference between the two rows was worth three pixels of radius on emery and
// gabbro and is not worth them here, and at 18 px bold the date is not competing with
// the countdown for attention anyway: it is the row that never changes.
#if defined(PBL_PLATFORM_FLINT)
#  define FONT_DATE  FONT_KEY_GOTHIC_18_BOLD
#  define FONT_SLOT  FONT_KEY_GOTHIC_18_BOLD
#elif defined(PBL_PLATFORM_EMERY)
#  define FONT_DATE  FONT_KEY_GOTHIC_24_BOLD
#  define FONT_SLOT  FONT_KEY_GOTHIC_24_BOLD
#else   // gabbro
// The same pair as emery, where the TTF table had this display a size below it. That
// gap was the condensed face's: DATE_26 was what fit the chord the date sits on here,
// and GOTHIC_28_BOLD measures narrower than DATE_26 did.
#  define FONT_DATE  FONT_KEY_GOTHIC_24_BOLD
#  define FONT_SLOT  FONT_KEY_GOTHIC_24_BOLD
#endif
