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
adb install -r build-quest\EphemerisQuest.apk
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
| right pinch | next piece: composed from the next seed, swapped in behind a fade |
| left hand height | the rows' filters (`perform.filter`), two octaves either way, mid height = as composed |
| right hand height | the echo throw (`perform.throw`), from mid height up |

A hand moves its control only while it is **not** pinching. Height is measured against the head, so it
works standing or sitting; both controls are smoothed over 0.15 s and centred, so nothing ever jumps. When a
piece has ended and its rooms have rung out, the next one follows by itself.

The panel is head-locked (yaw only) and drawn as points: the piece and its style, the section, the root and
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
set=compose.key=D;compose.scale=Dorian     any knobs, repeatable
```

## Still open

- A quality level for the headset (PLAN: tape-key singers 6 → 3, the rows without 2x oversampling except the
  bass row) — to be decided after measuring on the device.
- The NEON lane path is only checked through the x86 shim on the desktop (`vectest_neon`); on the device,
  build the core with the NDK and run `eph_vectest` and `eph_selftest` over adb, as Phosphene's README shows.
