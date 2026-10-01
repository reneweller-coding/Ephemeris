"""Ephemeris -- the manual's screenshots: every page as a whole, from the standalone, muted.

    python Tools/manual/make_shots.py [path\\to\\Ephemeris.exe]   -> docs/screenshots/tab_NN.png

Each tab is its own run of the standalone in the screenshot mode: the seed 4242, the playhead at beat 400, and
EPH_SHOT_FULL, which grows the window until nothing of the page in front scrolls, so every knob is in the picture.
The runs are muted (EPH_SHOT forces it). A page is a tab, or one of the small tabs of a tab of several (EPH_SUBTAB:
Synths, Rooms, Mixer); the pictures are numbered in the order of PAGES, which chapters.txt refers to. Besides, docs/screenshot.png: the rack at the window's usual size, the
picture on the project's front page; docs/screenshots/concert.png, a 60-minute concert, and docs/screenshots/zoom.png,
the arrange view zoomed in on a build (EPH_SHOT_ZOOM), after Parhelion's.
"""
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
# (name, tab, small tab or None): the frame's order of the tabs (01.10.2026).
PAGES = [("Set", 0, None), ("Arrange", 1, None), ("Rack", 2, None),
         ("Row Voices", 3, 0), ("Lead", 3, 1), ("Drone", 3, 2), ("Poly", 3, 3), ("Tape Keys", 3, 4), ("Strings", 3, 5),
         ("Atmosphere", 4, None), ("Drums", 5, None), ("Echo + Spring", 6, 0), ("Hall", 6, 1),
         ("Console", 7, 0), ("Master", 7, 1), ("Perform", 8, None), ("Export", 9, None), ("Style", 10, None)]
RACK = 2


def shot(exe, page, path, full, extra=None):
    name, tab, sub = PAGES[page]
    env = dict(os.environ, EPH_MUTE="1", EPH_SEED="4242", EPH_PLAY="1", EPH_SHOT_AT="400", EPH_TAB=str(tab), EPH_SHOT=path)
    env.pop("EPH_SUBTAB", None)
    if sub is not None:
        env["EPH_SUBTAB"] = str(sub)
    env.update(extra or {})
    if full:
        env["EPH_SHOT_FULL"] = "1"
    else:
        env.pop("EPH_SHOT_FULL", None)
    if os.path.exists(path):
        os.remove(path)
    subprocess.run([exe], env=env, timeout=600, check=False)
    if not os.path.exists(path):
        sys.exit("no picture of page %d (%s)" % (page, name))
    print("%-14s %s" % (name, os.path.relpath(path, ROOT)))


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "Plugin", "Ephemeris_artefacts", "Release",
                                                              "Standalone", "Ephemeris.exe")
    out = os.path.join(ROOT, "docs", "screenshots")
    os.makedirs(out, exist_ok=True)
    for page in range(len(PAGES)):
        shot(exe, page, os.path.join(out, "tab_%02d.png" % page), True)
    shot(exe, RACK, os.path.join(out, "concert.png"), False, {"EPH_SETS": "compose.concert_minutes=60", "EPH_SHOT_AT": "900"})
    shot(exe, RACK, os.path.join(out, "zoom.png"), False, {"EPH_SHOT_ZOOM": "360:520"})
    shot(exe, RACK, os.path.join(ROOT, "docs", "screenshot.png"), False)


if __name__ == "__main__":
    main()
