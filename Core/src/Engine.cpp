/**
 * @file Engine.cpp
 * @brief The engine of Phase 1: rows, voices, gestures, mixer, tape echo.
 */
#include "eph/Engine.h"
#include "eph/Dsp.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr int kCell = 32;   ///< the parameter raster in samples
}

void Engine::prepare(double sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max(1, maxBlock);
    sample_ = 0;
}

void Engine::load(const Score& score)
{
    score_ = score;
    score_.sort();
    sample_ = 0;
    evCursor_ = 0;

    // Notes of the rows onto the sample grid. A note whose predecessor in the same row slides into it
    // is legato: the voice glides instead of retriggering.
    events_.clear();
    int id = 0;
    bool slideInto[kRows] = {};
    for (const NoteEvent& n : score_.notes) {
        const int row = static_cast<int>(n.part) - static_cast<int>(Part::Row1);
        if (row < 0 || row >= kRows) continue;
        const int64_t on = std::llround(score_.tempo.secondsAt(n.beat) * sampleRate_);
        const int64_t off = std::max(on + 1, static_cast<int64_t>(std::llround(score_.tempo.secondsAt(n.beat + n.length) * sampleRate_)));
        ++id;
        events_.push_back({ on, 1, static_cast<uint8_t>(row), n.accent, slideInto[row], n.pitch, n.velocity, id });
        events_.push_back({ off, 0, static_cast<uint8_t>(row), false, false, n.pitch, 0.0f, id });
        slideInto[row] = n.slide;
    }
    std::stable_sort(events_.begin(), events_.end(), [](const Ev& a, const Ev& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.on < b.on;
    });

    // Gestures grouped by the knob they move.
    tracks_.clear();
    trackOf_.assign(static_cast<size_t>(params_.count()), -1);
    for (const Gesture& g : score_.gestures) {
        if (g.param < 0 || g.param >= params_.count()) continue;
        int& t = trackOf_[static_cast<size_t>(g.param)];
        if (t < 0) { t = static_cast<int>(tracks_.size()); tracks_.push_back({ g.param, {}, 0, 0.0f }); }
        tracks_[static_cast<size_t>(t)].gestures.push_back(g);
    }
    for (Track& t : tracks_) t.cursor = t.gestures.size();

    for (int r = 0; r < kRows; ++r) voices_[r].prepare(sampleRate_, mixSeed(score_.seed, 100 + static_cast<uint64_t>(r)));
    echo_.prepare(sampleRate_, 2.5, mixSeed(score_.seed, 200));
    updateCell();
}

float Engine::played(int id) const
{
    const float knob = params_.get(id);
    const int t = id >= 0 && id < static_cast<int>(trackOf_.size()) ? trackOf_[static_cast<size_t>(id)] : -1;
    if (t < 0) return knob;
    const float off = tracks_[static_cast<size_t>(t)].offset;
    if (off == 0.0f) return knob;
    const float norm = std::clamp(params_.toNormalised(id, knob) + off, 0.0f, 1.0f);
    return params_.fromNormalised(id, norm);
}

