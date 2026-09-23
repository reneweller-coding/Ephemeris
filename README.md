# Ephemeris

A generator for long Berlin-school pieces and whole concerts: interlocking sequencer rows on analogue
modelled voices, the gestures of a player on filters, echo and transposition, a synthesised tape
keyboard, string machine, pads and drones, lead solos, tape echo and spring. Standalone and VST3 on
Windows, native on Meta Quest 2. Everything is synthesised; there are no samples.

The plan, the musical specification and the literature behind each building block are in
[docs/PLAN.md](docs/PLAN.md) (German).

**Status:** Phases 0 to 2 -- the frame, the rack with a transposer row, the modular voice and the lead,
the tape echo, the two hands that play the knobs, and a ten-minute sketch that uses all of it. The
following lines describe Phase 0: parameter system, tempo map, score with gesture curves, MIDI export,
`eph_render`, self test and vector tests. It renders silence on the right clock; the first sound comes
with Phase 1.

## Build

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Try it

```bash
build/Tools/render/Release/eph_render.exe --minutes 20 --bpm 96 --ramp-to 124 --out out/frame.wav --midi out/frame.mid
build/Tools/render/Release/eph_render.exe --list
```

## Licence

AGPL-3.0, as Noctuary and Phosphene. Some modules are copied from
[Phosphene](../PsytranceGenerator) and name their origin and commit in their file header.
