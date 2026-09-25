# Ephemeris

A generator for long Berlin-school pieces and whole concerts: interlocking sequencer rows on analogue
modelled voices, the gestures of a player on filters, echo and transposition, a synthesised tape
keyboard, string machine, pads and drones, lead solos, tape echo and spring. Standalone and VST3 on
Windows, native on Meta Quest. Everything is synthesised; there are no samples.

The plan, the musical specification and the literature behind each building block are in
[docs/PLAN.md](docs/PLAN.md) (German); the user manual is [docs/manual](docs/manual/Ephemeris-Manual.pdf).

**Status (25.09.2026):** Phases 0 to 7 in their first form.
- The rack with a transposer row; modular voices for the rows, the lead and the drone, running side by side
  in SIMD lanes; a synthesised tape keyboard (choir, strings, flute and the machine), a string machine, the
  atmosphere, tape echo with springs, hall, drums for the styles that have them.
- Five style profiles, a form grammar whose builds end on conjunctions of the rows, and a composer that
  writes whole pieces and concerts; rerolling any part on its own; `.ephset` files; WAV and MIDI export.
- The plugin: a mixer with meters, a perform page (filter hand, transposition key, hold, echo throw; MIDI
  keys, controllers and learn), the rack with the orrery, a page per source; the host's tempo in a host.
- Concerts that morph from one style to another and follow an arc of tension; a style of your own.
- Rooms: the hall or a plate, the tape echo or a bucket-brigade delay; a granular cloud in the atmosphere.
- Score cues over OSC for a visualiser (Kaleidoscope); stems; a gestures page and a style page.
- The Quest app (built, not yet run on a headset), the release build with installer and pluginval, and the
  manual generator.

![The panel](docs/screenshot.png)

## Build

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
build/Plugin/Ephemeris_artefacts/Release/Standalone/Ephemeris.exe
```

`EPH_MUTE=1` starts the standalone or the plugin muted (the screenshot mode `EPH_SHOT` does as well); every
automated run uses it.

## Try it

```bash
build/Tools/render/Release/eph_render.exe --minutes 14 --seed 11 --set "compose.style=Cosmic" --out out/piece.wav --midi out/piece.mid
build/Tools/render/Release/eph_render.exe --concert 60 --seed 3 --set "compose.style=Drift" --out out/night.wav
build/Tools/render/Release/eph_render.exe --set-file out/piece.ephset --reroll lead --save-set out/piece2.ephset
build/Tools/render/Release/eph_render.exe --list
```

## Tools

| | |
|---|---|
| `Tools/manual/make_manual.py` | the manual out of the program: prose (`chapters.txt`), parameter tables (`eph_render --dump-params`), screenshots |
| `Tools/analyze_ref.py` | statistics of reference recordings (tempogram, row lengths, filter sweeps, levels); the audio never enters the project |
| `Deploy/build_release.ps1` | release build: static runtime, tests, manual, stage, checks, portable zip, Inno Setup installer |
| `Quest/build_apk.ps1` | the Quest APK without Gradle (see [Quest/README.md](Quest/README.md)) |

## Licence

AGPL-3.0, as Noctuary and Phosphene. Some modules are copied from
[Phosphene](../PsytranceGenerator) and name their origin and commit in their file header.
