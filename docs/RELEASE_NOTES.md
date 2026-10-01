# Ephemeris release notes

## Next: the family's panel (01.10.2026, not yet released)

**One panel for the family.** Totality, Parhelion, Ephemeris and Phosphene share their panel now (Plugin/Frame.h,
the same file in each, and Noctuary its right-hand tools): the header's two rows -- the logo, the style, the key and
the scale, piece, concert or night set and its length, Compose, New seed, Play, Mute; the status, Undo, Redo, Help and the
settings --, the overview under it, the tabs in one order (Set, Arrange, the instrument's own pages, Mixer, Perform,
Export, Style), the same keys (Space, Ctrl+Z / Ctrl+Y, F1, F11, Esc, Ctrl+S / Ctrl+O / Ctrl+E) and the same right
click on every control (MIDI learn, forget, the default). Each keeps its own colours and letters and has a picture
behind its panel, generated for it and kept low in contrast (Settings > Picture behind the panel takes it away).

**Undo and Redo** of every change -- a knob, a new seed, a reroll, a preset, a loaded set --, step by step, the step
named in the tooltip. **Help** (F1): this manual inside the plugin, by topic, with the page in front, the keys and the
headset as topics of their own (a new chapter, The panel). **Settings**: the update check, the picture, the headset,
the window's size, full screen, the keys, About.

**A live ring** round every knob the composer moves shows where it stands at this moment.

**The tabs anew.** Set (the composer's knobs), Arrange (the rerolls and the automation, once the Gestures tab), Rack,
Synths (Row Voices, Lead, Drone, Poly, Tape Keys, Strings), Atmosphere, Drums, Rooms (Echo + Spring, Hall), Mixer
(Console, Master), Perform, Export (WAV, stems, MIDI, the set file, the cues), Style. The rerolls, Save set, Load set
and Export left the top bar for their pages.

**The same controllers in every generator.** Controller 74 (brightness) grabs the filters, 11 throws the echo, 64 holds
the moves; the mod wheel is no longer bound to the filters.

**The headset only when there is one.** The controls of a Meta Quest's hands are shown only while a headset sends them,
or when the settings say Always. The Quest app sends its hands to the plugin in bridge mode (bridge_host in eph.cfg,
port 9102) and the plugin plays with them in the grammar every generator's headset shares: left pinch play / stop,
both hands the next piece, right pinch holds the moves or lets them go, the left hand the rows' filters, the right the echo throw. audio=0
leaves the headset silent.

On the headset alone the right pinch holds the moves now, and both hands together are the next piece (until now the
right pinch was).

**Words.** The automation is "the moves" now: Hold Moves, Move Length, Rest Between Moves, "reroll moves".

## 1.0.1 (01.10.2026)

**A piece, a concert or a night set.** The top bar chooses what is composed with three buttons -- Piece, Concert,
Night set -- and one Length beside them (a piece 4 to 40 minutes, a concert or a night set 20 minutes to 12 hours); a
click composes it, Compose reads "Compose piece", "Compose concert" or "Compose night set" and is lit while what plays
is something else; the status line says "single piece" or "concert of N pieces" / "night set of N pieces". The
concert's length is kept while a piece is chosen.

**Zoom.** The arrange view zooms with the mouse wheel around the pointer, down to four bars; a drag or Shift + wheel
moves along, a double click shows everything. A ruler counts a piece's bars and a concert's minutes; zoomed in, the
notes themselves stand in the lanes; a zoomed view pages on with the playhead. A concert's pieces are apart by a line
and carry their number (a night set's its style as well).

## 1.0.0 (26.09.2026)

The first public release: a generator for long Berlin School pieces, whole concerts and night sets -- sequencer rows
interlocking on modelled analogue voices, a composer that plays the form, the sounds and a player's hands on the
knobs, and the rooms around them. Standalone and VST3 for Windows, and a native app for Meta Quest. Everything is
synthesised; there are no samples.

### What is in it

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

### Requirements

- Windows 10 or 11, 64-bit, a processor with AVX2 (Intel from Haswell, 2013; AMD from Ryzen).
- For the plugin: a VST3 host.
- The Quest app: Meta Quest 2 or later, installed with `adb install -r EphemerisQuest-1.0.0.apk` (developer mode).

### Notes

- The installer and the program are not code-signed yet: Windows' SmartScreen may warn once ("More info", "Run
  anyway").
- Ephemeris asks GitHub once a day whether a newer release is out and shows it in the status row (nothing else is
  sent, nothing is downloaded); the switch "Update check" turns it off.
- Licence: AGPL-3.0. The source is at https://github.com/reneweller-coding/Ephemeris.
