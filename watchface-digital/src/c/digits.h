#pragma once
#include <pebble.h>

// The clock's numerals, drawn rather than set in a font.
//
// Every glyph is a 7-cell-high lattice of square cells, `cell` px on a side, and a
// cell is empty, full, or half-filled by a triangle cut at 45 degrees. So every edge
// is horizontal, vertical or exactly diagonal, and every corner lands on a whole
// multiple of `cell` — which is what makes the diagonals a clean one-pixel staircase on
// flint with no antialiasing, and identical on colour. Nothing is rasterised from an
// outline, so there is nothing for a rasteriser to round away.
//
// Digits and ':' only, and tabular: every digit is the same width. Anything else in
// `text` is skipped without advancing.

// The ink's extent: exactly the box digits_draw() fills, with no slack above or below.
GSize digits_size(const char *text, int16_t cell);

// Draw `text` with its ink's top-left corner at `origin`.
void digits_draw(GContext *ctx, const char *text, GPoint origin, int16_t cell,
                 GColor ink);
