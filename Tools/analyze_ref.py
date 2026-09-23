"""Reference measurements for the style profiles (PLAN 11.4). Only statistics are kept, never audio.

For every track of a profile's folders (Tools/ref_sets.txt) it measures:
  * the step period of the sequence: the strongest periodicity of the onset envelope between 0.09 and
    0.6 s, and from it the tempo, read as sixteenths or eighths, whichever lands in 80..150 BPM;
  * the row length (rough): the shortest multiples k of the step period (3..32) that correlate nearly as
    strongly as the strongest -- every multiple of the true period correlates, so this is a hint only;
  * the filter sweeps: brightness = log2 of the power-weighted spectral centroid over 150 Hz .. 8 kHz
    in half-second frames, smoothed over 4 s; a sweep is a run between two turning points at least
    0.25 octave apart -- its duration in seconds and its span in octaves;
  * length and level (RMS dBFS of the whole, and the 10..95 % range of 3-second RMS).

Usage:
  python Tools/analyze_ref.py [--profiles Cosmic,Melodic] [--max-tracks 10] [--out Tools/ref_stats.json]
Decoding is by ffmpeg (on PATH or C:\\Anw\\Tools\\ffmpeg\\bin), to mono float at 11025 Hz.
"""
import argparse, json, os, shutil, subprocess, sys
import numpy as np

SR = 11025
AUDIO = ('.flac', '.mp3', '.wav', '.m4a', '.ogg', '.ape', '.wv')
HERE = os.path.dirname(os.path.abspath(__file__))


def ffmpeg():
    exe = shutil.which('ffmpeg') or r'C:\Anw\Tools\ffmpeg\bin\ffmpeg.exe'
    if not os.path.exists(exe):
        sys.exit('ffmpeg not found')
    return exe


def decode(path):
    cmd = [ffmpeg(), '-v', 'quiet', '-i', path, '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-']
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def read_sets(path):
    sets = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            prof, folder = [x.strip() for x in line.split('|', 1)]
            sets.setdefault(prof, []).append(folder)
    return sets


def tracks_of(folders, limit):
    out = []
    for folder in folders:
        if os.path.isfile(folder):
            out.append(folder)
            continue
        for root, _, files in os.walk(folder):
            out += [os.path.join(root, f) for f in sorted(files) if f.lower().endswith(AUDIO)]
    # Spread the choice over the folders instead of taking the first album whole.
    if len(out) > limit:
        idx = np.linspace(0, len(out) - 1, limit).round().astype(int)
        out = [out[i] for i in sorted(set(idx))]
    return out


