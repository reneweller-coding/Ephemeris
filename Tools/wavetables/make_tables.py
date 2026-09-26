"""
Writes Core/src/synth/WavetableData.cpp: the sampled wavetables of Ephemeris (Wavetable.h), taken from the
AmbientSynth (Noctuary) library.

Ephemeris keeps its wavetables in the program, so the plugin and the Quest need no files beside them. A table here
is 32 frames of 256 samples (harmonics 1 to 127 -- what a cycle of 256 samples can hold; enough for pads and
sequences, whose filters close far below that), 16 bit. The frames are thinned evenly from the source table, each
cut to its first 127 harmonics through the FFT, and the table is scaled so that its loudest sample reaches 32000.

The sources (see Library/README.md and Library/Wavetables/Classic/CREDITS-classic.md in the AmbientSynth):
  Ambient/  the library's own tables (Tools/HarmonicGen/ambientgen.py): per family the tables of the best fitness
  Classic/  AKWF (Kristoffer Ekstrand, CC0 1.0) and WaveEdit Online (CC0 1.0), picked by name

    python Tools/wavetables/make_tables.py [path to AmbientSynth/Library/Wavetables]
"""
import json
import os
import struct
import sys

import numpy as np

LIB = sys.argv[1] if len(sys.argv) > 1 else r'G:\Tools\VRAudio\AmbientSynth\Library\Wavetables'
# The VCO models' tables (Vco.h), built in Wavetable.cpp after the sampled ones.
VCO_NAMES = '    "921 Waves", "Prophet Waves", "SEM Waves", "2600 Waves", "E-mu Waves",'
OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'Core', 'src', 'synth', 'WavetableData.cpp')
FRAMES, LEN, TOP = 32, 256, 127

# (family, how many, display name) from the Ambient shelf, best fitness first.
AMBIENT = [('bowed', 3, 'Bowed'), ('tube', 3, 'Tube'), ('vowel_alto', 1, 'Alto'), ('vowel_tenor', 1, 'Tenor'),
           ('vowel_soprano', 1, 'Soprano'), ('vowel_bass', 1, 'Basso'), ('consonant', 3, 'Consonant'),
           ('overtone', 3, 'Overtones'), ('otmorph', 3, 'Morph'), ('sampled', 3, 'Sampled')]
# (file, display name) from the Classic shelf.
CLASSIC = [('akwf_fmsynth_01', 'FM Synth'), ('akwf_eorgan_01', 'E-Organ'), ('akwf_violin_01', 'Violin'),
           ('akwf_theremin_01', 'Theremin'),
           # The PPG Wave's tables as the WaveEdit users rebuilt them (grounded, smooth steps), and its upper waves.
           ('wavedit_ppg_wa00', 'PPG 00'), ('wavedit_ppg_wa03', 'PPG 03'), ('wavedit_ppg_wa06', 'PPG 06'),
           ('wavedit_ppg_wa07', 'PPG 07'), ('wavedit_ppg_wa12', 'PPG 12'), ('wavedit_ppg_wa17', 'PPG 17'),
           ('wavedit_ppg_wa22', 'PPG 22'), ('wavedit_ppg_wa25', 'PPG 25'), ('wavedit_ppg_uppe', 'PPG Upper'),
           ('wavedit_drone', 'Drone Bank'), ('wavedit_organic', 'Organic'), ('wavedit_voxsynth', 'Vox Synth')]


def read_wav(path):
    b = open(path, 'rb').read()
    i = b.find(b'fmt ')
    tag, ch, _, _, _, bits = struct.unpack('<HHIIHH', b[i + 8:i + 24])
    j = b.find(b'data')
    n = struct.unpack('<I', b[j + 4:j + 8])[0]
    raw = b[j + 8:j + 8 + n]
    if tag == 3 or (tag == 0xFFFE and bits == 32):
        x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(raw, dtype=np.int16) / 32768.0
    elif bits == 24:
        a = np.frombuffer(raw[:len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)) << 8 >> 8) / 8388608.0
    else:
        raise ValueError(path)
    return x.reshape(-1, ch).mean(axis=1)


