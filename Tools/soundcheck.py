"""Ephemeris -- the sound regression check: fixed seeds rendered, measured, and held against Tests/golden/soundcheck.json.

    python Tools/soundcheck.py [--exe path\to\eph_render.exe] [--update] [--only CASE] [--jobs N]

A change of the code must not thin or swell the sound without anybody noticing (02.10.2026; Totality's "only kick and
hats" rounds were found by ear, never by a test). For every case below the render tool writes the mix and its stems;
this script measures the mix's integrated loudness (ITU-R BS.1770-4: K-weighting, 400 ms blocks, the -70 LUFS and the
relative -10 LU gate) and every stem's loudness relative to the mix, and holds them against what was recorded when the
golden file was last updated. Not bit for bit -- icx and MSVC already differ, and so does every deliberate change of a
default -- but inside bands: the mix within MIX_TOL LU, a stem within STEM_TOL LU of its recorded level; a stem quieter
than QUIET LU below the mix only has to stay quiet. --update records the present state, after a change that was meant
(say why in the commit).

The ctest "soundcheck" (labels sound, slow; cmake/Family.cmake, family_soundcheck) runs it with the build's
eph_render. Without numpy and scipy it exits 77, which ctest reports as skipped.
"""
import argparse
import concurrent.futures
import glob
import json
import math
import os
import shutil
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GOLDEN = os.path.join(ROOT, "Tests", "golden", "soundcheck.json")
RENDER = "eph_render"
STEMS = "dir"   # "dir": --stems names a directory; "prefix": --stems names a prefix, files <prefix>_<stem>.wav
MIX_TOL = 1.0       # LU: the mix's integrated loudness
STEM_TOL = 3.0      # LU: a stem's loudness relative to the mix
QUIET = -40.0       # LU below the mix: quieter than this, a stem is only checked for staying quiet (under -34)
# The cases: a name and the render tool's arguments (the seed, the style, the length).
CASES = [
    ('cosmic', ['--seed', '7', '--minutes', '4', '--set', 'compose.style=Cosmic']),
    ('doom', ['--seed', '11', '--minutes', '4', '--set', 'compose.style=Doom']),
    ('melodic', ['--seed', '23', '--minutes', '4', '--set', 'compose.style=Melodic']),
    ('modern', ['--seed', '42', '--minutes', '4', '--set', 'compose.style=Modern']),
    ('drift', ['--seed', '5', '--minutes', '4', '--set', 'compose.style=Drift']),
]


