"""Ephemeris -- the logo: the orrery of the Rack page, drawn per size.

    python Deploy/make_icon.py        -> Deploy/ephemeris.ico (16 .. 256 px), ephemeris.png, ephemeris_512.png, ephemeris.svg

The same drawing is the plugin's header (PluginEditor.cpp, drawLogo), the manual's cover (the SVG, sharp in
print, from the 512-pixel drawing) and the Quest's panel (main.cpp, addLogo, in points of light).

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


def svg(size=256):
    """The large drawing (five orbits) as vectors."""
    s, c = float(size), size / 2.0
    hexa = lambda col: "#%02x%02x%02x" % col[:3]
    sun, width = s * 0.16, max(1.0, s / 64.0)
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">' % (size, size, size, size),
           '<rect x="0" y="0" width="%g" height="%g" rx="%g" fill="%s"/>' % (s, s, s / 5, hexa(BACK))]
    for k in range(5):
        r = sun + (s * 0.42 - sun) * (k + 1) / 5
        out.append('<circle cx="%g" cy="%g" r="%.3f" fill="none" stroke="%s" stroke-opacity="%.3f" stroke-width="%g"/>'
                   % (c, c, r, hexa(ORBIT), ORBIT[3] / 255.0, width))
        a = -math.pi / 2 + 2.1 * (k + 1)
        pr = max(s * 0.035, width * 2.2)
        out.append('<circle cx="%.3f" cy="%.3f" r="%.3f" fill="%s"/>' % (c + r * math.cos(a), c + r * math.sin(a), pr, hexa(PLANETS[k])))
    out.append('<circle cx="%g" cy="%g" r="%.3f" fill="%s"/>' % (c, c, sun, hexa(SUN)))
    out.append('</svg>')
    return "\n".join(out) + "\n"


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [draw(n) for n in sizes]
    images[-1].save(ROOT / "ephemeris.ico", sizes=[(n, n) for n in sizes], append_images=images[:-1])
    images[-1].save(ROOT / "ephemeris.png")
    draw(512).save(ROOT / "ephemeris_512.png")
    (ROOT / "ephemeris.svg").write_text(svg(), encoding="utf-8")
    print("wrote", ROOT / "ephemeris.ico", "and the png and svg")


if __name__ == "__main__":
    main()