def frames_of(path, frame_len):
    x = read_wav(path)
    total = len(x) // frame_len
    idx = [round(k * (total - 1) / (FRAMES - 1)) for k in range(FRAMES)] if total >= FRAMES else list(range(total))
    out = []
    for k in idx:
        spec = np.fft.rfft(x[k * frame_len:(k + 1) * frame_len])
        s = np.zeros(LEN // 2 + 1, dtype=complex)
        top = min(TOP, len(spec) - 1)
        s[1:top + 1] = spec[1:top + 1] * (LEN / frame_len)
        out.append(np.fft.irfft(s, LEN))
    return np.array(out)


def main():
    tables = []
    for fam, count, name in AMBIENT:
        cands = []
        for f in os.listdir(os.path.join(LIB, 'Ambient')):
            if f.startswith('ambient_' + fam + '_') and f.endswith('.json'):
                rest = f[len('ambient_' + fam + '_'):-5]
                if not rest.isdigit():
                    continue
                meta = json.load(open(os.path.join(LIB, 'Ambient', f)))
                if meta.get('grounded', True):
                    cands.append((-meta.get('fitness', 0.0), f[:-5], meta.get('frame_len', 2048)))
        cands.sort()
        for n, (_, base, flen) in enumerate(cands[:count]):
            tables.append((name if count == 1 else '%s %d' % (name, n + 1), os.path.join(LIB, 'Ambient', base + '.wav'), flen, base))
    for base, name in CLASSIC:
        meta = json.load(open(os.path.join(LIB, 'Classic', base + '.json')))
        tables.append((name, os.path.join(LIB, 'Classic', base + '.wav'), meta.get('frame_len', 2048), base))

    lines = ['/**',
             ' * @file WavetableData.cpp',
             ' * @brief The sampled wavetables (Wavetable.h): %d tables of up to %d frames of %d samples, 16 bit.' % (len(tables), FRAMES, LEN),
             ' *',
             ' * Generated by Tools/wavetables/make_tables.py from the AmbientSynth (Noctuary) library -- its own tables',
             ' * (ambientgen) and AKWF (Kristoffer Ekstrand) and WaveEdit Online, both CC0 1.0. Do not edit.',
             ' */',
             '#include "eph/synth/Wavetable.h"',
             '',
             'namespace eph {',
             'namespace {',
             '']
    entries = []
    for t, (name, path, flen, base) in enumerate(tables):
        fr = frames_of(path, flen)
        peak = np.max(np.abs(fr))
        q = np.round(fr / peak * 32000.0).astype(np.int16) if peak > 0 else np.zeros_like(fr, dtype=np.int16)
        vals = q.flatten().tolist()
        lines.append('// %s: %s' % (name, base))
        lines.append('const int16_t kT%d[] = {' % t)
        for i in range(0, len(vals), 32):
            lines.append('    ' + ','.join(str(v) for v in vals[i:i + 32]) + ',')
        lines.append('};')
        entries.append('    { "%s", %d, kT%d },' % (name, len(fr), t))
    lines += ['', '} // namespace', '',
              'const SampledTable kSampledTables[kSampledTableCount] = {'] + entries + ['};', '',
              'static_assert(sizeof(kSampledTables) / sizeof(kSampledTables[0]) == %d, "the count in Wavetable.h");' % len(tables), '',
              '// The names as literals, so the parameter tables may point at them before any constructor has run.',
              'const char* const kWavetableNames[kWavetableCount] = {',
              '    "Classic", "PWM", "Sync", "Formant", "Vocal", "Organ", "Glass", "Metal",'] +              ['    "%s",' % name for name, _, _, _ in tables] + [VCO_NAMES, '};', '',
              '// The same with "Analog" first: the choice of the row voices (voice.table; 0 plays the analog oscillators).',
              'const char* const kVoiceTableNames[kWavetableCount + 1] = {',
              '    "Analog", "Classic", "PWM", "Sync", "Formant", "Vocal", "Organ", "Glass", "Metal",'] +              ['    "%s",' % name for name, _, _, _ in tables] + [VCO_NAMES, '};',
              '', '} // namespace eph', '']
    open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print('%d tables -> %s (%d KB)' % (len(tables), os.path.normpath(OUT), os.path.getsize(OUT) // 1024))
    for name, _, _, base in tables:
        print('  %-14s %s' % (name, base))


if __name__ == '__main__':
    main()
