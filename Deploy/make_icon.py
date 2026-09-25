"""Ephemeris -- the Windows icon: the orrery of the Rack page, drawn per size.

    python Deploy/make_icon.py        -> Deploy/ephemeris.ico (16 .. 256 px)

A warm sun in the middle and orbits around it, each with its planet; at 16 and 24 pixels only the sun and two
orbits, since more would turn into a speckle. The .ico stays committed: the release build must not depend
on Python being installed.
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
SUN = (232, 178, 92, 255)
ORBIT = (201, 164, 92, 150)
PLANETS = [(224, 164, 88, 255), (217, 130, 91, 255), (159, 191, 111, 255), (111, 184, 174, 255), (111, 143, 184, 255)]
BACK = (21, 23, 28, 255)


def draw(size):
    s = size * 4                                   # drawn large, scaled down smooth
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=s // 5, fill=BACK)
    c = s / 2
    orbits = 2 if size <= 24 else (3 if size <= 48 else 5)
    sun = s * (0.16 if size > 24 else 0.2)
    width = max(4, s // 64)
    for k in range(orbits):
        r = sun + (s * 0.42 - sun) * (k + 1) / orbits
        d.ellipse((c - r, c - r, c + r, c + r), outline=ORBIT, width=width)
        a = -math.pi / 2 + 2.1 * (k + 1)            # the planets spread round their orbits
        pr = max(s * 0.035, width * 2.2)
        x, y = c + r * math.cos(a), c + r * math.sin(a)
        d.ellipse((x - pr, y - pr, x + pr, y + pr), fill=PLANETS[k % len(PLANETS)])
    d.ellipse((c - sun, c - sun, c + sun, c + sun), fill=SUN)
    return im.resize((size, size), Image.LANCZOS)


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [draw(n) for n in sizes]
    images[-1].save(ROOT / "ephemeris.ico", sizes=[(n, n) for n in sizes], append_images=images[:-1])
    images[-1].save(ROOT / "ephemeris.png")
    print("wrote", ROOT / "ephemeris.ico")


if __name__ == "__main__":
    main()
