#include "digits.h"
#include <string.h>

#define ROWS 7

// One cell of space between glyphs, so the gaps are on the same lattice as the strokes.
#define GAP_CELLS 1

// The glyphs, a row of cells per string, read top to bottom.
//
//   '#' full      ' ' empty
//   'J' a triangle filling the lower right, like the corner of a J
//   'L' lower left     'r' upper left     '7' upper right
//
// Strokes are one cell. The outer corners of the round digits are cut by a whole cell,
// and 1, 2, 3, 4 and 7 carry their diagonals as a run of J-r pairs: each pair is a
// parallelogram one cell wide, and the next row's pair starts a cell to the left, so
// the run joins up into one 45-degree stroke.
//
// 6 and 9 are each other turned through half a circle; keep them so.
static const char GLYPHS[][ROWS][6] = {
  { "J###L", "#   #", "#   #", "#   #", "#   #", "#   #", "7###r" },   // 0
  { "  J# ", " Jr# ", "   # ", "   # ", "   # ", "   # ", "   # " },   // 1
  { "J###L", "    #", "   Jr", "  Jr ", " Jr  ", "Jr   ", "#####" },   // 2
  { "#####", "   Jr", "  Jr ", "  ##L", "    #", "    #", "####r" },   // 3
  { "   J#", "  Jr#", " Jr #", "Jr  #", "#####", "    #", "    #" },   // 4
  { "#####", "#    ", "#    ", "####L", "    #", "    #", "####r" },   // 5
  { "J####", "#    ", "#    ", "####L", "#   #", "#   #", "7###r" },   // 6
  { "#####", "    #", "   Jr", "  Jr ", " Jr  ", " #   ", " #   " },   // 7
  { "J###L", "#   #", "#   #", "#####", "#   #", "#   #", "7###r" },   // 8
  { "J###L", "#   #", "#   #", "7####", "    #", "    #", "####r" },   // 9
  { " ",     " ",     "#",     " ",     "#",     " ",     " "     },   // :
};

static int glyph_index(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c == ':') return 10;
  return -1;
}

static int16_t glyph_cols(int g) {
  return (int16_t)strlen(GLYPHS[g][0]);
}

GSize digits_size(const char *text, int16_t cell) {
  int16_t cols = 0;
  int n = 0;
  for (const char *p = text; *p; p++) {
    int g = glyph_index(*p);
    if (g < 0) continue;
    cols += glyph_cols(g);
    n++;
  }
  if (n > 1) cols += (int16_t)((n - 1) * GAP_CELLS);
  return GSize(cols * cell, n ? ROWS * cell : 0);
}

// One cell's triangle, a row of pixels at a time.
//
// A pixel is ink when its centre is inside the triangle, and a centre lying exactly on
// the hypotenuse counts as inside — and with whole-pixel corners a 45-degree line passes
// through a pixel centre on every row, so the rule is not academic. Inside on every
// triangle, rather than inside on one side and outside on the other, so that each
// chamfer cuts the same amount from whichever corner it is on and a mirrored glyph comes
// out mirrored. It also puts back the weight a diagonal loses to its angle: a run of J-r
// pairs is one cell wide measured across the row, so its true thickness is only seven
// tenths of a stem's, and the shared pixel on each edge makes up part of the difference.
static void fill_tri(GContext *ctx, char kind, int16_t x, int16_t y, int16_t s) {
  for (int16_t k = 0; k < s; k++) {
    int16_t i0, i1;   // [i0, i1) along row k
    switch (kind) {
      case 'J': i0 = s - 1 - k; i1 = s;     break;
      case 'r': i0 = 0;         i1 = s - k; break;
      case 'L': i0 = 0;         i1 = k + 1; break;
      case '7': i0 = k;         i1 = s;     break;
      default:  return;
    }
    graphics_fill_rect(ctx, GRect(x + i0, y + k, i1 - i0, 1), 0, GCornerNone);
  }
}

void digits_draw(GContext *ctx, const char *text, GPoint origin, int16_t cell,
                 GColor ink) {
  graphics_context_set_fill_color(ctx, ink);
  int16_t x0 = origin.x;
  for (const char *p = text; *p; p++) {
    int g = glyph_index(*p);
    if (g < 0) continue;
    int16_t cols = glyph_cols(g);
    for (int16_t r = 0; r < ROWS; r++) {
      int16_t y = origin.y + r * cell;
      // A run of full cells is one rect, not one per cell.
      int16_t c = 0;
      while (c < cols) {
        char k = GLYPHS[g][r][c];
        if (k == '#') {
          int16_t c1 = c;
          while (c1 < cols && GLYPHS[g][r][c1] == '#') c1++;
          graphics_fill_rect(ctx, GRect(x0 + c * cell, y, (c1 - c) * cell, cell),
                             0, GCornerNone);
          c = c1;
        } else {
          if (k != ' ') fill_tri(ctx, k, x0 + c * cell, y, cell);
          c++;
        }
      }
    }
    x0 += (cols + GAP_CELLS) * cell;
  }
}
