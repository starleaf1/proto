#!/usr/bin/env python3
"""Draw Proto Digital's launcher icon into resources/images/.

The one image resource on this face, and the only glyph on it not drawn in C:
the firmware renders the watchface selector, not the app, so this shape cannot
be a few calls in an update proc like every other mark the face makes.

Two files, because the selector runs an icon in a different mode per platform
(see guides/app-resources/images, "Menu Icon in the Launcher"):

  flint   inversion mode, which a watchface only gets with memoryFormat 1Bit.
          The icon inverts against the row it lands on, so it is opaque black
          on white here and legible either way up. Transparency would be read
          as white.
  colour  non-inverting and never re-tinted, so it has to carry its own
          contrast: the selector paints the highlighted row a saturated
          magenta, and measured there the rail, the notches and the red rule
          all but vanish against it. So the colour icon sits on an opaque
          plate in COL_BG, which makes it a thumbnail of the face - and costs
          nothing on the unhighlighted rows, which are white already.

25x25 is the SDK's limit; anything larger is rejected outright.

Both say the same thing the face does, in the face's own vocabulary: the rail
down the left edge with its hour notches, a running band, a task as a blunt
wedge, and now pinned a quarter of the way down - a red rule where there is
colour, a wedge reaching out at the rail on flint.
"""
import os
from PIL import Image, ImageDraw

N = 25

INK  = (0x00, 0x00, 0x00, 0xFF)
BAND = (0x00, 0xAA, 0xFF, 0xFF)   # GColorVividCerulean - COL_BAND
NOW  = (0xFF, 0x00, 0x00, 0xFF)   # GColorRed           - COL_INDEX
TASK = (0xFF, 0xAA, 0x00, 0xFF)   # GColorChromeYellow  - COL_TASK_SOON
WHITE = (0xFF, 0xFF, 0xFF, 0xFF)

# Three lanes across, and nothing shares one: the rail, its notch lane, and the
# markers. On flint every ink folds to black, so a shape that touched its
# neighbour would read as one shape with it.
RAIL_X = 2
NOTCH_X0, NOTCH_X1 = 3, 5
MARK_X0 = 8
TOP, BOT = 1, 23
NOW_Y = 6                          # the quarter mark: one hour back of four


def draw(colour):
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0) if colour else WHITE)
    d = ImageDraw.Draw(im)
    if colour:
        d.rounded_rectangle([0, 0, N - 1, N - 1], radius=4, fill=WHITE)
    band = BAND if colour else INK
    task = TASK if colour else INK

    d.rectangle([RAIL_X, TOP, RAIL_X, BOT], fill=INK)
    for y in (2, 11, 20):                                  # hour notches
        d.rectangle([NOTCH_X0, y, NOTCH_X1, y + 1], fill=INK)
    d.rectangle([MARK_X0, 10, 20, 13], fill=band)          # a running band
    d.polygon([(MARK_X0, 17), (MARK_X0, 21), (13, 19)], fill=task)

    if colour:
        # One pixel short of the plate at each end: struck through the strip,
        # not run off the edge of it.
        d.rectangle([1, NOW_Y, 13, NOW_Y + 1], fill=NOW)
    else:
        d.polygon([(11, NOW_Y - 3), (11, NOW_Y + 3), (7, NOW_Y)], fill=INK)
    return im


here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, os.pardir, "resources", "images")
os.makedirs(out, exist_ok=True)

# flint wants only black and white in the file, and no alpha to guess at.
bw = Image.new("RGB", (N, N), (255, 255, 255))
bw.paste(draw(False), (0, 0), draw(False))
bw.convert("1").save(os.path.join(out, "menu_icon_bw.png"))
draw(True).save(os.path.join(out, "menu_icon_color.png"))
print("wrote menu_icon_bw.png and menu_icon_color.png")
