# Ephemeris 1.0.0

The first public release: a generator for long Berlin School pieces, whole concerts and night sets -- sequencer rows
interlocking on modelled analogue voices, a composer that plays the form, the sounds and a player's hands on the
knobs, and the rooms around them. Standalone and VST3 for Windows, and a native app for Meta Quest. Everything is
synthesised; there are no samples.

## What is in it

- **The composer.** Five styles (Cosmic, Doom, Melodic, Modern, Drift) and one of your own; pieces of any length,
  concerts that morph from style to style, night sets that overlap as a DJ mixes; every part rerolled on its own;
  sets saved and loaded; WAV (with stems) and MIDI export.
- **The voices.** Eight sequencer rows, a lead and a drone on modular voices: five classic VCOs (Moog 921, Prophet-5,
  Oberheim SEM, ARP 2600, E-mu Modular) beside the analogue pair, with hard sync and cross mod, band-limited; ten
  circuit-modelled filters (Moog ladder, Prophet and Juno cascades, Oberheim SEM, Xpander pole mixing, diode ladder,
  Korg35, Polivoks, EDP Wasp, comb), solved sample by sample.
- **Modulation on every synth.** Full ADSRs for amp and filter, a mod envelope, four LFOs (synced to the bar or free,
  retriggered, fading in) and an eight-slot matrix on the voices, the lead, the drone and the pad synth; two LFOs and
  four slots on the tape keys and the string machine.
- **More instruments.** A tape keyboard (choir, strings, flute and the machine), a string machine after the
  Streichfett, a polyphonic wavetable pad synth (51 tables), drums for the styles that have them, an atmosphere.
- **Rooms and mix.** Tape echo, bucket-brigade delay, springs, hall or plate, early reflections, a blend room, a
  shimmer hall; a mix after a production guide for depth and clarity; every piece's loudest part levelled to its
  style's target.
- **Sounds you can take over.** 1024 factory presets per synth, most of them with modulation. The composer sets the
  piece's presets and mix on the knobs themselves: the pages show what plays, and a knob you turn moves from there.

## Requirements

- Windows 10 or 11, 64-bit, a processor with AVX2 (Intel from Haswell, 2013; AMD from Ryzen).
- For the plugin: a VST3 host.
- The Quest app: Meta Quest 2 or later, installed with `adb install -r EphemerisQuest-1.0.0.apk` (developer mode).

## Notes

- The installer and the program are not code-signed yet: Windows' SmartScreen may warn once ("More info", "Run
  anyway").
- Ephemeris asks GitHub once a day whether a newer release is out and shows it in the status row (nothing else is
  sent, nothing is downloaded); the switch "Update check" turns it off.
- Licence: AGPL-3.0. The source is at https://github.com/reneweller-coding/Ephemeris.
