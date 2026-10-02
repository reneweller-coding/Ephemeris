"""The demos of Ephemeris: a track per style as MP3, and one of them as a video by KaleidoscopeEnhanced (02.10.2026).

    python Tools/demo/make_demos.py [--only NAME ...] [--no-video] [--kaleidoscope DIR] [--exe RENDERER]
    python Tools/demo/make_demos.py --publish

Every demo below is rendered with the instrument's own renderer into work/demos/ and encoded as an MP3 (ffmpeg, LAME
VBR around 190 kbit/s, title and style in the tags, a short fade at the end, and just enough less gain to keep
the encoded true peak at -1 dBTP). The demo marked VIDEO is also rendered
through KaleidoscopeEnhanced (github.com/reneweller-coding/KaleidoscopeEnhanced): its batch mode (-x) records the
visualizer to that WAV, and its score cues (-k) -- the bars, the sections, the drops and the key, written here from
what the renderer knows about the track -- put the cuts on the music instead of guessing them. The recording is then
encoded once more for the web (H.264, AAC 256 kbit/s, faststart) and a poster frame is cut from it.

KaleidoscopeEnhanced is not part of this repository. --kaleidoscope (or the environment variable KALEIDO_ROOT) names a
folder laid out like its package: bin/Kaleidoscope.exe beside Presets, Engine, FX, Scene2D, Scene3D, Transitions,
Images and Models, with the Qt DLLs beside the exe or on PATH (KALEIDO_QT adds a folder to PATH). ffmpeg must be on
PATH, or FFMPEG names it. The video renders in real time in a window of its own.

Everything lands in work/demos/: <name>.wav (the render), <name>.mp3, and for the video <name>.cues.tsv,
<name>.mp4 and <name>.jpg. --publish then puts the MP3s, the video and its poster on the GitHub release "demos"
(gh; created on first use, never marked as the latest release), where the README links to them.
"""
import argparse
import glob
import os
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'work', 'demos')

# ---------------------------------------------------------------- this instrument
NAME = 'Ephemeris'
RENDER = 'eph_render'
GENRE = 'Electronic'
FADE_IN = 0.0
# name, title, the renderer's arguments: five minutes per style
DEMOS = [
    ('cosmic', 'Cosmic', ['--seed', '7', '--minutes', '5', '--set', 'compose.style=Cosmic']),
    ('doom', 'Doom', ['--seed', '11', '--minutes', '5', '--set', 'compose.style=Doom']),
    ('melodic', 'Melodic', ['--seed', '23', '--minutes', '5', '--set', 'compose.style=Melodic']),
    ('modern', 'Modern', ['--seed', '42', '--minutes', '5', '--set', 'compose.style=Modern']),
    ('drift', 'Drift', ['--seed', '5', '--minutes', '5', '--set', 'compose.style=Drift']),
]
VIDEO = ('cosmic', 'SpaceAmbient')   # the demo that becomes a video, and KaleidoscopeEnhanced's configuration for it
CUES = 'ephemeris'                   # the tempo points the renderer prints, the phases and keys of its --cues file
# A phase -> the section KaleidoscopeEnhanced knows, its energy (0..1), and whether it lands as a drop.
SECTIONS = {
    'ATMOSPHERE': ('Intro', 0.2, False),
    'ENTRY': ('Groove', 0.45, False),
    'BUILD': ('Build', 0.6, False),
    'LEAD': ('Drop', 0.75, False),
    'PEAK': ('Drop', 0.95, True),
    'BREAKDOWN': ('Break', 0.35, False),
    'BRIDGE': ('Groove', 0.5, False),
    'CODA': ('Outro', 0.3, False),
}

# ---------------------------------------------------------------- tools


def ffmpeg():
    """The ffmpeg to run: FFMPEG, or the one on PATH."""
    exe = os.environ.get('FFMPEG') or shutil.which('ffmpeg')
    if not exe:
        sys.exit('ffmpeg not found: put it on PATH or name it in FFMPEG')
    return exe


def aac():
    """The AAC encoder for the video's sound: Windows' own (aac_mf) where ffmpeg has it -- ffmpeg's native one lets a
    dense master, Phosphene's at -9 LUFS, overshoot by 4 dB --, else the native one."""
    p = subprocess.run([ffmpeg(), '-hide_banner', '-encoders'], capture_output=True, text=True, errors='replace')
    return ['-c:a', 'aac_mf', '-b:a', '256000'] if ' aac_mf ' in p.stdout else ['-c:a', 'aac', '-b:a', '256k']


