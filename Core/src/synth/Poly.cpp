/**
 * @file Poly.cpp
 * @brief The pad synth (Poly.h).
 *
 * Everything runs sample by sample on the sample count's grid -- the control values every sixteen samples, the
 * drift every 512, each key drawing its drift from its own generator -- so the sound does not depend on how the
 * host cuts the blocks (the engine's rule).
 */
#include "eph/synth/Poly.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr int kControl = 16;    ///< samples between two control updates
constexpr int kDrift = 512;     ///< samples between two steps of the drift
}

void PolySynth::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate;
    prepareWavetables();
    rng_.seed(seed);
    for (int i = 0; i < kKeys; ++i) {
        keys_[i] = Key{};
        keys_[i].rng.seed(mixSeed(seed, 100 + static_cast<uint64_t>(i)));
        for (FilterLane& f : keys_[i].filt) f.allocate(2048);   // the comb's lines (24 Hz at 48 kHz)
    }
    size_t len = 1;
    while (len < static_cast<size_t>(0.05 * sampleRate)) len <<= 1;
    line_.assign(len, 0.0f);
    mask_ = len - 1;
    write_ = 0;
    order_ = 0;
    scanPhase_ = chorusPhase_ = 0.0;
    ring_ = 0.0f;
    count_ = 0;
    set(s_);
}

void PolySynth::set(const PolySettings& s)
{
    // A new filter model starts from rest.
    if (s.filter != s_.filter)
        for (Key& k : keys_) for (FilterLane& f : k.filt) f.clear();
    s_ = s;
    table_ = &wavetable(s.table);
    times_.setTimes(std::max(0.005f, s.attackS), 1.0f, 1.0f, std::max(0.05f, s.releaseS));
    for (Key& k : keys_) k.env.copyTimes(times_);
}

void PolySynth::retune(Key& k)
{
    for (int o = 0; o < 2; ++o) {
        const double cents = (o == 0 ? -0.5 : 0.5) * static_cast<double>(s_.detuneCents) + static_cast<double>(k.drift[o]);
        const double hz = midiToHz(static_cast<double>(k.pitch) + cents / 100.0);
        k.inc[o] = hz / sr_;
        k.level[o] = cycleLevelFor(hz, sr_, k.level[o]);
    }
}

void PolySynth::noteOn(int pitch, float velocity, int id)
{
    Key* k = nullptr;
    for (Key& c : keys_) if (!c.on) { k = &c; break; }
    if (k == nullptr) {
        k = &keys_[0];
        for (Key& c : keys_) if (c.order < k->order) k = &c;
    }
    const bool fresh = !k->on;
    k->on = true;
    k->held = true;
    k->pitch = pitch;
    k->id = id;
    k->velocity = velocity;
    k->order = ++order_;
    if (fresh) {
        // Free-running oscillators: a new key starts wherever, so a chord is never phase-locked.
        k->phase[0] = rng_.uniform();
        k->phase[1] = rng_.uniform();
        k->drift[0] = s_.driftCents * rng_.bipolar();
        k->drift[1] = s_.driftCents * rng_.bipolar();
        k->filt[0].clear();
        k->filt[1].clear();
        k->level[0] = k->level[1] = -1;
    }
    k->scanOffset = rng_.uniform();
    k->env.copyTimes(times_);
    k->env.noteOn();
    retune(*k);
}

void PolySynth::noteOff(int id)
{
    for (Key& k : keys_)
        if (k.on && k.held && k.id == id) { k.held = false; k.env.noteOff(); }
}

void PolySynth::silence()
{
    for (Key& k : keys_) { k.on = false; k.held = false; k.env.kill(); }
    std::fill(line_.begin(), line_.end(), 0.0f);
    ring_ = 0.0f;
}

bool PolySynth::active() const
{
    for (const Key& k : keys_) if (k.on) return true;
    return ring_ > 1e-5f;
}

