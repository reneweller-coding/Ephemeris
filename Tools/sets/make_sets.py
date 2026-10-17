"""Ephemeris' factory concerts (17.10.2026): 256 concerts to begin a night with -> Core/src/FactorySetsData.inc.

A concert is a row of pieces that flow into each other (composeConcert). Ephemeris knows five forms of it, and the bank
has each of the five styles in every one of them, and the night sets beside:

- **Concert**: pieces of the style, their keys moving from piece to piece (10 a style);
- **Arc**: the pieces following an arc of tension over the whole concert, quiet at the ends, densest at 60 %
  (compose.concert_arc, 10 a style);
- **Album**: interludes between the long pieces, the darkest in the middle, the last one ethereal (compose.album,
  10 a style);
- **Journey**: from the style to another, each piece at the share already played (compose.morph_to, 12 a style);
- **Long night**: four hours to a night of twelve, along an arc (4 a style);
- **Night sets**: the styles mixed along waves of energy, the pieces overlapping as a DJ mixes them (compose.night_set,
  26, by their length).

Each its length from those its form is played at, a piece's length, now and then the style's own tempo left for the
knob's, its seed; a name of its own (two words of the old synthesizer records' sky), never twice. The composer picks
every piece's sounds as always. The draws are seeded, so the bank is the same every time.

How long each plays is measured by composing it, which the render app does for every row of its --catalog: build, ask
it, and run the script again with its answer -- the lengths go into the table, so the app and Heliacal list the bank at
once.

    python Tools/sets/make_sets.py
    bin/msvc/EphemerisSets.exe --catalog > work/sets_catalog.json
    python Tools/sets/make_sets.py --lengths work/sets_catalog.json
"""
import json
import pathlib
import random
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "Core" / "src" / "FactorySetsData.inc"

STYLES = ["Cosmic", "Doom", "Melodic", "Modern", "Drift"]
NIGHT = "Night sets"
# The forms with how many a style, the lengths they are played at (minutes) and a piece's length.
FORMS = {
    "Concert": dict(count=10, lengths=[30, 45, 60, 90, 120], piece=(10.0, 25.0)),
    "Arc": dict(count=10, lengths=[45, 60, 90, 120, 150], piece=(10.0, 22.0)),
    "Album": dict(count=10, lengths=[40, 50, 60, 75, 90], piece=(12.0, 20.0)),
    "Journey": dict(count=12, lengths=[60, 90, 120, 180], piece=(12.0, 25.0)),
    "Long night": dict(count=4, lengths=[240, 360, 480, 720], piece=(16.0, 30.0)),
}
# A concert has 64 pieces at most and its style draws their lengths: Modern's short pieces fill about nine hours,
# Melodic's about ten and a half -- their long nights end before that.
STYLE_CAP = {"Modern": 480, "Melodic": 600}
NIGHT_LENGTHS = {"One hour or two": [60, 90, 120], "An evening": [180, 240], "All night": [360, 480, 600, 720]}
NIGHT_COUNTS = {"One hour or two": 10, "An evening": 8, "All night": 8}

FIRST = ["Vega", "Sirius", "Helium", "Mirage", "Solstice", "Equinox", "Cygnus", "Quasar", "Pulsar", "Nova", "Polar",
         "Stratosphere", "Sequencer", "Analog", "Crystal", "Silver", "Cobalt", "Magnetic", "Orbital", "Spectral",
         "Distant", "Cosmic", "Solar", "Lunar", "Stellar", "Electric", "Hollow", "Midnight", "Infinite", "Frozen",
         "Ancient", "Velvet", "Tidal", "Northern", "Mirror", "Glass", "Tape", "Phase", "Echo", "Drifting",
         "Shimmering", "Endless", "Neon", "Amber", "Twilight", "Radiant", "Celestial", "Deep"]
SECOND = ["Gate", "Voyage", "Horizon", "Orbit", "Signal", "Current", "Drift", "Machine", "Tide", "Spheres",
          "Meridian", "Observatory", "Transmission", "Cathedral", "Ocean", "Sequence", "Station", "Field", "Arc",
          "Nebula", "Ascension", "Mirror", "Moons", "Rings", "Garden", "Satellite", "Shore", "Wave", "Expanse",
          "Apogee", "Perigee", "Cascade", "Aurora", "Beacon", "Corridor", "Frontier", "Lights", "Pulse", "Vessel",
          "Zenith", "Reverie", "Dawn", "Dusk", "Clouds", "Rain", "Weather", "Delta", "Lantern"]


