"""Ephemeris -- the manual's screenshots: every tab as a whole page, from the standalone, muted.

    python Tools/manual/make_shots.py [path\\to\\Ephemeris.exe]   -> docs/screenshots/tab_NN.png

Each tab is its own run of the standalone in the screenshot mode: the seed 4242, the playhead at beat 400, and
EPH_SHOT_FULL, which grows the window until nothing of the page in front scrolls, so every knob is in the picture.
The runs are muted (EPH_SHOT forces it). Besides, docs/screenshot.png: the rack at the window's usual size, the
picture on the project's front page.
"""
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
TABS = ["Mixer", "Perform", "Rack", "Gestures", "Voices", "Lead", "Drone", "Tape Keys", "Strings", "Poly", "Atmosphere",
        "Echo + Spring", "Hall", "Drums", "Master", "Style"]


def shot(exe, tab, path, full):
    env = dict(os.environ, EPH_MUTE="1", EPH_SEED="4242", EPH_PLAY="1", EPH_SHOT_AT="400", EPH_TAB=str(tab), EPH_SHOT=path)
    if full:
        env["EPH_SHOT_FULL"] = "1"
    else:
        env.pop("EPH_SHOT_FULL", None)
    if os.path.exists(path):
        os.remove(path)
    subprocess.run([exe], env=env, timeout=600, check=False)
    if not os.path.exists(path):
        sys.exit("no picture of tab %d (%s)" % (tab, TABS[tab]))
    print("%-14s %s" % (TABS[tab], os.path.relpath(path, ROOT)))


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "Plugin", "Ephemeris_artefacts", "Release",
                                                              "Standalone", "Ephemeris.exe")
    out = os.path.join(ROOT, "docs", "screenshots")
    os.makedirs(out, exist_ok=True)
    for tab in range(len(TABS)):
        shot(exe, tab, os.path.join(out, "tab_%02d.png" % tab), True)
    shot(exe, 2, os.path.join(ROOT, "docs", "screenshot.png"), False)


if __name__ == "__main__":
    main()
