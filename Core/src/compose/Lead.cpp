/**
 * @file Lead.cpp
 * @brief Lead phrases by rule.
 */
#include "eph/compose/Lead.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {

/** @brief Pitch classes (relative to the root) of the scale, and of the pentatonic within it. */
void pitchSets(int scale, bool* inScaleSet, bool* pentatonic)
{
    for (int i = 0; i < 12; ++i) inScaleSet[i] = pentatonic[i] = false;
    const int n = scaleSize(scale);
    for (int d = 0; d < n; ++d) inScaleSet[scaleSemitones(scale, d) % 12] = true;
    for (int pc : { 0, 3, 5, 7, 10 }) pentatonic[pc] = inScaleSet[pc] || pc == 0;
    // Where the scale has no minor third or seventh (none of ours), the pentatonic keeps what it has.
}

/** @brief The allowed pitches in the register, under a root. */
std::vector<int> allowed(int low, int high, int rootPc, const bool* set)
{
    std::vector<int> out;
    for (int p = low; p <= high; ++p)
        if (set[pitchClass(p - rootPc)]) out.push_back(p);
    return out;
}

int nearestIndex(const std::vector<int>& v, int pitch)
{
    int best = 0;
    for (int i = 1; i < static_cast<int>(v.size()); ++i)
        if (std::abs(v[static_cast<size_t>(i)] - pitch) < std::abs(v[static_cast<size_t>(best)] - pitch)) best = i;
    return best;
}

} // namespace

bool inScale(int pitch, int rootPc, int scale)
{
    bool sc[12], pent[12];
    pitchSets(scale, sc, pent);
    return sc[pitchClass(pitch - rootPc)];
}

