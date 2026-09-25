"""The harmony of the reference recordings (25.09.2026), against the weights of Core/src/compose/Harmony.cpp.
Only statistics are kept (Tools/ref_harmony.json), never audio. For every track of a profile's folders
(Tools/ref_sets.txt, decoded as Tools/analyze_ref.py does):
  * chroma in two bands from a long STFT (8192 at 11025 Hz): the bass (35..220 Hz) and the body (110..1760 Hz);
  * the centre: the pitch class the bass dwells on most (the drone's and the ostinato's root);
  * the mode: the body's mean chroma against the seven modes' pitch sets (tonic and fifth weighted), the best;
  * the bass roots: the loudest bass pitch class per 2-second block (blocks under -50 dB left out), smoothed by a
    median of three, runs of the same root as chords (a run shorter than two blocks joins the one before);
  * from them: the share of the time on the centre, the chords' lengths in bars (the tempo from ref_stats.json,
    else 120 BPM), the degrees (semitones from the centre) the bass moves to, the moves from the centre and back,
    and the track's class: one root (Static), two (Pendulum), three or four in a returning order (Loop), else Walk.
Usage:
  python Tools/analyze_harmony.py [--profiles Cosmic,Melodic] [--max-tracks 8]
"""
import argparse, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_ref import decode, read_sets, tracks_of, SR, HERE

MODES = {  # compose.scale order, pitch sets from the centre
    'Aeolian': [0, 2, 3, 5, 7, 8, 10], 'Dorian': [0, 2, 3, 5, 7, 9, 10], 'Phrygian': [0, 1, 3, 5, 7, 8, 10],
    'Harmonic Minor': [0, 2, 3, 5, 7, 8, 11], 'Mixolydian': [0, 2, 4, 5, 7, 9, 10], 'Lydian': [0, 2, 4, 6, 7, 9, 11],
    'Locrian': [0, 1, 3, 5, 6, 8, 10],
}
DEGREE = {0: 'i', 1: 'bII', 2: 'ii', 3: 'III', 4: 'III+', 5: 'iv', 6: 'tritone', 7: 'v', 8: 'VI', 9: 'vi', 10: 'VII', 11: 'vii'}


