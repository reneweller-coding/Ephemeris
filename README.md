# Ephemeris

A generator for long Berlin-school pieces and whole concerts: interlocking sequencer rows on analogue
modelled voices, the gestures of a player on filters, echo and transposition, a synthesised tape
keyboard, string machine, pads and drones, lead solos, tape echo and spring. Standalone and VST3 on
Windows, native on Meta Quest. Everything is synthesised; there are no samples.

The plan, the musical specification and the literature behind each building block are in
[docs/PLAN.md](docs/PLAN.md) (German); the user manual is [docs/manual](docs/manual/Ephemeris-Manual.pdf).

**Version 1.0.0 (26.09.2026)**, the first release ([release notes](docs/RELEASE_NOTES.md)). Windows 10/11 x64 with an
AVX2 processor; Meta Quest 2 or later.
- Five classic VCOs (Moog 921, Prophet-5, Oberheim SEM, ARP 2600, E-mu Modular) with hard sync and cross mod, and ten
  circuit-modelled filters solved sample by sample, in SIMD lanes; full envelopes, four LFOs and a modulation matrix
  on every synth; presets with generous modulation, set by the composer on the knobs themselves.
- Every piece's loudest part levelled to its style's target (`Leveler.h`); an update check against GitHub's releases.
- The rack with a transposer row; modular voices for the rows, the lead and the drone, running side by side
  in SIMD lanes; a synthesised tape keyboard (choir, strings, flute and the machine), a string machine, the
  atmosphere, tape echo with springs, hall, drums for the styles that have them.
- Five style profiles, a form grammar whose builds end on conjunctions of the rows, and a composer that
  writes whole pieces and concerts; rerolling any part on its own; `.ephset` files; WAV and MIDI export.
- The plugin: a mixer with meters, a perform page (filter hand, transposition key, hold, echo throw; MIDI
  keys, controllers and learn), the rack with the orrery, a page per source; the host's tempo in a host.
- Concerts that morph from one style to another and follow an arc of tension; a style of your own.
- Rooms: the hall or a plate, the tape echo or a bucket-brigade delay; a granular cloud in the atmosphere.
- A string machine after Waldorf's Streichfett: five registers morphing through eight registrations, the
  mix animated by a slow LFO, three ensembles and a phaser.
- Harmony, sequencing and form after a style guide of the modern Berlin School: a chord track the bass
  follows under the unchanged sequence, sequencer transpositions, parallel changes of mode, eight modes;
  sequence archetypes, ratchets, probability gates, a doubled pulse, a sequence that loses its steps;
  the open fifth at the end, the rooms and the lead's phrasing after the guide; a mode of its own for a
  later phase, quantised random steps, the stereo field with a wandering drone, a second echo for the
  counter rows, and concerts as albums with interludes.
- A mix after a production guide for depth, width and clarity: a low cut per source, the bass mono under
  100 Hz, a guarded width, ducked echo and hall returns, level and width automated along the form, the
  styles levelled (and since 1.0 every piece's peak to its style's target); eph_render reports loudness (EBU R128), true
  peak, PSR, loudness range and the stereo correlation of every render.
- From the dark-ambient practice: a distance macro per source (the bass approaches and recedes), cascaded
  ducking rows -> pads -> atmosphere, a foundation in pure intervals, an all-pass spread for the tape keys,
  slow movements with irrational periods, near events where no sequence plays, export fades, an archive
  master without a limiter.
- 1024 factory presets for each synth (voices, lead, drone, tape keys, strings, drums, atmosphere), in
  sixteen groups each, with names from dark to bright; they set the sound, never the mix or the tuning;
  your own presets saved beside them.
- A blend room (a short plate) that feeds the hall -- the serial far space; a soft clipper before the limiter,
  a limiter for the band under 80 Hz, a punch for the main sequence; early reflections and a shimmer hall as
  the production guide's sends A and D, spectral ducking in six bands, a resonance tamer on the rows.
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
| `Deploy/build_release.ps1` | release build (Intel's icx where oneAPI is installed, else MSVC): static runtime, tests, pluginval, manual, stage, checks, optional signing, portable zip, Inno Setup installer |
| `Deploy/publish_release.ps1` | the GitHub release: tag, installer, portable zip, Quest APK, release notes |
| `eph_bench` (Tests/bench.cpp) | the voice bank's and the pad synth's cost per second of audio |
| `Quest/build_apk.ps1` | the Quest APK without Gradle (see [Quest/README.md](Quest/README.md)) |

## Updates

Once a day the program asks GitHub's releases whether a newer version is out and shows it in the status row as a link
to its page; nothing else is sent, nothing is downloaded. "Update check" in the status row turns it off.

## Licence

AGPL-3.0, as Noctuary and Phosphene. Some modules are copied from
[Phosphene](../PsytranceGenerator) and name their origin and commit in their file header.