def length_text(minutes):
    """A concert's length as people say it."""
    if minutes < 60 or minutes % 60:
        return f"{minutes} min"
    h = minutes // 60
    return "1 hour" if h == 1 else f"{h} hours"


def names(rng, count):
    """Two words each, never the same word twice in a name, never a name twice."""
    out, seen = [], set()
    while len(out) < count:
        a, b = rng.choice(FIRST), rng.choice(SECOND)
        if a == b or a + " " + b in seen:
            continue
        seen.add(a + " " + b)
        out.append(a + " " + b)
    return out


def sets():
    """The 256 concerts: (name, section, group, note, text)."""
    rng = random.Random(17102028)
    plan = []
    for style in STYLES:
        for form, f in FORMS.items():
            for k in range(f["count"]):
                plan.append((style, form, min(f["lengths"][k % len(f["lengths"])], STYLE_CAP.get(style, 720))))
    for group, lengths in NIGHT_LENGTHS.items():
        for k in range(NIGHT_COUNTS[group]):
            plan.append((NIGHT, group, lengths[k % len(lengths)]))
    out = []
    for (section, group, minutes), name in zip(plan, names(rng, len(plan))):
        night = section == NIGHT
        style = rng.choice(STYLES) if night else section
        piece = rng.uniform(*(FORMS[group]["piece"] if not night else (12.0, 25.0)))
        knobs = [("compose.style", style), ("compose.concert_minutes", f"{minutes}"),
                 ("compose.piece_minutes", f"{piece:.1f}")]
        note = length_text(minutes)
        if night:
            knobs.append(("compose.night_set", "1"))
            note += ", the styles mixed along waves of energy"
        elif group == "Arc":
            knobs.append(("compose.concert_arc", f"{rng.uniform(0.5, 1.0):.2f}"))
            note += f", {style} along an arc of tension"
        elif group == "Album":
            knobs.append(("compose.album", "1"))
            if rng.random() < 0.5:
                knobs.append(("compose.concert_arc", f"{rng.uniform(0.3, 0.8):.2f}"))
            note += f", {style} as an album"
        elif group == "Journey":
            other = rng.choice([s for s in STYLES if s != style])
            knobs.append(("compose.morph_to", other))
            note += f", from {style} to {other}"
        elif group == "Long night":
            knobs.append(("compose.concert_arc", f"{rng.uniform(0.4, 0.9):.2f}"))
            note += f", {style} through the night"
        else:
            note += f", {style}"
        if rng.random() < 0.15 and not night:
            knobs.append(("compose.style_tempo", "0"))
            knobs.append(("compose.bpm", f"{rng.uniform(96.0, 128.0):.0f}"))
        seed = rng.randrange(1, 2 ** 31)
        text = ["# Ephemeris set", f"seed={seed}", f"minutes={piece:.1f}", f"concert={minutes}"]
        text += [f"param {k}={v}" for k, v in knobs]
        out.append((name, section, group, note, "\n".join(text) + "\n"))
    return out


def c_string(s):
    """A C string literal, a line of the set to a line of the source."""
    lines = s.split("\n")
    parts = [line.replace("\\", "\\\\").replace('"', '\\"') + "\\n" for line in lines[:-1]]
    return "\n".join(f'      "{p}"' for p in parts)


def main():
    rows = sets()
    lengths = {}
    if "--lengths" in sys.argv:   # the render app's catalog: the factory concerts' rows with their lengths
        catalog = json.load(open(sys.argv[sys.argv.index("--lengths") + 1], encoding="utf-8"))
        lengths = {i["name"]: i["length"] for i in catalog["items"] if i["kind"] == "Factory set"}
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// The factory's concerts (FactorySets.h), written by Tools/sets/make_sets.py -- not by hand.\n")
        for name, section, group, note, text in rows:
            f.write(f'    {{ "{name}", "{section}", "{group}", "{note}",\n{c_string(text)},\n'
                    f'      {lengths.get(name, 0.0):.1f} }},\n')
    measured = sum(1 for row in rows if row[0] in lengths)
    print(f"{len(rows)} concerts, {measured} with their lengths -> {OUT}")


if __name__ == "__main__":
    main()
