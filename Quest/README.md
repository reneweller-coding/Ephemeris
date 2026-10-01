# Ephemeris for Meta Quest

The whole generator on the headset: the composer writes a piece, the engine synthesizes it, and the hands
play it. Native OpenXR, no game engine — `NativeActivity` + `android_native_app_glue`, EGL, GLES 3, the
Khronos OpenXR loader, `XR_EXT_hand_tracking`, Oboe, and the unchanged core from `../Core`. The frame of
the app (session, swapchains, point renderer, font, hands, audio stream) is Phosphene's Quest app; the
player and the panel are Ephemeris'.

```
Quest/
  CMakeLists.txt        NDK build of libephquest.so (links EphemerisCore, oboe, openxr_loader)
  AndroidManifest.xml   NativeActivity, hasCode=false, hand-tracking permission/features, VR category
  src/main.cpp          the app: OpenXR session, composer thread, Oboe, hand controls, GLES panel with the orrery
  res/mipmap-*/         the launcher icon at five densities (Deploy/make_icon.py)
  fetch_thirdparty.ps1  OpenXR loader (prefab AAR) and Oboe into ../ThirdParty (junctions into Noctuary's)
  build_apk.ps1         CMake/NDK -> aapt2 -> jar -> zipalign -> apksigner (debug key)
```

## Build

```powershell
powershell -File Quest\fetch_thirdparty.ps1
powershell -File Quest\build_apk.ps1
adb install -r bin\quest\EphemerisQuest.apk
```

Needs NDK r27 (`C:\Android-Buildtools\sdk\ndk\27.2.12479018`), build-tools 34, platform android-34 and
JDK 17 — the parameters at the top of `build_apk.ps1`. No Gradle and no ninja. The APK is 3.6 MB: Ephemeris
ships no data, every sound is synthesised.

Built on 25.09.2026 without a headset attached: it compiles and links for arm64 and the APK is signed, but it
has not run on a device yet.

## Three threads

| Thread | Does |
|---|---|
| Audio (Oboe, low latency, exclusive) | `PiecePlayer::process`: `Engine::process` and the play/stop fade; publishes beat, seconds and level. One compare-and-exchange, no lock, no allocation. |
| Composer | composes a whole piece and loads it before the stream starts; "next piece" composes the next one and swaps it in behind a fade while the audio thread writes silence. |
| Render (the glue thread) | OpenXR frame loop, hands, gestures, picture. |

## Playing it

| Gesture | Effect |
|---|---|
| left pinch | play / stop — a 15 ms fade, the music pauses where it is |
| right pinch | hold the moves (`perform.hold`: the score's moves stand where they are), or let them go again |
| both hands pinched together | the next piece: composed from the next seed, swapped in behind a fade |
| left hand height | the rows' filters (`perform.filter`), two octaves either way, mid height = as composed |
| right hand height | the echo throw (`perform.throw`), from mid height up |

The grammar every generator's headset shares (01.10.2026; until then the right pinch was the next piece): a pinch acts
when it opens again, so a pinch of both hands never also counts as two single ones. The left hand has a dead zone round
the middle. A hand moves its control only while it is **not** pinching. Height is measured against the head, so it
works standing or sitting; both controls are smoothed over 0.15 s and centred, so nothing ever jumps. When a
piece has ended and its rooms have rung out, the next one follows by itself.

**The bridge.** With `bridge_host` set, the app sends its hands to Ephemeris on that computer as well: OSC `/hands` with
six floats (left and right height, left and right pinch, left and right tracked), 30 times a second, to `bridge_port`
(9102 by default, the port in the plugin's settings under Headset). The plugin reads them with the same grammar and
the same numbers -- every generator of the family has them (its `Plugin/Frame.h`) -- and shows its headset controls
while they arrive. `audio=0` leaves the headset silent, so only the computer plays.

The panel is head-locked (yaw only) and drawn as points: a title line with the logo (the icon's orrery, in points of
light; larger on the start screen while the first piece is composed), the piece and its style, the section, the root and
the scale, the time and the tempo, the level, both hand controls, four beat lamps — and beside it the
orrery of the Rack page, every row a planet on its orbit around the root, with a column of light at a
conjunction.

## Config

`eph.cfg` in the app's external data folder, every key optional:

```
adb push eph.cfg /sdcard/Android/data/com.reneweller.ephemeris.quest/files/eph.cfg
```

```
mute=1                     start silent (the test rule); the engine still runs
seed=2026                  the first piece's seed; the next piece takes the next seed
minutes=12                 length of a piece
style=Cosmic               Cosmic, Doom, Melodic, Modern or Drift
quality=quest              quest (default: 3 singers per choir key) or desktop (6)
osc_host=192.168.1.20      the score cues to a visualiser (Cue.h: /eph/beat, /eph/phase, /eph/key, /eph/conjunction)
osc_port=9000
bridge_host=192.168.1.20   the bridge: the hands to Ephemeris on that computer; empty = off
bridge_port=9102                its headset port (the plugin's settings, Headset)
audio=0                    no sound on the headset, the computer plays (the same as mute=1)
set=compose.key=D;compose.scale=Dorian     any knobs, repeatable
night=8                    a night set of 8 hours: the styles mixed, the pieces overlapping as a DJ mixes them
```

## Still open

- The quality level has one setting so far, the tape keys' singers (6 → 3; `eph_render --quality quest`
  renders it on the desktop). The rows' 2x oversampling stays: in the SIMD lanes the ten voices cost two
  (NEON: three) registers, and whether that is too much is for the device to say.
- The NEON lane path is only checked through the x86 shim on the desktop (`vectest_neon`); on the device,
  build the core with the NDK and run `eph_vectest` and `eph_selftest` over adb, as Phosphene's README shows.