void Engine::updateCell()
{
    // Gesture offsets at this cell's beat.
    const double beat = score_.tempo.beatAt(seconds());
    for (Track& t : tracks_) {
        // Positions only move forward, so the cursor only moves forward: the latest gesture that has
        // started is the one that counts (Score::gestureOffset), gestures.size() while none has.
        const size_t none = t.gestures.size();
        size_t c = t.cursor;
        size_t next = c == none ? 0 : c + 1;
        while (next < none && t.gestures[next].beat <= beat) c = next++;
        t.cursor = c;
        t.offset = c == none ? 0.0f : gestureValue(t.gestures[c], beat);
    }

    for (int r = 0; r < kRows; ++r) {
        auto v = [&](int index) { return played(params_.id(Module::Voice, r, index)); };
        VoiceSettings s;
        s.wave = v(voice::Wave);
        s.detuneCents = v(voice::Detune);
        s.pulseWidth = v(voice::PulseWidth);
        s.driftCents = v(voice::Drift);
        s.driveDb = v(voice::Drive);
        s.cutoffHz = v(voice::Cutoff);
        s.resonance = v(voice::Resonance);
        s.envOctaves = v(voice::EnvAmount);
        s.decayMs = v(voice::Decay);
        s.keyTrack = v(voice::KeyTrack);
        s.accent = v(voice::Accent);
        s.releaseMs = v(voice::AmpDecay);
        s.glideMs = v(voice::Glide);
        voices_[r].set(s);
        running_[r] = voices_[r].active() || voices_[r].held();
        auto rp = [&](int index) { return played(params_.id(Module::Row, r, index)); };
        const float level = dbToGain(rp(row::Level));
        const float pan = std::clamp(rp(row::Pan), -1.0f, 1.0f);
        const float a = (pan + 1.0f) * 0.25f * kPi;
        gainL_[r] = level * std::cos(a);
        gainR_[r] = level * std::sin(a);
        send_[r] = rp(row::EchoSend);
    }
    auto e = [&](int index) { return played(params_.id(Module::Echo, 0, index)); };
    EchoSettings es;
    const double bpm = score_.tempo.bpmAt(beat);
    es.delaySeconds = echoTimeBeats(static_cast<EchoTime>(static_cast<int>(e(echo::Time)))) * 60.0 / bpm;
    es.feedback = e(echo::Feedback);
    es.toneHz = e(echo::Tone);
    es.wowMs = e(echo::Wow);
    es.flutterMs = e(echo::Flutter);
    es.driveDb = e(echo::Drive);
    es.pingPong = e(echo::PingPong) >= 0.5f;
    echo_.set(es);
    echoReturn_ = dbToGain(e(echo::Return));
    master_ = dbToGain(played(params_.id(Module::Master, 0, master::Level)));
}

void Engine::renderSpan(float* L, float* R, int n)
{
    float buf[kCell], sendL[kCell], sendR[kCell], wetL[kCell], wetR[kCell];
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);
    std::fill(sendL, sendL + n, 0.0f);
    std::fill(sendR, sendR + n, 0.0f);
    for (int r = 0; r < kRows; ++r) {
        ModVoice& v = voices_[r];
        if (!running_[r]) continue;
        v.process(buf, n);
        const float gl = gainL_[r], gr = gainR_[r], s = send_[r];
        for (int i = 0; i < n; ++i) {
            L[i] += buf[i] * gl;
            R[i] += buf[i] * gr;
            sendL[i] += buf[i] * gl * s;
            sendR[i] += buf[i] * gr * s;
        }
    }
    std::fill(wetL, wetL + n, 0.0f);
    std::fill(wetR, wetR + n, 0.0f);
    echo_.process(sendL, sendR, wetL, wetR, n);
    for (int i = 0; i < n; ++i) {
        L[i] = (L[i] + wetL[i] * echoReturn_) * master_;
        R[i] = (R[i] + wetR[i] * echoReturn_) * master_;
    }
}

bool Engine::process(float* L, float* R, int n)
{
    int done = 0;
    while (done < n) {
        if ((sample_ % kCell) == 0) updateCell();
        // Every event due at this sample, offs before ons.
        while (evCursor_ < events_.size() && events_[evCursor_].sample <= sample_) {
            const Ev& e = events_[evCursor_++];
            if (e.on) { voices_[e.row].noteOn(e.pitch, e.velocity, e.accent, e.legato, e.id); running_[e.row] = true; }
            else voices_[e.row].noteOff(e.id);
        }
        int64_t end = std::min<int64_t>(sample_ + (n - done), (sample_ / kCell + 1) * kCell);
        if (evCursor_ < events_.size()) end = std::min(end, events_[evCursor_].sample);
        const int len = static_cast<int>(end - sample_);
        renderSpan(L + done, R + done, len);
        sample_ += len;
        done += len;
    }
    return seconds() < lengthSeconds();
}

} // namespace eph
