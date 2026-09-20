#!/usr/bin/env python3
"""Draw Proto Analog's launcher icon into resources/images/.

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
          magenta, and measured there the dial and both hands all but vanish
          against it. So the colour icon sits on an opaque plate in COL_BG -
          a disc, this face's plate being the dial itself - which makes it a
          thumbnail of the face and costs nothing on the unhighlighted rows,
          which are white already.

25x25 is the SDK's limit; anything larger is rejected outright.

The dial, two hands told apart by width as they are on the face, and one band
on the rim. The face's rail spans half the ring and carries the band on it,
and at 25 px across a 2 px rail under a 3 px band is one shape with it - a
quarter of the ring's depth is not a difference this many pixels can hold. So
the icon states the band alone: the rim is where time is marked. The band sits
in the lower left because the hands are read first and must not be crowded.
"""
import math
import os
from PIL import Image, ImageDraw

N = 25
CX = CY = 12
R = 11

INK   = (0x00, 0x00, 0x00, 0xFF)
BAND  = (0x00, 0xAA, 0xFF, 0xFF)   # GColorVividCerulean - COL_BAND
WHITE = (0xFF, 0xFF, 0xFF, 0xFF)

BAND_A0, BAND_A1, BAND_DEPTH = 108, 173, 3     # degrees, 0 at three o'clock
HOUR_DEG, MINUTE_DEG = 0, 90                   # twelve fifteen


def band_sector(im, a0, a1, depth, col):
    """An annular sector, not a thick arc: square radial ends and a clean inner
    edge, which is what fill_ring_band() gets from graphics_fill_radial."""
    mask = Image.new("L", (N, N), 0)
    md = ImageDraw.Draw(mask)
    md.pieslice([CX - R, CY - R, CX + R, CY + R], a0, a1, fill=255)
    ri = R - depth
    md.ellipse([CX - ri, CY - ri, CX + ri, CY + ri], fill=0)
    im.paste(Image.new("RGBA", (N, N), col), (0, 0), mask)


def hand(d, deg, length, half):
    """Tapered, from the pivot, at a clock angle - zero is twelve, clockwise."""
    a = math.radians(deg - 90)
    tx, ty = CX + length * math.cos(a), CY + length * math.sin(a)
    nx, ny = -math.sin(a), math.cos(a)
    tip = max(half * 0.7, 0.5)
    d.polygon([(CX + nx * half, CY + ny * half),
               (tx + nx * tip,  ty + ny * tip),
               (tx - nx * tip,  ty - ny * tip),
               (CX - nx * half, CY - ny * half)], fill=INK)


def draw(colour):
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0) if colour else WHITE)
    if colour:
        ImageDraw.Draw(im).ellipse([CX - R - 1, CY - R - 1,
                                    CX + R + 1, CY + R + 1], fill=WHITE)
    ImageDraw.Draw(im).ellipse([CX - R, CY - R, CX + R, CY + R],
                               outline=INK, width=1)
    band_sector(im, BAND_A0, BAND_A1, BAND_DEPTH, BAND if colour else INK)
    d = ImageDraw.Draw(im)
    hand(d, MINUTE_DEG, 9.0, 0.5)              # minute: the longer, and thin
    hand(d, HOUR_DEG,   5.5, 1.5)              # hour: short and wide
    d.ellipse([CX - 1.5, CY - 1.5, CX + 1.5, CY + 1.5], fill=INK)
    return im


here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, os.pardir, "resources", "images")
os.makedirs(out, exist_ok=True)

bw = Image.new("RGB", (N, N), (255, 255, 255))
bw.paste(draw(False), (0, 0), draw(False))
bw.convert("1").save(os.path.join(out, "menu_icon_bw.png"))
draw(True).save(os.path.join(out, "menu_icon_color.png"))
print("wrote menu_icon_bw.png and menu_icon_color.png")
