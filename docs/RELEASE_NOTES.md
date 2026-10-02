# Ephemeris release notes

## 1.3.0 (03.10.2026): the family plays together

**The family jam** (Settings > Family jam: Off, Lead or Follow; in the plugin and in the standalone). The five
instruments -- Totality, Parhelion, Ephemeris, Phosphene and Noctuary -- play as one band on the local network. The
leader's key, the energy of its sections and its breaks and drops go out over UDP multicast; a follower takes the new
root at its next bar line by the shortest way and the mode with its next piece, and in Ephemeris every voice but the
drums moves to the leader's key, on top of its own Transpose; the rows' filters close with a quiet section of the
leader's (while the performer's hand leaves them alone); and in the leader's breaks the drums are out until its drop.
Ableton Link or the DAW's transport gives them the same bars, so that a section lands on the same bar line everywhere;
a leader that falls silent for four seconds leaves its followers to themselves.

**An Audio Unit on the Mac, an LV2 on Linux.** The macOS zip has the Audio Unit beside the standalone and the VST3
(Logic, GarageBand, MainStage), passed by Apple's `auval -strict` on GitHub's runners; the Linux archive has the LV2
(Ardour, Carla, Reaper, Qtractor), read by lilv there.

**Kaleidoscope.** KaleidoscopeEnhanced understands the score cues of all five instruments as they come (since
02.10.2026) -- Totality's blocks and keys, Parhelion's sections, Ephemeris' phases, Phosphene's sections and drops,
Noctuary's bars, keys and scenes -- and cuts its pictures to them, on a drop at once.

**Behind the panel.** A test of the follower's engine (`testJam`): the transposition note for note, the drums as
written, the break without its foundation. The jam's bus (Plugin/Jam.h) is the same file in all five repositories.

## 1.2.0 (02.10.2026): with a DAW, on a Mac, and heard

**MIDI out.** In a DAW the plugin sends what it plays: every note of every part at the moment it sounds, a channel
per part as in the MIDI file of an export, the muted parts left out; a stop or a jump of the transport sends all notes
off. Record a part as notes, or let another instrument double it.

**Outputs of their own.** Besides the main output, a stereo output for each of its fifteen channel strips (the eight rows, the lead, the drone, the tape keys, the strings, the poly synth, the drums and the atmosphere) and one for the rooms, off until the
DAW switches them on; each carries its part before the master while the main output plays on.

**Ableton Link** in the standalone (Settings > Ableton Link, off to begin with): with other Link apps in the session
Ephemeris takes their tempo, lines its bars up with theirs and starts and stops with them; alone it offers its own tempo.

**The keyboard's options**, each off until chosen (the Keyboard group): Lower Keys Play and Split At divide the keys
between two voices, Scale Lock keeps every key in the piece's scale, Velocity Curve (As Played, Soft, Hard, Fixed) shapes the
touch. A release always ends the note its press started.

**On a Mac.** Every release gets a build for Apple Silicon (macOS 12 or newer) -- the standalone and the VST3 --,
built and tested on GitHub's runners by the workflow `macos` and attached to the release as `Ephemeris-<version>-macOS.zip`.
It is signed ad hoc, not notarized (that takes a paid Apple account; README-macOS.txt in the zip says how to open it),
and it has not yet been played on a real Mac.

**Demos.** The release "demos" holds a track per style as an MP3 and one of them as a video with pictures by
[KaleidoscopeEnhanced](https://github.com/reneweller-coding/KaleidoscopeEnhanced), its cuts placed by the track's own
score cues; `Tools/demo/make_demos.py` renders them all again.

**Behind the panel.** A sound check (`Tools/soundcheck.py`, `ctest -L sound`): five styles, four minutes each, and their stems, measured by loudness
(BS.1770) against `Tests/golden/soundcheck.json`, so a change that makes a style louder, quieter or emptier shows up
before anybody listens. A CI run on every push (GitHub Actions: the build and the tests on Windows, the documentation
check on Linux) that nobody waits for. One release script for the family (`Deploy/publish_release.ps1`: the notes from
this file, the checksums, the tag, the release).

**On Linux** (the same evening, attached to the release afterwards): an archive for x86-64 with the
standalone, the VST3 and the renderer, built by the workflow `linux` on GitHub's Ubuntu 22.04 runners
(GCC 12), its quick tests run there, and tried under WSL: the window, and the sound check against the
Windows renders.

## 1.1.0 (01.10.2026): the family's panel

**Play it yourself.** A Keyboard group on the Perform page: Keyboard Plays sends the keys of a MIDI keyboard to a voice
(the lead, the drone, the poly synth, the tape keys, the strings, the drums, a row, or by channel), with the sound its page has; Replace leaves that voice's generated notes out, Layer plays over
them; Composer off leaves every generated note out, so only what is played sounds -- through the mix and the effects
as composed. The keys play on their samples; an export plays what was composed.

**One layout for the repositories.** Every instrument of the family builds the same way now: `build.ps1` (msvc, icx,
release, quest) on the presets of `CMakePresets.json`, the build trees under `build\<preset>`, everything that can be
started -- the standalone, the VST3, the renderer -- flat in `bin\msvc` and `bin\icx` (and the Quest APK in
`bin\quest`), the release in `dist\`, local data, renders and logs in `work\` (`cmake/Family.cmake`).

**Every line explained.** Every class, function, variable, macro and table of the sources -- the core, the plugin,
the Quest app, the tools and the tests -- has its Doxygen comment now, and the test `doccheck` (`cmake/Family.cmake`)
fails as soon as one is missing. The scripts that generate tables write the comments into what they generate.

**No page scrolls.** The window opens at 1280 x 860, as every generator of the family's. A tab of several modules has a
small tab for each, and a page that is still taller than the window shows its groups in sections, one at a time --
the sound and the modulation apart, cut further where needed (Filter, LFO, Matrix ...) --, switched at its top right.
The overview above the tabs can be folded away in the settings.

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

## 1.0.1 (01.10.2026, released with 1.1.0)

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