def stft_power(x, n=1024, hop=256):
    w = np.hanning(n)
    frames = 1 + max(0, (len(x) - n) // hop)
    idx = np.arange(n)[None, :] + hop * np.arange(frames)[:, None]
    return np.abs(np.fft.rfft(x[idx] * w, axis=1)) ** 2, hop


def measure(x):
    res = {'seconds': round(len(x) / SR, 1)}
    if len(x) < SR * 20:
        return res
    P, hop = stft_power(x)
    f = np.fft.rfftfreq(1024, 1 / SR)
    # Onset envelope: positive spectral flux on log power, above 100 Hz.
    band = f > 100
    L = np.log1p(P[:, band] * 1e4)
    flux = np.maximum(np.diff(L, axis=0), 0).sum(axis=1)
    flux -= np.convolve(flux, np.ones(43) / 43, mode='same')   # remove the slow part (~1 s)
    flux = np.maximum(flux, 0)
    fr = SR / hop                                                # envelope frames per second
    ac = np.correlate(flux - flux.mean(), flux - flux.mean(), mode='full')[len(flux) - 1:]
    ac /= ac[0] + 1e-12
    lo, hi = int(0.09 * fr), int(0.6 * fr)
    lag = lo + int(np.argmax(ac[lo:hi]))
    # Parabolic refinement of the peak.
    a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
    lagf = lag + 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
    step = lagf / fr
    res['step_s'] = round(step, 4)
    res['step_strength'] = round(float(b), 3)
    for div, name in ((4, 'sixteenths'), (2, 'eighths')):
        bpm = 60.0 / (div * step)
        if 80 <= bpm <= 150:
            res['bpm'], res['step_as'] = round(bpm, 1), name
            break
    ks = []
    for k in range(3, 33):
        j = int(round(k * lagf))
        if j + 1 < len(ac):
            ks.append((float(ac[j - 1:j + 2].max()), k))
    # Every multiple of the true period correlates; the row is the shortest that is nearly as strong
    # as the strongest.
    best = max(v for v, _ in ks) if ks else 0.0
    strong = sorted(k for v, k in ks if v >= 0.9 * best)
    res['row_length_candidates'] = strong[:3]

    # Brightness in half-second frames, over 150 Hz .. 8 kHz, then 4-s smoothing.
    per = int(0.5 * fr)
    bandc = (f >= 150) & (f <= 8000)
    Pc = P[:, bandc]
    nfr = Pc.shape[0] // per
    Pc = Pc[:nfr * per].reshape(nfr, per, -1).sum(axis=1)
    cent = (Pc * f[bandc]).sum(axis=1) / (Pc.sum(axis=1) + 1e-20)
    br = np.log2(np.maximum(cent, 1.0))
    br = np.convolve(br, np.ones(8) / 8, mode='same')
    # Turning points with 0.25 octave hysteresis: a maximum counts once the brightness has fallen that
    # far below it, a minimum once it has risen that far above it.
    H = 0.25
    sweeps, state, hi, lo = [], 0, (0, br[0]), (0, br[0])
    for i, v in enumerate(br):
        if v > hi[1]:
            hi = (i, v)
        if v < lo[1]:
            lo = (i, v)
        if state != -1 and hi[1] - v >= H:
            sweeps.append(hi); state, lo = -1, (i, v)
        elif state != 1 and v - lo[1] >= H:
            sweeps.append(lo); state, hi = 1, (i, v)
    durs = [(sweeps[j + 1][0] - sweeps[j][0]) * 0.5 for j in range(len(sweeps) - 1)]
    spans = [abs(sweeps[j + 1][1] - sweeps[j][1]) for j in range(len(sweeps) - 1)]
    res['sweeps'] = len(durs)
    if durs:
        res['sweep_s_median'] = round(float(np.median(durs)), 1)
        res['sweep_s_q25_q75'] = [round(float(np.percentile(durs, 25)), 1), round(float(np.percentile(durs, 75)), 1)]
        res['sweep_oct_median'] = round(float(np.median(spans)), 2)
    res['sweeps_per_min'] = round(len(durs) / (len(x) / SR / 60), 2)
    res['brightness_range_oct'] = round(float(np.percentile(br, 95) - np.percentile(br, 5)), 2)

    seg = int(3 * SR)
    rms = np.sqrt(np.array([np.mean(x[i:i + seg] ** 2) for i in range(0, len(x) - seg, seg)]) + 1e-20)
    res['rms_db'] = round(float(10 * np.log10(np.mean(x ** 2) + 1e-20)), 1)
    res['range_db'] = round(float(20 * np.log10(np.percentile(rms, 95) / max(np.percentile(rms, 10), 1e-10))), 1)
    return res


def summary(rows):
    def med(key):
        v = [r[key] for r in rows if key in r]
        return round(float(np.median(v)), 2) if v else None
    return {k: med(k) for k in ('seconds', 'bpm', 'step_s', 'sweep_s_median', 'sweep_oct_median',
                                'sweeps_per_min', 'brightness_range_oct', 'rms_db', 'range_db')} | {'tracks': len(rows)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sets', default=os.path.join(HERE, 'ref_sets.txt'))
    ap.add_argument('--profiles', default='')
    ap.add_argument('--max-tracks', type=int, default=10)
    ap.add_argument('--out', default=os.path.join(HERE, 'ref_stats.json'))
    args = ap.parse_args()
    sets = read_sets(args.sets)
    want = [p for p in args.profiles.split(',') if p] or list(sets)
    stats = json.load(open(args.out, encoding='utf-8')) if os.path.exists(args.out) else {}
    for prof in want:
        rows = []
        for path in tracks_of(sets.get(prof, []), args.max_tracks):
            try:
                r = measure(decode(path))
            except Exception as e:   # a file ffmpeg cannot read is skipped, and said so
                print(f'  skipped {path}: {e}')
                continue
            r['track'] = os.path.basename(path)
            rows.append(r)
            print(f"  {prof:8s} {r['track'][:48]:48s} {r['seconds']:6.0f}s  bpm {r.get('bpm', '-')!s:>6}"
                  f"  rows {r.get('row_length_candidates')}  sweep {r.get('sweep_s_median', '-')}s"
                  f" / {r.get('sweep_oct_median', '-')} oct  rms {r.get('rms_db')}")
        stats[prof] = {'summary': summary(rows), 'tracks': rows}
        print(prof, json.dumps(stats[prof]['summary']))
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(stats, f, indent=1, ensure_ascii=False)


if __name__ == '__main__':
    main()
