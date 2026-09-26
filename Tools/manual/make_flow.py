"""Ephemeris -- the signal flow as a picture: docs/flow.png, for the manual and the project's front page.

    python Tools/manual/make_flow.py

Every unit a box in the colour of its family (the panel's: amber sources, copper filters, sage envelopes and
modulation, teal what moves by itself, steel blue the rooms and the mix), the groups as frames, the buses as arrows.
Drawn with Pillow at twice the size it is shown at, so it stays sharp on a screen and in the PDF.
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
S = 2                                   # drawn at twice the size
W, H = 1400, 820                        # in the size it is shown at

BG = (14, 17, 23)
BOX = (22, 27, 36)
INK = (232, 223, 204)
DIM = (142, 148, 159)
SOURCE, FILTER, ENVELOPE, MOTION, SPACE = (224, 164, 88), (217, 130, 91), (159, 191, 111), (111, 184, 174), (111, 143, 184)
AMBER = (232, 169, 72)


def font(size, bold=False):
    names = ["segoeuib.ttf" if bold else "segoeui.ttf", "arialbd.ttf" if bold else "arial.ttf", "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"]
    for n in names:
        for d in ("C:/Windows/Fonts", "/usr/share/fonts/truetype/dejavu"):
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, size * S)
    return ImageFont.load_default()


img = Image.new("RGB", (W * S, H * S), BG)
d = ImageDraw.Draw(img)
F_BOX, F_SMALL, F_TITLE, F_NOTE = font(13), font(11), font(13, True), font(12)


def wrap(text, f, width):
    lines = []
    for para in text.split("\n"):
        line = ""
        for word in para.split(" "):
            test = (line + " " + word).strip()
            if d.textlength(test, font=f) <= width * S:
                line = test
            else:
                lines.append(line)
                line = word
        lines.append(line)
    return lines


def box(x, y, w, h, text, colour, f=None, centre=True):
    f = f or F_BOX
    d.rounded_rectangle([x * S, y * S, (x + w) * S, (y + h) * S], radius=7 * S, fill=BOX, outline=colour, width=2 * S)
    lines = wrap(text, f, w - 16)
    lh = f.size * 1.3
    ty = y * S + (h * S - lh * len(lines)) / 2
    for ln in lines:
        tw = d.textlength(ln, font=f)
        tx = (x * S + (w * S - tw) / 2) if centre else (x + 10) * S
        d.text((tx, ty), ln, font=f, fill=INK)
        ty += lh


def group(x, y, w, h, title, colour):
    d.rounded_rectangle([x * S, y * S, (x + w) * S, (y + h) * S], radius=10 * S, outline=colour, width=2 * S)
    d.text(((x + 14) * S, (y + 8) * S), title, font=F_TITLE, fill=colour)


def arrow(points, colour, dashed=False, head=True):
    pts = [(px * S, py * S) for px, py in points]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if dashed:
            n = max(1, int(((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5 / (12 * S)))
            for i in range(0, n, 2):
                a, b = i / n, min(1.0, (i + 1) / n)
                d.line([(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a), (x0 + (x1 - x0) * b, y0 + (y1 - y0) * b)], fill=colour, width=2 * S)
        else:
            d.line([(x0, y0), (x1, y1)], fill=colour, width=2 * S)
    if not head:
        return
    (x0, y0), (x1, y1) = pts[-2], pts[-1]
    import math
    ang = math.atan2(y1 - y0, x1 - x0)
    size = 8 * S
    d.polygon([(x1, y1), (x1 - size * math.cos(ang - 0.45), y1 - size * math.sin(ang - 0.45)),
               (x1 - size * math.cos(ang + 0.45), y1 - size * math.sin(ang + 0.45))], fill=colour)


def note(x, y, text, colour=DIM, f=None):
    d.text((x * S, y * S), text, font=f or F_NOTE, fill=colour)


# The inputs: what plays the instruments.
ins = [("Composer: form, harmony, the sounds and the mix of every piece", MOTION),
       ("Rack: eight sequencer rows and the transposer", SOURCE),
       ("The hands: gestures on the knobs", MOTION),
       ("Perform: filter hand, transposition, hold, echo throw; MIDI", AMBER),
       ("Score cues out (OSC)", SPACE)]
x = 20
for i, (t, c) in enumerate(ins):
    box(x, 16, 262, 50, t, c, F_SMALL)
    x += 274

# The voice bank.
group(20, 92, 660, 430, "VOICE BANK  x10  --  the rows, the lead, the drone, in SIMD lanes at twice the rate", SOURCE)
box(36, 124, 628, 62, "VCO 1 + VCO 2: Analog, Moog 921, Prophet-5, Oberheim SEM, ARP 2600, E-mu Modular -- saw into pulse, "
    "sync, cross mod, drift and jitter per model; or a wavetable (51 tables)", SOURCE)
box(36, 200, 628, 36, "Mixer: drive into an antiderivative-antialiased sigmoid", SOURCE)
box(36, 250, 628, 62, "Filter: ten circuit models -- Moog ladder, Prophet, Juno, Oberheim SEM, Xpander, diode ladder, Korg35, "
    "Polivoks, EDP Wasp, comb -- solved by Newton every sample; filter FM", FILTER)
box(36, 326, 628, 36, "Half-band down to the base rate  ->  DC blocker  ->  VCA", SOURCE)
box(36, 376, 628, 62, "Modulation: amp and filter ADSR, a mod envelope, four LFOs (synced to the bar or free, retriggered, "
    "fading in), an eight-slot matrix; glide, accents, the rows' modulation lanes", ENVELOPE)
box(36, 452, 628, 54, "The sounds: 1024 factory presets per synth, set by the composer on the knobs as each piece begins", MOTION)
for y0, y1 in ((186, 200), (236, 250), (312, 326), (362, 376)):
    arrow([(350, y0), (350, y1)], DIM)

# The other sources.
group(20, 540, 660, 262, "MORE SOURCES", SOURCE)
box(36, 572, 628, 36, "Pad synth: 8 keys x 2 wavetable oscillators  ->  the ten filter models  ->  ensemble", SOURCE)
box(36, 618, 628, 36, "Tape keys: choir, strings, flute -- and the machine: wow, flutter, sag, the tape's end", SOURCE)
box(36, 664, 628, 36, "String machine: divide-down registers, the registration morphing, ensemble, phaser", SOURCE)
box(36, 710, 306, 36, "Drums (by style)", SOURCE)
box(358, 710, 306, 36, "Atmosphere: wind, sweeps, bleeps, grains", SOURCE)
note(40, 760, "every source also on its own modulation: envelopes, LFOs, the matrix")

# Inputs into the sources.
arrow([(151, 66), (151, 92)], MOTION)
arrow([(425, 66), (425, 92)], SOURCE)
arrow([(699, 66), (699, 74), (560, 74), (560, 92)], MOTION, dashed=True)
arrow([(973, 66), (973, 82), (630, 82), (630, 92)], AMBER, dashed=True)

# The strips.
group(720, 92, 660, 130, "CHANNEL STRIPS  --  one per source", SPACE)
box(736, 124, 628, 36, "low cut  ->  distance (level, highs, hall)  ->  punch  ->  pan and width  ->  sends", SPACE)
box(736, 170, 628, 36, "ducking in six bands: rows -> pads -> atmosphere; a resonance tamer on the rows", MOTION)
arrow([(680, 680), (700, 680), (700, 300)], SOURCE, head=False)
arrow([(680, 300), (700, 300), (700, 142), (736, 142)], SOURCE)

# The rooms.
group(720, 240, 660, 250, "ROOMS  --  the sends", SPACE)
rooms = ["Tape echo or bucket-brigade delay, and the springs", "Echo 2: the counter rows' own time",
         "Early reflections (send A)", "Blend room: a short plate that feeds the hall",
         "Hall: an eight-line FDN, or the plate", "Shimmer hall (send D)"]
for i, t in enumerate(rooms):
    box(736 + (i % 2) * 318, 272 + (i // 2) * 64, 310, 52, t, SPACE, F_SMALL)
arrow([(1050, 222), (1050, 240)], SPACE, dashed=True)
note(1062, 224, "sends", SPACE, F_SMALL)

# The master.
group(720, 508, 660, 208, "MASTER", SPACE)
box(736, 540, 628, 90, "DC and subsonics out (20 Hz)  ->  mono under 100 Hz  ->  width above 300 Hz, guarded  ->  "
    "the band under 80 Hz limited on its own  ->  a gentle compressor (1.5 : 1)  ->  soft clip  ->  true-peak limiter "
    "(-1 dBTP)", SPACE)
box(736, 642, 628, 58, "Level: yours, the style's, and the piece's loudness correction -- its peak measured and brought "
    "to its style's target before it plays", AMBER)
arrow([(1370, 206), (1392, 206), (1392, 560), (1364, 560)], SPACE)
note(1300, 222, "dry", SPACE, F_SMALL)
arrow([(1050, 490), (1050, 508)], SPACE)
note(1062, 492, "returns", SPACE, F_SMALL)
box(960, 734, 404, 40, "Out: stereo -- WAV, stems, MIDI with the gestures", SPACE)
arrow([(1162, 716), (1162, 734)], SPACE)
note(720, 786, "Deterministic throughout: a piece renders offline to the bit as it plays, for any block size.", DIM)

out = os.path.join(ROOT, "docs", "flow.png")
img.save(out)
print("wrote", os.path.relpath(out, ROOT), img.size)
