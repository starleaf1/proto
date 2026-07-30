#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Dial trigonometry and the vertical layout.
//
// Every coordinate on this face derives from the root layer's bounds, so the
// same code lays out a 144x168 rectangle (flint), a 200x228 one (emery) and a
// 260x260 circle (gabbro). Only dial_boundary() branches on display shape.
// ---------------------------------------------------------------------------

// The face's fixed slots, measured once per paint. Vertical positions stack
// outward from the numeral rather than sitting at fractions of the radius: the
// numeral's height varies with each platform's font size, and both slots have to
// fit in whatever room is left between it and the dial's inner edge without ever
// overlapping it.
typedef struct {
  GRect   bounds;
  GPoint  center;
  int16_t radius;      // min(w, h) / 2 — the scale every other size derives from
  GRect   dial;        // bounds inset by the edge margin
  int16_t tick_len;    // full-length notch; also the depth of the marker band
  GRect   num_box;     // minute numeral
  GRect   date_box;
  GRect   top_box;     // top alert slot
  GRect   bottom_box;  // bottom countdown slot
} Layout;

Layout layout_compute(GRect bounds, GFont num_font, GFont date_font,
                      GFont slot_font, const char *num_text);

// The dial boundary point at angle a (12 o'clock = up, clockwise): a circle on round
// displays, the rectangular perimeter otherwise, so the dial hugs the screen on every
// platform. See the note in geometry.c on what that costs and what it must not cost.
GPoint dial_boundary(GRect dial, GPoint c, int32_t a);

// Move p toward the center along the ray at angle a by d px.
//
// Every marker depth on this face is expressed through this, in pixels — never as a
// fraction of the distance to the boundary. That is what keeps a marker's outer-to-
// inner extent identical at every angle even where the boundary is a rectangle and so
// much further away at the corners.
GPoint step_in(GPoint p, int32_t a, int32_t d);

// Move p perpendicular to the ray at angle a by d px. Signed, so +d and -d give
// the two base corners of a radial triangle.
GPoint step_side(GPoint p, int32_t a, int32_t d);

// A depth measured perpendicular to the dial's boundary, converted into the distance to
// travel along the ray at angle a to cover it.
//
// On a circle the two are the same number: the ray *is* the edge normal. On a rectangle
// they are not, and the gap between them is exactly what a band's thickness looks like.
// A fixed count of pixels along an oblique ray only presents cos(theta) of itself across
// the edge, so an uncorrected band measures a full tick zone at three o'clock and about
// two thirds of one at the corner — a 1.5x swing that reads as the band changing weight
// partway along its length.
//
// `boundary` is the point dial_boundary() returned for this angle, and it is what tells
// this which edge the ray leaves through and therefore which normal applies.
int32_t depth_along_ray(GRect dial, GPoint boundary, int32_t a, int32_t d);

// Clock position of a wall-clock time on the 12-hour dial, in whole degrees
// (0..359, 0 = 12 o'clock). One degree is two minutes.
int deg_of_time(time_t t);

// The tight, centred bounding box of one text row inside `box`, padded a little.
GRect text_plate(GRect box, GFont font, const char *text);

// Fill `r` with the background colour. Invisible over the background itself, so
// the only thing it does is cut away whatever a marker or band had already drawn
// there. Used behind every text row: the row names something specific and the
// marker it covers is still visible either side of it, so text wins the overlap.
void knock_out(GContext *ctx, GRect r);

// A triangle, optionally haloed. `halo` first strokes the outline in the
// background colour, wide and centred on the path, so 1px of it lands outside
// the shape — that ring is what keeps a marker readable where it overlaps a
// band drawn in the same ink.
void draw_tri(GContext *ctx, GPoint p0, GPoint p1, GPoint p2,
              GColor ink, bool filled, int16_t stroke_w, bool halo);

// A triangle with its base on the dial boundary and its apex pointing inward.
// Hollow shapes get a stroke of half_width/2, so the outline scales with the
// marker instead of vanishing to a hairline on the larger displays.
void draw_radial_triangle(GContext *ctx, GRect dial, GPoint c, int32_t a,
                          int16_t depth, int16_t half_width,
                          GColor ink, bool filled, bool halo);
