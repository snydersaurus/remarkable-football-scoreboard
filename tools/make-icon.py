#!/usr/bin/env python3
"""Draw the neutral launcher icon both apps ship with.

The icon an app ships is a committed binary, so it should be something we drew
rather than a league or club mark: an install has no business claiming a team
before anyone picks one, and a drawn football is the only genuinely neutral
answer. Once a team IS picked the backend writes that team's mark over this,
fetched at runtime -- see LogoStore::writeLauncherIcon.

Outline only, never filled. A 600px solid black shape is the worst thing you
can put on an e-ink panel, and at sidebar size it would read as a blob.

    python3 tools/make-icon.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw

SIDE = 600          # AppLoad launcher icons are 600x600
SUPERSAMPLE = 4     # draw big, downscale: PIL has no antialiased stroking


def football(side: int = SIDE) -> Image.Image:
    w = side * SUPERSAMPLE
    img = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    cx = cy = w / 2
    half_w, half_h = w * 0.40, w * 0.235
    stroke = int(w * 0.018)
    ink = (0, 0, 0, 255)

    # A football is a lens -- the overlap of two circles -- which is what gives
    # it genuinely pointed ends. An ellipse just looks like an egg.
    radius = (half_w ** 2 + half_h ** 2) / (2 * half_h)
    offset = radius - half_h

    top, bottom = [], []
    steps = 400
    for i in range(steps + 1):
        x = -half_w + (2 * half_w) * i / steps
        inside = radius * radius - x * x
        if inside <= 0:
            continue
        y = math.sqrt(inside) - offset
        top.append((cx + x, cy - y))
        bottom.append((cx + x, cy + y))
    draw.polygon(top + list(reversed(bottom)), outline=ink, width=stroke)

    # Laces: a centre line with five cross stitches.
    lace_half = half_w * 0.34
    draw.line([(cx - lace_half, cy), (cx + lace_half, cy)], fill=ink, width=stroke)
    for i in range(5):
        lx = cx - lace_half * 0.72 + (lace_half * 1.44) * i / 4
        draw.line([(lx, cy - half_h * 0.30), (lx, cy + half_h * 0.30)],
                  fill=ink, width=stroke)

    return img.resize((side, side), Image.LANCZOS)


def main() -> int:
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    icon = football()
    for league in ("cfb", "nfl"):
        path = os.path.join(here, "appload-native", league, "icon.png")
        icon.save(path)
        print("wrote %s (%d bytes)" % (os.path.relpath(path, here),
                                       os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