def _biquads(sr):
    """The two K-weighting stages of BS.1770-4 for the rate @p sr: the high shelf and the RLB high pass."""
    g, q, fc = 3.999843853973347, 0.7071752369554196, 1681.974450955533
    a = 10.0 ** (g / 40.0)
    w0 = 2.0 * math.pi * fc / sr
    al = math.sin(w0) / (2.0 * q)
    c = math.cos(w0)
    s = 2.0 * math.sqrt(a) * al
    shelf = ([a * ((a + 1) + (a - 1) * c + s), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - s)],
             [(a + 1) - (a - 1) * c + s, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - s])
    fc, q = 38.13547087602444, 0.5003270373238773
    w0 = 2.0 * math.pi * fc / sr
    al = math.sin(w0) / (2.0 * q)
    c = math.cos(w0)
    hp = ([(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al])
    return shelf, hp


def read_wav(path):
    """The samples of a WAV as float64 frames x channels, full scale 1."""
    import numpy as np
    from scipy.io import wavfile
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")   # the cue and list chunks a render writes
        sr, x = wavfile.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / float(2 ** (8 * x.dtype.itemsize - 1))
    elif x.dtype.kind == "u":
        x = (x.astype(np.float64) - 128.0) / 128.0
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = x[:, None]
    return sr, x


def loudness(sr, x):
    """Integrated loudness (LUFS) of @p x, or None where nothing passes the -70 LUFS gate."""
    import numpy as np
    from scipy.signal import lfilter
    (b1, a1), (b2, a2) = _biquads(sr)
    y = lfilter(b2, a2, lfilter(b1, a1, x, axis=0), axis=0)
    block, step = int(round(0.4 * sr)), int(round(0.1 * sr))
    if len(y) < block:
        return None
    cs = np.concatenate([np.zeros((1, y.shape[1])), np.cumsum(y * y, axis=0)])
    starts = np.arange(0, len(y) - block + 1, step)
    z = ((cs[starts + block] - cs[starts]) / block).sum(axis=1)
    lk = -0.691 + 10.0 * np.log10(np.maximum(z, 1e-30))
    gated = z[lk > -70.0]
    if len(gated) == 0:
        return None
    rel = -0.691 + 10.0 * math.log10(gated.mean()) - 10.0
    gated = z[(lk > -70.0) & (lk > rel)]
    return round(-0.691 + 10.0 * math.log10(gated.mean()), 2)


def run_case(exe, work, name, args):
    """Renders one case into work/<name> and measures it: {"mix": LUFS, "stems": {stem: LU against the mix}}."""
    d = os.path.join(work, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "stems"))
    mix = os.path.join(d, "mix.wav")
    stems = os.path.join(d, "stems") if STEMS == "dir" else os.path.join(d, "stems", "stem")
    env = dict(os.environ)
    for k in ("TOT_MUTE", "PARH_MUTE", "EPH_MUTE", "PHOS_MUTE", "AMBIENT_MUTE"):
        env.pop(k, None)
    if RENDER == "ambient_render":
        env.setdefault("AMBIENT_PACKS", os.path.join(ROOT, "Library", "Packs"))
    p = subprocess.run([exe] + args + ["--out", mix, "--stems", stems], cwd=ROOT, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if p.returncode != 0 or not os.path.exists(mix):
        raise RuntimeError("%s: the render failed (%d)\n%s" % (name, p.returncode, p.stdout[-2000:]))
    sr, x = read_wav(mix)
    total = loudness(sr, x)
    result = {"mix": total, "stems": {}}
    for f in sorted(glob.glob(os.path.join(d, "stems", "*.wav"))):
        stem = os.path.splitext(os.path.basename(f))[0]
        if STEMS == "prefix" and stem.startswith("stem_"):
            stem = stem[5:]
        s = loudness(*read_wav(f))
        result["stems"][stem] = None if (s is None or total is None) else round(s - total, 2)
    shutil.rmtree(d, ignore_errors=True)
    return name, result


def compare(golden, now):
    """The lines that break the bands, empty when everything holds."""
    bad = []
    for name, g in golden.items():
        n = now.get(name)
        if n is None:
            continue
        if (g["mix"] is None) != (n["mix"] is None) or (g["mix"] is not None and abs(n["mix"] - g["mix"]) > MIX_TOL):
            bad.append("%s: the mix %s LUFS, recorded %s" % (name, n["mix"], g["mix"]))
        for stem in sorted(set(g["stems"]) | set(n["stems"])):
            if stem not in n["stems"]:
                bad.append("%s: the stem %s is gone" % (name, stem))
                continue
            if stem not in g["stems"]:
                bad.append("%s: a new stem %s (--update records it)" % (name, stem))
                continue
            gv, nv = g["stems"][stem], n["stems"][stem]
            if gv is None or gv < QUIET:
                if nv is not None and nv > QUIET + 6.0:
                    bad.append("%s: %s was silent or quiet (%s LU), now %s LU" % (name, stem, gv, nv))
            elif nv is None or abs(nv - gv) > STEM_TOL:
                bad.append("%s: %s %s LU against the mix, recorded %s" % (name, stem, nv, gv))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=os.path.join(ROOT, "bin", "msvc", RENDER + ".exe"))
    ap.add_argument("--work", default=os.path.join(ROOT, "work", "soundcheck"))
    ap.add_argument("--update", action="store_true", help="record the present state as the golden one")
    ap.add_argument("--only", action="append", help="this case only (repeatable)")
    ap.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 2) // 2)))
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)   # the render runs in the repository's root
    try:
        import numpy  # noqa: F401
        import scipy  # noqa: F401
    except ImportError:
        print("soundcheck: needs numpy and scipy -- skipped")
        return 77
    if not os.path.exists(a.exe):
        print("soundcheck: no %s" % a.exe)
        return 2
    cases = [c for c in CASES if not a.only or c[0] in a.only]
    now = {}
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for name, result in pool.map(lambda c: run_case(a.exe, a.work, c[0], c[1]), cases):
            now[name] = result
            loud = [k for k, v in result["stems"].items() if v is not None and v >= QUIET]
            print("%-20s mix %7s LUFS, %d stems (%d audible)" % (name, result["mix"], len(result["stems"]), len(loud)))
    golden = {}
    if os.path.exists(GOLDEN):
        with open(GOLDEN, encoding="utf-8") as f:
            golden = json.load(f)["cases"]
    if a.update:
        golden.update(now)
        os.makedirs(os.path.dirname(GOLDEN), exist_ok=True)
        with open(GOLDEN, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"render": RENDER, "mix_tol": MIX_TOL, "stem_tol": STEM_TOL, "quiet": QUIET,
                       "cases": dict(sorted(golden.items()))}, f, indent=1, sort_keys=False)
            f.write("\n")
        print("recorded %d cases in %s" % (len(now), os.path.relpath(GOLDEN, ROOT)))
        return 0
    missing = [c[0] for c in cases if c[0] not in golden]
    if missing:
        print("soundcheck: nothing recorded for %s -- run with --update" % ", ".join(missing))
        return 1
    bad = compare(golden, now)
    for line in bad:
        print("  " + line)
    print("soundcheck: %s" % ("%d out of their bands" % len(bad) if bad else "every case inside its bands"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
