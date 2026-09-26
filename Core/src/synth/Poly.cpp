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
#include <cstring>

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
        keys_[i].mod.prepare(sampleRate, mixSeed(seed, 200 + static_cast<uint64_t>(i)));
        keys_[i].mod.set(s_.mod);
    }
    times_.setSampleRate(sampleRate);
    ftimes_.setSampleRate(sampleRate);
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
    const bool modMoved = std::memcmp(&s.mod, &s_.mod, sizeof(ModSettings)) != 0;
    s_ = s;
    table_ = &wavetable(s.table);
    times_.setTimes(std::max(0.005f, s.attackS), s.ampDecayS, std::clamp(s.ampSustain, 0.0f, 1.0f), std::max(0.05f, s.releaseS));
    ftimes_.setTimes(s.filtAttackS, s.filtDecayS, std::clamp(s.filtSustain, 0.0f, 1.0f), s.filtReleaseS);
    for (Key& k : keys_) {
        k.env.copyTimes(times_);
        k.fenv.copyTimes(ftimes_);
        if (modMoved) k.mod.set(s.mod);
    }
}

void PolySynth::retune(Key& k)
{
    for (int o = 0; o < 2; ++o) {
        double cents = (o == 0 ? -0.5 : 0.5) * static_cast<double>(s_.detuneCents) + static_cast<double>(k.drift[o]);
        if (k.mod.active() && k.mod.targets(ModDest::Pitch)) cents += 100.0 * static_cast<double>(k.mo[static_cast<int>(ModDest::Pitch)]);
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
    k->fenv.copyTimes(ftimes_);
    k->fenv.noteOn();
    k->mod.noteOn(count_, beatAt(count_));
    retune(*k);
}

void PolySynth::noteOff(int id)
{
    for (Key& k : keys_)
        if (k.on && k.held && k.id == id) { k.held = false; k.env.noteOff(); k.fenv.noteOff(); k.mod.noteOff(); }
}

void PolySynth::silence()
{
    for (Key& k : keys_) { k.on = false; k.held = false; k.env.kill(); k.fenv.kill(); k.mod.kill(); }
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
            const bool linked = s_.filtLink != 0;
            const bool mod = k.mod.active();
            if (control) {
                const float fe = linked ? k.env.level() : k.fenv.level();
                if (mod) {
                    // The key's matrix at this control step (Modulation.h).
                    float ext[kModSources] = {};
                    ext[static_cast<int>(ModSource::FilterEnv)] = fe;
                    ext[static_cast<int>(ModSource::Velocity)] = k.velocity;
                    k.mod.evaluate(t, beatAt(t), ext, k.mo);
                    if (k.mod.targets(ModDest::Pitch)) retune(k);
                    k.fk = FilterVoicing::feedback(model, std::clamp(s_.resonance + k.mod.offset(k.mo, ModDest::Resonance), 0.0f, 1.0f));
                    k.mode = std::clamp(s_.filterMode + k.mod.offset(k.mo, ModDest::FilterMode), 0.0f, 1.0f);
                    k.lv = std::clamp(1.0f + k.mod.offset(k.mo, ModDest::Level), 0.0f, 2.0f);
                    const float p = std::clamp(k.mod.offset(k.mo, ModDest::Pan), -1.0f, 1.0f);
                    k.panL = std::min(1.0f, 1.0f - p);
                    k.panR = std::min(1.0f, 1.0f + p);
                }
                const float scan = 0.5f * s_.scan * static_cast<float>(std::sin(6.283185307179586 * (scanPhase_ + k.scanOffset)));
                float pos = s_.position + scan;
                if (mod && k.mod.targets(ModDest::TablePos)) pos += k.mo[static_cast<int>(ModDest::TablePos)];
                k.position = std::clamp(pos, 0.0f, 1.0f);
                float envOct = s_.envOctaves * fe;
                if (s_.envVelocity > 0.0f) envOct *= 1.0f - s_.envVelocity + s_.envVelocity * k.velocity;
                float octs = 0.5f * static_cast<float>(k.pitch - 60) / 12.0f + envOct;
                if (mod && k.mod.targets(ModDest::Cutoff)) octs += k.mo[static_cast<int>(ModDest::Cutoff)];
                const float fc = std::min(s_.cutoffHz * std::exp2(octs), static_cast<float>(0.45 * sr_));
                k.g = std::tan(3.14159265f * fc / static_cast<float>(sr_));
            }
            const float env = k.env.process();
            if (!linked) k.fenv.process();
            k.mod.tick();
            if (!k.env.isActive()) { k.on = false; continue; }
            const float a = hasTable ? table_->at(k.level[0], k.position, k.phase[0]) : 0.0f;
            const float b = hasTable ? table_->at(k.level[1], k.position, k.phase[1]) : 0.0f;
            for (int o = 0; o < 2; ++o) {
                k.phase[o] += k.inc[o];
                if (k.phase[o] >= 1.0) k.phase[o] -= 1.0;
            }
            float amp = env * k.velocity * keyGain;
            if (!mod) {
                l += k.filt[0].tick(model, (a + cross * b) * amp, k.g, fk, s_.filterMode);
                r += k.filt[1].tick(model, (cross * a + b) * amp, k.g, fk, s_.filterMode);
            } else {
                // What the matrix moves: resonance, mode and level per key, and the key's place between the sides.
                amp *= k.lv;
                const float kf = k.mod.targets(ModDest::Resonance) ? k.fk : fk;
                const float km = k.mod.targets(ModDest::FilterMode) ? k.mode : s_.filterMode;
                l += k.panL * k.filt[0].tick(model, (a + cross * b) * amp, k.g, kf, km);
                r += k.panR * k.filt[1].tick(model, (cross * a + b) * amp, k.g, kf, km);
            }
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
