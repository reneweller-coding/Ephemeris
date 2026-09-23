# Ephemeris

A generator for long Berlin-school pieces and whole concerts: interlocking sequencer rows on analogue
modelled voices, the gestures of a player on filters, echo and transposition, a synthesised tape
keyboard, string machine, pads and drones, lead solos, tape echo and spring. Standalone and VST3 on
Windows, native on Meta Quest 2. Everything is synthesised; there are no samples.

The plan, the musical specification and the literature behind each building block are in
[docs/PLAN.md](docs/PLAN.md) (German).

**Status:** Phases 0 to 5 in their first form: the rack with a transposer row, modular voices, the
lead and the drone, a synthesised tape keyboard (choir, strings, flute and the machine), a string machine,
the atmosphere, tape echo with springs, hall, drums for the styles that have them; five style profiles,
a form grammar and a composer that writes whole pieces and concerts; rerolling any part on its own and
`.ephset` files; and a VST3 and standalone. See `docs/PLAN.md` for the state and the measurements.

![The panel](docs/screenshot.png)

## Build

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
build/Plugin/Ephemeris_artefacts/Release/Standalone/Ephemeris.exe
```

## Try it

```bash
build/Tools/render/Release/eph_render.exe --minutes 14 --seed 11 --set "compose.style=Cosmic" --out out/piece.wav --midi out/piece.mid
build/Tools/render/Release/eph_render.exe --concert 60 --seed 3 --set "compose.style=Drift" --out out/night.wav
build/Tools/render/Release/eph_render.exe --set-file out/piece.ephset --reroll lead --save-set out/piece2.ephset
build/Tools/render/Release/eph_render.exe --list
```

## Licence

AGPL-3.0, as Noctuary and Phosphene. Some modules are copied from
[Phosphene](../PsytranceGenerator) and name their origin and commit in their file header.