void writeLead(Score& score, const LeadPlan& plan, double from, double to, Rng& rng)
{
    bool sc[12], pent[12];
    pitchSets(plan.scale, sc, pent);
    bool chord[12] = {};
    chord[0] = chord[7] = true;
    chord[sc[3] ? 3 : 4] = true;
    const int centre = (plan.low + plan.high) / 2;

    double t = std::ceil(from / kBeatsPerBar - 1e-9) * kBeatsPerBar;
    int cur = centre;
    std::vector<double> lastRhythm;
    std::vector<int> lastSteps;
    int lastBars = 0;
    while (t < to - kBeatsPerBar) {
        // A repeated phrase keeps the length of the one it repeats, so its rhythm fits.
        const bool repeat = !lastRhythm.empty() && rng.uniform() < 0.4f;
        const int bars = repeat ? lastBars : (rng.uniform() < 0.55f ? 2 : 4);
        const double end = std::min(t + bars * kBeatsPerBar, to);
        const int rootPc = pitchClass(plan.keyRoot + rootShiftAt(plan.shifts, t));
        const std::vector<int> pentSet = allowed(plan.low, plan.high, rootPc, pent);
        const std::vector<int> fullSet = allowed(plan.low, plan.high, rootPc, sc);
        if (pentSet.size() < 3) break;

        // Rhythm: a repeat of the last phrase's, or new cells; the last note is held to the phrase end.
        std::vector<double> rhythm;
        if (repeat) rhythm = lastRhythm;
        else {
            double pos = rng.uniform() < 0.3f ? 0.5 : 0.0;   // sometimes in on the "and"
            if (pos > 0.0) rhythm.push_back(-pos);            // a negative entry is a rest
            while (pos < (end - t) - 2.0) {
                const float u = rng.uniform();
                const float dense = plan.intensity;
                double len;
                if (u < 0.25f - 0.15f * dense) len = 2.0;
                else if (u < 0.5f - 0.1f * dense) len = 1.5;
                else if (u < 0.8f) len = 1.0;
                else len = 0.5;
                if (len == 0.5) { rhythm.push_back(0.5); rhythm.push_back(0.5); pos += 1.0; continue; }
                rhythm.push_back(len);
                pos += len;
            }
            rhythm.push_back(std::max(1.0, (end - t) - pos));
        }

        // Pitches: an arc over the pentatonic, steps with the odd leap; chord tones on the strong beats.
        int idx = nearestIndex(pentSet, repeat ? cur : centre + static_cast<int>(rng.below(7)) - 3);
        std::vector<int> steps;
        double pos = 0.0;
        const int notes = static_cast<int>(rhythm.size());
        for (int k = 0; k < notes; ++k) {
            const double len = rhythm[static_cast<size_t>(k)];
            if (len < 0.0) { pos += -len; steps.push_back(0); continue; }
            int step;
            if (repeat && k < static_cast<int>(lastSteps.size())) step = lastSteps[static_cast<size_t>(k)];
            else {
                const bool rising = pos < 0.5 * (end - t);
                const float u = rng.uniform();
                step = u < 0.15f ? 0 : (u < 0.75f ? 1 : (u < 0.92f ? 2 : 3));
                if (!rising) step = -step;
                if (rng.uniform() < 0.2f) step = -step;
            }
            steps.push_back(step);
            idx = std::clamp(idx + step, 0, static_cast<int>(pentSet.size()) - 1);
            int pitch = pentSet[static_cast<size_t>(idx)];
            const double beat = t + pos;
            const bool strong = std::fmod(beat, 2.0) < 1e-6;
            const bool last = k == notes - 1;
            if ((strong || last) && !chord[pitchClass(pitch - rootPc)]) {
                // Move to the nearest chord tone of the register.
                for (int d = 1; d < 5; ++d) {
                    if (pitch + d <= plan.high && chord[pitchClass(pitch + d - rootPc)]) { pitch += d; break; }
                    if (pitch - d >= plan.low && chord[pitchClass(pitch - d - rootPc)]) { pitch -= d; break; }
                }
            } else if (!strong && len <= 0.5 && rng.uniform() < 0.25f) {
                // A passing tone of the full scale, off the beat.
                const int j = nearestIndex(fullSet, pitch + (step >= 0 ? 1 : -1));
                pitch = fullSet[static_cast<size_t>(j)];
            }
            // Under a root that changes inside the phrase, the note is snapped into the new one.
            const int rootNow = pitchClass(plan.keyRoot + rootShiftAt(plan.shifts, beat));
            if (!sc[pitchClass(pitch - rootNow)]) {
                const std::vector<int> now = allowed(plan.low, plan.high, rootNow, sc);
                if (!now.empty()) pitch = now[static_cast<size_t>(nearestIndex(now, pitch))];
            }
            const double noteLen = std::min(len, to - beat);
            if (noteLen <= 0.05) break;
            // The glide from a whole tone below, at the start of a phrase or onto a long note.
            // It comes from the scale tone below (a whole tone, or a half where the scale has no whole).
            const int grace = sc[pitchClass(pitch - 2 - rootNow)] ? pitch - 2 : (sc[pitchClass(pitch - 1 - rootNow)] ? pitch - 1 : -1);
            if ((k == 0 || len >= 2.0) && rng.uniform() < 0.3f && grace >= plan.low && len >= 1.0) {
                NoteEvent g;
                g.beat = beat;
                g.length = 0.3;   // overlaps the target, so the voice glides
                g.part = Part::Lead;
                g.pitch = grace;
                g.velocity = 0.75f;
                g.slide = true;
                score.notes.push_back(g);
                NoteEvent e;
                e.beat = beat + 0.25;
                e.length = std::max(0.2, noteLen - 0.25 - 0.05);
                e.part = Part::Lead;
                e.pitch = pitch;
                e.velocity = 0.8f;
                score.notes.push_back(e);
            } else {
                NoteEvent e;
                e.beat = beat;
                e.length = std::max(0.1, noteLen * (len <= 0.5 ? 0.85 : 0.95));
                e.part = Part::Lead;
                e.pitch = pitch;
                e.velocity = 0.72f + 0.12f * rng.uniform();
                score.notes.push_back(e);
            }
            cur = pitch;
            pos += len;
        }
        lastRhythm = rhythm;
        lastSteps = steps;
        lastBars = bars;
        // A rest of one to four bars, shorter with more intensity.
        const int restBars = 1 + static_cast<int>(rng.below(4 - static_cast<int>(2.0f * plan.intensity)));
        t = end + restBars * kBeatsPerBar;
    }
}

} // namespace eph