void PolySynth::process(float* L, float* R, int n)
{
    const double scanStep = static_cast<double>(s_.scanHz) * kControl / sr_;
    const double chorusInc = 0.45 / sr_;
    const float spread = std::clamp(s_.spread, 0.0f, 1.0f), cross = 1.0f - spread;
    const float chorus = std::clamp(s_.chorus, 0.0f, 1.0f);
    const float base = static_cast<float>(0.007 * sr_), depth = static_cast<float>(0.0025 * sr_) * chorus;
    const float wet = 0.7f * chorus, norm = 1.0f / (1.0f + 0.5f * chorus);
    const float keyGain = 0.3f / (1.0f + cross);
    const bool hasTable = table_ != nullptr && !table_->empty();
    const FilterModel model = static_cast<FilterModel>(std::clamp(s_.filter, 0, kFilterModels - 1));
    const float fk = FilterVoicing::feedback(model, std::clamp(s_.resonance, 0.0f, 1.0f));
    for (int i = 0; i < n; ++i) {
        const int64_t t = count_ + i;
        const bool control = t % kControl == 0, drift = t % kDrift == 0;
        if (control) {
            scanPhase_ += scanStep;
            if (scanPhase_ >= 1.0) scanPhase_ -= 1.0;
        }
        float l = 0.0f, r = 0.0f;
        for (Key& k : keys_) {
            if (!k.on) continue;
            if (drift && s_.driftCents > 0.0f) {
                for (float& d : k.drift) d += 0.3f * (s_.driftCents * k.rng.bipolar() - d);
                retune(k);
            }
            if (control) {
                const float scan = 0.5f * s_.scan * static_cast<float>(std::sin(6.283185307179586 * (scanPhase_ + k.scanOffset)));
                k.position = std::clamp(s_.position + scan, 0.0f, 1.0f);
                const float octs = 0.5f * static_cast<float>(k.pitch - 60) / 12.0f + s_.envOctaves * k.env.level();
                const float fc = std::min(s_.cutoffHz * std::exp2(octs), static_cast<float>(0.45 * sr_));
                k.g = std::tan(3.14159265f * fc / static_cast<float>(sr_));
            }
            const float env = k.env.process();
            if (!k.env.isActive()) { k.on = false; continue; }
            const float a = hasTable ? table_->at(k.level[0], k.position, k.phase[0]) : 0.0f;
            const float b = hasTable ? table_->at(k.level[1], k.position, k.phase[1]) : 0.0f;
            for (int o = 0; o < 2; ++o) {
                k.phase[o] += k.inc[o];
                if (k.phase[o] >= 1.0) k.phase[o] -= 1.0;
            }
            const float amp = env * k.velocity * keyGain;
            l += k.filt[0].tick(model, (a + cross * b) * amp, k.g, fk, s_.filterMode);
            r += k.filt[1].tick(model, (cross * a + b) * amp, k.g, fk, s_.filterMode);
        }
        // The ensemble: one line, read on either side with the sweep turned against itself.
        line_[write_] = 0.5f * (l + r);
        const float sw = depth * static_cast<float>(std::sin(6.283185307179586 * chorusPhase_));
        chorusPhase_ += chorusInc;
        if (chorusPhase_ >= 1.0) chorusPhase_ -= 1.0;
        auto tap = [&](float d) {
            const float pos = static_cast<float>(write_ + mask_ + 1) - d;   // one line length on: never negative
            const size_t i0 = static_cast<size_t>(pos);
            const float frac = pos - static_cast<float>(i0);
            return line_[i0 & mask_] + frac * (line_[(i0 + 1) & mask_] - line_[i0 & mask_]);
        };
        const float outL = (l + wet * tap(base + sw)) * norm, outR = (r + wet * tap(base - sw)) * norm;
        write_ = (write_ + 1) & mask_;
        L[i] = outL;
        R[i] = outR;
        ring_ = std::max(ring_ * 0.9999f, std::max(std::fabs(outL), std::fabs(outR)));
    }
    count_ += n;
}

} // namespace eph