def chroma(x, lo, hi, n=8192, hop=2048):
    win = np.hanning(n)
    frames = 1 + max(0, (len(x) - n) // hop)
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    sel = (freqs >= lo) & (freqs <= hi)
    pcs = (np.round(12 * np.log2(freqs[sel] / 440.0)) + 9) % 12
    out = np.zeros((frames, 12))
    for f in range(frames):
        seg = x[f * hop: f * hop + n]
        if len(seg) < n:
            break
        p = np.abs(np.fft.rfft(seg * win))[sel] ** 2
        out[f] = np.bincount(pcs.astype(int), weights=p, minlength=12)
    return out


def analyse(x, bpm):
    bass, body = chroma(x, 35.0, 220.0), chroma(x, 110.0, 1760.0)
    centre = int(np.argmax(bass.sum(axis=0)))
    prof = np.roll(body.mean(axis=0), -centre)
    prof = prof / (prof.sum() + 1e-12)
    scores = {}
    for name, pcs in MODES.items():
        t = np.zeros(12)
        t[pcs] = 1.0
        t[0] += 1.0
        t[7] += 0.5
        scores[name] = float(np.corrcoef(prof, t)[0, 1])
    mode = max(scores, key=scores.get)
    # Bass roots per 2-second block (about ten frames of 0.19 s).
    per = max(1, int(round(2.0 / (2048 / SR))))
    blocks = len(bass) // per
    energy = np.array([bass[b * per:(b + 1) * per].sum() for b in range(blocks)])
    floor = energy.max() * 1e-5 if blocks else 0
    roots = [int((np.argmax(bass[b * per:(b + 1) * per].sum(axis=0)) - centre) % 12) if energy[b] > floor else -1 for b in range(blocks)]
    sm = roots[:]
    for b in range(1, blocks - 1):
        trio = [r for r in roots[b - 1:b + 2] if r >= 0]
        if len(trio) == 3:
            sm[b] = int(np.median(trio)) if trio[0] != trio[2] else trio[0]
    runs = []
    for r in sm:
        if r < 0:
            continue
        if runs and runs[-1][0] == r:
            runs[-1][1] += 1
        else:
            runs.append([r, 1])
    merged = []
    for r, n in runs:
        if merged and (n < 2 or merged[-1][0] == r):
            merged[-1][1] += n
        else:
            merged.append([r, n])
    voiced = sum(n for _, n in merged)
    bars_per_block = 2.0 * bpm / 240.0
    tonic = sum(n for r, n in merged if r == 0) / max(1, voiced)
    lengths = [n * bars_per_block for _, n in merged]
    degrees = {}
    for r, n in merged:
        if r != 0:
            degrees[DEGREE[r]] = degrees.get(DEGREE[r], 0) + n
    moves = {}
    for a, b in zip(merged, merged[1:]):
        key = DEGREE[a[0]] + '>' + DEGREE[b[0]]
        moves[key] = moves.get(key, 0) + 1
    used = [r for r in set(r for r, _ in merged) if sum(n for q, n in merged if q == r) >= 0.1 * voiced]
    if len(used) <= 1:
        cls = 'Static'
    elif len(used) == 2:
        cls = 'Pendulum'
    elif len(used) <= 4:
        cls = 'Loop'
    else:
        cls = 'Walk'
    return {'centre': centre, 'mode': mode, 'tonic_share': tonic, 'chord_bars': float(np.median(lengths)) if lengths else 0.0,
            'changes_per_min': 60.0 * (len(merged) - 1) / max(1.0, voiced * 2.0), 'degrees': degrees, 'moves': moves, 'class': cls}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sets', default=os.path.join(HERE, 'ref_sets.txt'))
    ap.add_argument('--profiles', default='')
    ap.add_argument('--max-tracks', type=int, default=8)
    ap.add_argument('--out', default=os.path.join(HERE, 'ref_harmony.json'))
    a = ap.parse_args()
    sets = read_sets(a.sets)
    tempo = {}
    try:
        with open(os.path.join(HERE, 'ref_stats.json'), encoding='utf-8') as f:
            stats = json.load(f)
        for prof, s in stats.items():
            if isinstance(s, dict) and 'summary' in s and s['summary'].get('bpm'):
                tempo[prof] = float(s['summary']['bpm'])
    except (OSError, ValueError, KeyError, TypeError):
        pass
    result = {}
    for prof, folders in sets.items():
        if a.profiles and prof not in a.profiles.split(','):
            continue
        rows = []
        for path in tracks_of(folders, a.max_tracks):
            try:
                x = decode(path)
            except Exception as e:
                print('  skip', os.path.basename(path), e)
                continue
            if len(x) < SR * 60:
                continue
            r = analyse(x, tempo.get(prof, 120.0))
            rows.append(r)
            print(f"  {prof:8s} {os.path.basename(path)[:44]:44s} {r['mode']:15s} i {r['tonic_share']:.2f} bars {r['chord_bars']:5.1f} {r['class']}")
        if not rows:
            continue
        deg, mv, modes, classes = {}, {}, {}, {}
        for r in rows:
            for k, v in r['degrees'].items():
                deg[k] = deg.get(k, 0) + v
            for k, v in r['moves'].items():
                mv[k] = mv.get(k, 0) + v
            modes[r['mode']] = modes.get(r['mode'], 0) + 1
            classes[r['class']] = classes.get(r['class'], 0) + 1
        tot = sum(deg.values()) or 1
        result[prof] = {
            'tracks': len(rows),
            'tonic_share': float(np.median([r['tonic_share'] for r in rows])),
            'chord_bars': float(np.median([r['chord_bars'] for r in rows])),
            'changes_per_min': float(np.median([r['changes_per_min'] for r in rows])),
            'degrees': {k: round(v / tot, 3) for k, v in sorted(deg.items(), key=lambda kv: -kv[1])},
            'moves': dict(sorted(mv.items(), key=lambda kv: -kv[1])[:12]),
            'modes': modes, 'classes': classes,
        }
        s = result[prof]
        print(f"{prof}: {s['tracks']} tracks, on i {s['tonic_share']:.2f}, chords {s['chord_bars']:.1f} bars, "
              f"{s['changes_per_min']:.1f}/min; classes {s['classes']}; modes {s['modes']}; degrees {list(s['degrees'].items())[:5]}")
    with open(a.out, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=1, ensure_ascii=False)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
