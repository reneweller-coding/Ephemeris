"""Ephemeris -- the picture GitHub shows when the project is linked: docs/social-preview.png (1280 x 640).

    python Tools/manual/make_preview.py

The logo, the name, one sentence, and the rack (docs/screenshot.png, from make_shots.py). GitHub takes it by
hand: Settings, Social preview.
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
W, H = 1280, 640


def font(name, size):
    try:
        return ImageFont.truetype(os.path.join("C:/Windows/Fonts", name), size)
    except OSError:
        return ImageFont.load_default()


img = Image.new("RGB", (W, H), (14, 17, 23))
d = ImageDraw.Draw(img)
shot = Image.open(os.path.join(ROOT, "docs", "screenshot.png")).convert("RGB")
crop = shot.crop((0, 0, 1180, 760)).resize((780, 502), Image.LANCZOS)
x0, y0 = W - 780 - 30, (H - 502) // 2
img.paste(crop, (x0, y0))
d.rectangle([x0 - 1, y0 - 1, x0 + 780, y0 + 502], outline=(44, 53, 68), width=2)
logo = Image.open(os.path.join(ROOT, "Deploy", "ephemeris_512.png")).convert("RGBA").resize((150, 150), Image.LANCZOS)
img.paste(logo, (60, 80), logo)
d.text((60, 252), "EPHEMERIS", font=font("segoeuib.ttf", 52), fill=(232, 169, 72))
y = 330
for line in ("A generator of Berlin School", "music: pieces, concerts and", "night sets, composed and", "played by the program."):
    d.text((62, y), line, font=font("segoeui.ttf", 26), fill=(232, 223, 204))
    y += 36
for line in ("VST3 and standalone for Windows,", "and an app for Meta Quest"):
    d.text((62, y + 18), line, font=font("segoeui.ttf", 19), fill=(142, 148, 159))
    y += 26
out = os.path.join(ROOT, "docs", "social-preview.png")
img.save(out)
print("wrote", os.path.relpath(out, ROOT), img.size)