def renderer(given):
    """The renderer: --exe, else the newest bin/<preset>/RENDER.exe of this checkout."""
    if given:
        return os.path.abspath(given)
    found = glob.glob(os.path.join(ROOT, 'bin', '*', RENDER + ('.exe' if os.name == 'nt' else '')))
    if not found:
        sys.exit('no %s in bin/: build it first (build.ps1) or name it with --exe' % RENDER)
    return max(found, key=os.path.getmtime)


def wav_seconds(path):
    """The length of a WAV in seconds, from its header (PCM or float, any chunk order)."""
    with open(path, 'rb') as f:
        if f.read(4) != b'RIFF':
            raise ValueError('not a WAV: ' + path)
        f.read(8)
        rate = block = 0
        while True:
            head = f.read(8)
            if len(head) < 8:
                break
            cid, size = head[:4], struct.unpack('<I', head[4:])[0]
            if cid == b'fmt ':
                fmt = f.read(size)
                rate, block = struct.unpack('<I', fmt[4:8])[0], struct.unpack('<H', fmt[12:14])[0]
            elif cid == b'data':
                return size / float(rate * block) if rate and block else 0.0
            else:
                f.seek(size + (size & 1), 1)
    return 0.0


def headroom(wav):
    """The gain in dB (never above 0) that would bring a file's true peak down to -1 dBTP."""
    p = subprocess.run([ffmpeg(), '-hide_banner', '-nostats', '-i', wav, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                       capture_output=True, text=True, errors='replace')
    peaks = re.findall(r'Peak:\s+(-?[\d.]+|-inf) dBFS', p.stderr)
    if not peaks or peaks[-1] == '-inf':
        return 0.0
    return min(0.0, -1.0 - float(peaks[-1]))


def fades(seconds, gain=0.0):
    """The fades of a demo: in over FADE_IN seconds (if any), out over the last eight; and its headroom."""
    out = ['volume=%.2fdB' % gain] if gain < 0.0 else []
    if FADE_IN > 0:
        out.append('afade=t=in:d=%.1f' % FADE_IN)
    out.append('afade=t=out:st=%.3f:d=8' % max(0.0, seconds - 8.0))
    return ','.join(out)


# ---------------------------------------------------------------- the cues
# One line per OSC message, as KaleidoscopeEnhanced's -k reads them: the second, the address, the arguments.
ORDER = {'/phos/key': 0, '/phos/section': 1, '/phos/bar': 2, '/phos/beat': 3, '/phos/drop': 4}


class Cues:
    """The cue lines of one track, collected in any order and written sorted."""

    def __init__(self):
        """No cues yet."""
        self.lines = []

    def add(self, seconds, address, *args):
        """One message at a second of the track."""
        self.lines.append((round(seconds, 3), ORDER[address], address, [str(a) for a in args]))

    def section(self, seconds, kind, energy, drop=False):
        """A section with its energy, and a drop on the same instant if it lands as one."""
        self.add(seconds, '/phos/section', kind, '%.2f' % energy)
        if drop:
            self.add(seconds, '/phos/drop')

    def grid(self, segments, length, first_beat=0.0):
        """Beats and bars over the track: segments are (seconds, bpm, beat) where the tempo changes."""
        segments = sorted(segments)
        for i, (start, bpm, beat0) in enumerate(segments):
            end = segments[i + 1][0] if i + 1 < len(segments) else length
            k = 0
            while True:
                t = start + k * 60.0 / bpm
                if t >= end - 1e-6 or t > length:
                    break
                beat = int(round(beat0 + k - first_beat))
                if beat >= 0:
                    self.add(t, '/phos/beat', beat)
                    if beat % 4 == 0:
                        self.add(t, '/phos/bar', beat // 4)
                k += 1

    def write(self, path, title):
        """The file, sorted by time and then by kind; returns the number of lines."""
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# %s: score cues for KaleidoscopeEnhanced -k (seconds, address, arguments)\n' % title)
            for t, _, address, args in sorted(self.lines, key=lambda x: (x[0], x[1])):
                f.write('\t'.join(['%.3f' % t, address] + args) + '\n')
        return len(self.lines)


def build_before(cues_at, bars, seconds_per_bar):
    """A Build eight bars before every drop that follows a break of at least that long."""
    extra = []
    for i, (t, kind, energy, drop) in enumerate(cues_at):
        if not drop or i == 0:
            continue
        prev_t, prev_kind = cues_at[i - 1][0], cues_at[i - 1][1]
        at = t - bars * seconds_per_bar
        if prev_kind == 'Break' and at > prev_t + 1e-3:
            extra.append((at, 'Build', 0.6, False))
    return sorted(cues_at + extra)


def cues_rekordbox(wav, length):
    """Totality and Parhelion: the beat grid and the key from <wav>.rekordbox.xml, the sections from <wav>.cues.json."""
    import json
    xml = open(wav + '.rekordbox.xml', encoding='utf-8').read()
    segments = [(float(a), float(b), 0.0) for a, b in re.findall(r'<TEMPO Inizio="([\d.]+)" Bpm="([\d.]+)"', xml)]
    beat = 0.0
    for i in range(1, len(segments)):   # the beat each tempo starts on, counted through the ones before it
        beat += (segments[i][0] - segments[i - 1][0]) * segments[i - 1][1] / 60.0
        segments[i] = (segments[i][0], segments[i][1], round(beat))
    cues = Cues()
    cues.grid(segments, length)
    key = re.search(r'Tonality="([^"]+)"', xml)
    if key:
        cues.add(0.0, '/phos/key', key.group(1))
    marks = [(0.0, 'Intro', 0.3, False)]
    for c in json.load(open(wav + '.cues.json', encoding='utf-8')):
        if c['label'] in SECTIONS:
            kind, energy, drop = SECTIONS[c['label']]
            marks.append((float(c['seconds']), kind, energy, drop))
    spb = 4 * 60.0 / segments[0][1]
    for t, kind, energy, drop in build_before(marks, 8, spb):
        cues.section(t, kind, energy, drop)
    return cues


def cues_ephemeris(wav, length, stdout):
    """Ephemeris: the tempo points the renderer prints, the phases and keys of its --cues file."""
    segments = [(float(s), float(b), float(beat)) for beat, b, s in
                re.findall(r'tempo point: beat ([\d.]+), ([\d.]+) BPM, at ([\d.]+) s', stdout)]
    cues = Cues()
    cues.grid(segments or [(0.0, 120.0, 0.0)], length)
    for line in open(wav + '.cues.txt', encoding='utf-8'):
        m = re.match(r'\s*([\d.]+)\s+([\d.]+) s\s+(\w+)\s+(.*?)\s+(-?\d+)\s+([-\d.]+)\s*$', line)
        if not m:
            continue
        t, kind, text = float(m.group(2)), m.group(3), m.group(4).strip()
        if kind == 'phase':
            sec, energy, drop = SECTIONS.get(text, ('Groove', 0.5, False))
            cues.section(t, sec, energy, drop)
        elif kind == 'key' and text:
            cues.add(t, '/phos/key', text)
    return cues


def cues_phosphene(wav, length, stdout):
    """Phosphene: every track's tempo, key and form as the renderer prints them (--tracks)."""
    cues = Cues()
    tracks = re.findall(r'^track\s+\d+\s+bar\s+(\d+)\s+\d+ bars\s+([\d.]+) BPM\s+(.+?)\s{2,}', stdout, re.M)
    forms = re.findall(r'form \([^)]*\):\s+(.*)$', stdout, re.M)
    if not tracks:
        return cues
    bpm = float(tracks[0][1])
    spb = 4 * 60.0 / bpm
    cues.grid([(0.0, bpm, 0.0)], length)
    for (bar0, _, key), form in zip(tracks, forms):
        start = int(bar0) * spb
        cues.add(start, '/phos/key', key.strip())
        for name, _, at, energy in re.findall(r'([A-Za-z]+)(\d+)@(\d+)\(E([\d.]+)\)', form):
            t = start + int(at) * spb
            if t < length:
                cues.section(t, name, float(energy), name == 'Drop')
    return cues


# ---------------------------------------------------------------- one demo


def render(exe, name, args):
    """Renders one demo into work/demos/<name>.wav with whatever the cues need beside it; returns the printout."""
    wav = os.path.join(OUT, name + '.wav')
    cmd = [exe] + args + ['--out', wav]
    if CUES == 'ephemeris':
        cmd += ['--cues', wav + '.cues.txt']
    elif CUES == 'phosphene':
        cmd += ['--tracks']
    print('render', name, ' '.join(args), flush=True)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors='replace')
    if p.returncode != 0 or not os.path.exists(wav):
        sys.exit('render failed (%d):\n%s%s' % (p.returncode, p.stdout[-2000:], p.stderr[-2000:]))
    return wav, p.stdout + p.stderr


def mp3(wav, name, title):
    """The MP3 of a demo, with its tags and its fades; encoded a second time a little quieter when the first one's
    true peak came out above -1 dBTP (an encoder overshoots a master that ends at -1). Returns the gain it took."""
    out = os.path.join(OUT, name + '.mp3')
    seconds = wav_seconds(wav)
    gain = 0.0
    for _ in range(2):
        subprocess.run([ffmpeg(), '-v', 'error', '-y', '-i', wav, '-af', fades(seconds, gain), '-c:a', 'libmp3lame',
                        '-q:a', '2', '-metadata', 'title=%s (%s demo)' % (title, NAME), '-metadata', 'artist=' + NAME,
                        '-metadata', 'album=%s demos' % NAME, '-metadata', 'genre=' + GENRE,
                        '-metadata', 'comment=Rendered by %s; github.com/reneweller-coding/%s' % (RENDER, NAME), out],
                       check=True)
        more = headroom(out)
        if more >= 0.0:
            break
        gain += more - 0.2
    print('  mp3 %s (%.0f s, %.1f dB)' % (out, seconds, gain), flush=True)
    return gain


def video(wav, name, title, config, kaleido, stdout, gain=0.0):
    """The video of a demo: the cues, the batch render in KaleidoscopeEnhanced, the web encode, the poster; its sound
    with the MP3's gain."""
    seconds = wav_seconds(wav)
    tsv = os.path.join(OUT, name + '.cues.tsv')
    cues = {'rekordbox': lambda: cues_rekordbox(wav, seconds),
            'ephemeris': lambda: cues_ephemeris(wav, seconds, stdout),
            'phosphene': lambda: cues_phosphene(wav, seconds, stdout)}.get(CUES, Cues)()
    n = cues.write(tsv, '%s, %s' % (NAME, title))
    print('  cues %s (%d lines)' % (tsv, n), flush=True)

    # Kaleidoscope reads 16-bit PCM; the fades go in here, so the video sounds like the MP3.
    pcm = os.path.join(OUT, name + '.video.wav')
    subprocess.run([ffmpeg(), '-v', 'error', '-y', '-i', wav, '-af', fades(seconds, gain), '-c:a', 'pcm_s16le', '-ar', '48000',
                    '-ac', '2', pcm], check=True)

    bindir = os.path.join(kaleido, 'bin')
    recordings = os.path.join(bindir, 'recordings')
    before = set(glob.glob(os.path.join(recordings, 'rec_*')))
    env = dict(os.environ)
    if os.environ.get('KALEIDO_QT'):
        env['PATH'] = os.environ['KALEIDO_QT'] + os.pathsep + env.get('PATH', '')
    cmd = [os.path.join(bindir, 'Kaleidoscope.exe'), '-x', pcm, '-c', config, '-t', '0']
    if n > 0:
        cmd += ['-k', tsv]
    print('  kaleidoscope', ' '.join(cmd[1:]), '(real time, %.0f s)' % seconds, flush=True)
    log = open(os.path.join(OUT, name + '.kaleidoscope.log'), 'w', encoding='utf-8', errors='replace')
    subprocess.run(cmd, cwd=bindir, env=env, stdout=log, stderr=subprocess.STDOUT)
    log.close()
    new = sorted(set(glob.glob(os.path.join(recordings, 'rec_*'))) - before, key=os.path.getmtime)
    if not new:
        sys.exit('KaleidoscopeEnhanced left no recording (see %s.kaleidoscope.log)' % name)
    rec = new[-1]

    # Its own mux (video.mp4 + audio.wav -> kaleidoscope.mp4) runs detached after it quits; once that file has stopped
    # growing, video.mp4 is complete. The recorded audio.wav is the one the frames were made to, so it is used as is.
    final = os.path.join(rec, 'kaleidoscope.mp4')
    last, still = -1, 0
    for _ in range(600):
        size = os.path.getsize(final) if os.path.exists(final) else -1
        still = still + 1 if size > 0 and size == last else 0
        if still >= 5:
            break
        last = size
        time.sleep(1)
    out = os.path.join(OUT, name + '.mp4')
    subprocess.run([ffmpeg(), '-v', 'error', '-y', '-i', os.path.join(rec, 'video.mp4'), '-i', os.path.join(rec, 'audio.wav'),
                    '-map', '0:v:0', '-map', '1:a:0', '-vf', "scale='min(1920,iw)':-2", '-c:v', 'libx264',
                    '-preset', 'slow', '-crf', '23', '-maxrate', '6M', '-bufsize', '12M', '-pix_fmt', 'yuv420p'] + aac() + ['-shortest',
                    '-movflags', '+faststart', '-metadata', 'title=%s (%s demo)' % (title, NAME),
                    '-metadata', 'artist=' + NAME, '-metadata',
                    'comment=Music by %s, pictures by KaleidoscopeEnhanced (%s)' % (NAME, config), out], check=True)
    jpg = os.path.join(OUT, name + '.jpg')
    subprocess.run([ffmpeg(), '-v', 'error', '-y', '-ss', '%.1f' % min(90.0, seconds * 0.4), '-i', out, '-frames:v', '1',
                    '-vf', 'scale=1280:-2', '-q:v', '3', jpg], check=True)
    os.remove(pcm)
    print('  video %s (from %s), poster %s' % (out, os.path.basename(rec), jpg), flush=True)
    return out


def publish():
    """The MP3s, the video and its poster on the release "demos" (gh), which is created the first time."""
    files = [os.path.join(OUT, n + '.mp3') for n, _, _ in DEMOS]
    files += [os.path.join(OUT, VIDEO[0] + e) for e in ('.mp4', '.jpg')]
    missing = [f for f in files if not os.path.exists(f)]
    if missing:
        sys.exit('not rendered yet: ' + ', '.join(os.path.basename(f) for f in missing))
    lines = ['Demos of %s, rendered by its own %s from the settings below -- nothing played in, nothing edited.' % (NAME, RENDER),
             '', '| demo | file | renderer arguments |', '|---|---|---|']
    for n, title, args in DEMOS:
        lines.append('| %s | `%s.mp3` | `%s` |' % (title, n, ' '.join('"%s"' % x if ' ' in x else x for x in args)))
    lines += ['', 'The video (`%s.mp4`) is the %s demo with pictures by '
              '[KaleidoscopeEnhanced](https://github.com/reneweller-coding/KaleidoscopeEnhanced) (configuration %s), '
              "its cuts placed by the track's own score cues. Made by `Tools/demo/make_demos.py`."
              % (VIDEO[0], dict((n, t) for n, t, _ in DEMOS)[VIDEO[0]], VIDEO[1])]
    notes = os.path.join(OUT, 'release.md')
    with open(notes, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    if subprocess.run(['gh', 'release', 'view', 'demos'], cwd=ROOT, capture_output=True).returncode != 0:
        subprocess.run(['gh', 'release', 'create', 'demos', '--title', '%s demos' % NAME, '--notes-file', notes,
                        '--latest=false'], cwd=ROOT, check=True)
    else:
        subprocess.run(['gh', 'release', 'edit', 'demos', '--notes-file', notes], cwd=ROOT, check=True)
    subprocess.run(['gh', 'release', 'upload', 'demos', '--clobber'] + files, cwd=ROOT, check=True)
    print('published %d files on the release "demos"' % len(files))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--only', nargs='*', help='only these demos (by name)')
    ap.add_argument('--no-video', action='store_true', help='the MP3s only')
    ap.add_argument('--kaleidoscope', default=os.environ.get('KALEIDO_ROOT', ''),
                    help='the KaleidoscopeEnhanced folder (bin/Kaleidoscope.exe beside Presets, ...); or KALEIDO_ROOT')
    ap.add_argument('--exe', help='the renderer (default: the newest bin/*/%s)' % RENDER)
    ap.add_argument('--publish', action='store_true', help='put what is rendered on the release "demos" and stop')
    a = ap.parse_args()
    if a.publish:
        publish()
        return
    os.makedirs(OUT, exist_ok=True)
    exe = renderer(a.exe)
    want_video = not a.no_video
    if want_video and not os.path.exists(os.path.join(a.kaleidoscope, 'bin', 'Kaleidoscope.exe')):
        sys.exit('no KaleidoscopeEnhanced at "%s": name its folder with --kaleidoscope or KALEIDO_ROOT, '
                 'or render the MP3s alone with --no-video' % a.kaleidoscope)
    for name, title, args in DEMOS:
        if a.only and name not in a.only:
            continue
        wav, stdout = render(exe, name, args)
        gain = mp3(wav, name, title)
        if want_video and name == VIDEO[0]:
            video(wav, name, title, VIDEO[1], a.kaleidoscope, stdout, gain)


if __name__ == '__main__':
    main()
