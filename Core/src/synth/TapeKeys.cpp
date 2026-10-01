/**
 * @file TapeKeys.cpp
 * @brief The tape keyboard.
 */
#include "eph/synth/TapeKeys.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace eph {

namespace {

/// Formants of a mixed choir: "aah" and "ooh" (after Klatt 1980; male and female values averaged and
/// rounded). Frequency in Hz, bandwidth in Hz, level in dB.
constexpr float kAahF[5] = { 730.0f, 1090.0f, 2440.0f, 3400.0f, 4200.0f };
constexpr float kAahB[5] = { 80.0f, 90.0f, 120.0f, 180.0f, 250.0f };   ///< "aah": bandwidths, Hz
constexpr float kAahG[5] = { 0.0f, -5.0f, -15.0f, -22.0f, -28.0f };   ///< "aah": levels, dB
constexpr float kOohF[5] = { 330.0f, 870.0f, 2240.0f, 3200.0f, 4100.0f };   ///< "ooh": frequencies, Hz
constexpr float kOohB[5] = { 60.0f, 80.0f, 110.0f, 170.0f, 240.0f };   ///< "ooh": bandwidths, Hz
constexpr float kOohG[5] = { 0.0f, -10.0f, -26.0f, -32.0f, -36.0f };   ///< "ooh": levels, dB

constexpr int kStringSaws = 5;   ///< saws of a strings tape

/** @brief The pressure pad's rise time constant for tape set @p set, s. */
double riseTau(TapeSet set) { return set == TapeSet::Strings ? 0.08 : (set == TapeSet::Flute ? 0.05 : 0.06); }

} // namespace

void TapeKeys::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    seed_ = seed;
    rng_.seed(mixSeed(seed, 17));
    mod_.prepare(sr_, mixSeed(seed, 0x5441ull));
    mod_.set(s_.mod);
    reset();
    set(s_);
}

void TapeKeys::reset()
{
    for (Key& k : keys_) { k.on = k.held = false; k.id = -1; }
    wowPhase_ = flutterPhase_ = drift_ = driftTarget_ = 0.0;
    count_ = 0;
    thump_ = 0.0f;
    mod_.kill();
}

void TapeKeys::set(const TapeSettings& s)
{
    const bool vowelMoved = s.vowel != s_.vowel || s.set != s_.set;
    if (std::memcmp(&s.mod, &s_.mod, sizeof(ModSettings)) != 0) mod_.set(s.mod);
    s_ = s;
    // The fall after the key (a time constant) and the player's decay (to 5 % of the way in its time).
    fallCoef_ = std::exp(-1.0 / (static_cast<double>(s.releaseMs) / 1000.0 * sr_));
    decayCoef_ = std::exp(-3.0 / (std::max(0.005, static_cast<double>(s.decayMs) / 1000.0) * sr_));
    for (int f = 0; f < 5; ++f) formantGain_[f] = dbToGain(kAahG[f] + s_.vowel * (kOohG[f] - kAahG[f]));
    const float sr = static_cast<float>(sr_);
    for (Key& k : keys_) {
        if (!k.on) continue;
        k.tone.set(std::min(0.45f * sr, s_.toneHz * static_cast<float>(std::exp2(0.4 * k.cents / 6.0 + mod_.offset(mo_, ModDest::Cutoff)))), 0.0f, sr);
        if (vowelMoved && k.set == TapeSet::Choir) {
            for (int f = 0; f < 5; ++f) {
                const float F = kAahF[f] + s_.vowel * (kOohF[f] - kAahF[f]);
                const float B = kAahB[f] + s_.vowel * (kOohB[f] - kAahB[f]);
                k.formant[f].setQ(F, F / B, sr);
            }
        }
    }
}

double TapeKeys::speedFor(int keysDown) const
{
    return std::exp2(-static_cast<double>(s_.sagCents) * std::max(0, keysDown - 1) / 1200.0);
}

void TapeKeys::startKey(Key& k, int pitch, float velocity, int id)
{
    k.on = k.held = true;
    k.pitch = pitch;
    k.id = id;
    k.velocity = velocity;
    k.age = 0.0;
    k.released = -1.0;
    k.order = ++counter_;
    k.set = s_.set;
    k.rise = 0.0;
    k.riseCoef = 1.0 - std::exp(-1.0 / (riseTau(k.set) * sr_));
    k.fall = 1.0;
    k.decay = 1.0;
    // This key's tape: the same every time the key is pressed, as an instrument has one set of tapes.
    Rng tape;
    tape.seed(mixSeed(seed_, 1000 + static_cast<uint64_t>(pitch)));
    const double a = static_cast<double>(s_.age);
    k.cents = 6.0 * a * static_cast<double>(tape.bipolar());
    k.gain = dbToGain(1.5f * s_.age * tape.bipolar());
    k.lag = 0.005 + 0.02 * a * static_cast<double>(tape.uniform());
    const float sr = static_cast<float>(sr_);
    k.tone.reset();
    k.tone.set(std::min(0.45f * sr, s_.toneHz * static_cast<float>(std::exp2(0.4 * k.cents / 6.0))), 0.0f, sr);
    for (int i = 0; i < kSingers; ++i) {
        Singer& g = k.singers[i];
        g.detune = (k.set == TapeSet::Strings ? 12.0 : 9.0) * static_cast<double>(tape.bipolar());
        g.vibHz = 4.8 + 1.4 * static_cast<double>(tape.uniform());
        g.vibDepth = 15.0 + 20.0 * static_cast<double>(tape.uniform());
        g.vibPhase = static_cast<double>(tape.uniform());
        g.wander = g.wanderTarget = 0.0;
        g.level = 0.8 + 0.4 * static_cast<double>(tape.uniform());
        g.osc.restart(static_cast<double>(tape.uniform()), 0.0);
        g.tilt.reset();
        g.tilt.setQ(1100.0f, 0.707f, sr);
    }
    for (int f = 0; f < 5; ++f) {
        const float F = kAahF[f] + s_.vowel * (kOohF[f] - kAahF[f]);
        const float B = kAahB[f] + s_.vowel * (kOohB[f] - kAahB[f]);
        k.formant[f].reset();
        k.formant[f].setQ(F, F / B, sr);
    }
    k.phase = 0.0;
    k.breath.reset();
    k.breath.setQ(std::min(0.45f * sr, static_cast<float>(2.0 * midiToHz(pitch))), 4.0f, sr);
    thump_ += 0.12f * s_.age * velocity;
}

void TapeKeys::noteOn(int pitch, float velocity, int id)
{
    Key* slot = nullptr;
    for (Key& k : keys_) if (!k.on) { slot = &k; break; }
    if (slot == nullptr) {
        slot = &keys_[0];
        for (Key& k : keys_) if (k.order < slot->order) slot = &k;
    }
    startKey(*slot, pitch, velocity, id);
    mod_.noteOn(count_, beatAt(count_));
}

void TapeKeys::noteOff(int id)
{
    for (Key& k : keys_)
        if (k.on && k.held && k.id == id) { k.held = false; k.released = 0.0; }
    bool held = false;
    for (const Key& k : keys_) held = held || k.held;
    if (!held) mod_.noteOff();
}

bool TapeKeys::active() const
{
    for (const Key& k : keys_) if (k.on) return true;
    return thump_ > 1e-5f;
}

float TapeKeys::render(Key& k, double speedCents)
{
    const double dt = 1.0 / sr_;
    k.age += dt;
    if (k.released >= 0.0) { k.released += dt; k.fall *= fallCoef_; }
    // The envelope of the machine: the lag, the pressure pad's rise, the tape end, the release.
    if (k.age > k.lag) k.rise += (1.0 - k.rise) * k.riseCoef;
    double env = k.rise * k.fall;
    // The player's envelope over the machine's: the swell, then the decay towards the sustain level.
    const double swellS = static_cast<double>(s_.swellMs) / 1000.0;
    if (swellS > 0.001 && k.age < swellS) env *= k.age / swellS;
    if (s_.sustain < 1.0f && k.age >= std::max(swellS, 0.0)) {
        k.decay *= decayCoef_;
        env *= static_cast<double>(s_.sustain) + (1.0 - static_cast<double>(s_.sustain)) * k.decay;
    }
    const double fadeStart = kTapeSeconds - 0.33;
    if (k.age > fadeStart) env *= std::max(0.0, 1.0 - (k.age - fadeStart) / 0.33);
    const double gone = s_.releaseMs > 70.0f ? 10.0 * static_cast<double>(s_.releaseMs) / 1000.0 : 0.7;   // a longer fall rings longer
    if (k.age >= kTapeSeconds || (k.released >= 0.0 && k.released > gone)) { k.on = false; return 0.0f; }

    const double cents = k.cents + speedCents;
    const float sr = static_cast<float>(sr_);
    float y = 0.0f;
    switch (k.set) {
    case TapeSet::Choir: {
        if ((count_ & 7) == 0) {
            for (int i = 0; i < singers_; ++i) {
                Singer& g = k.singers[i];
                g.vibPhase += 8.0 * g.vibHz / sr_;
                if (g.vibPhase >= 1.0) g.vibPhase -= 1.0;
                if ((count_ & 2047) == 0) g.wanderTarget = 6.0 * static_cast<double>(rng_.bipolar());
                g.wander += (g.wanderTarget - g.wander) * 0.004;
                const double c = cents + g.detune + g.wander + g.vibDepth * static_cast<double>(sin01(g.vibPhase));
                g.osc.set(midiToHz(k.pitch + c * 0.01), sr_, 0.0f, 0.5f);
            }
        }
        float sum = 0.0f;
        for (int i = 0; i < singers_; ++i) sum += static_cast<float>(k.singers[i].level) * k.singers[i].osc.next();
        float lo, bp, hi;
        k.singers[0].tilt.tick(sum * (1.0f / std::sqrt(static_cast<float>(kSingers * singers_))), lo, bp, hi);
        const float src = lo;
        for (int f = 0; f < 5; ++f) {
            float fl, fb, fh;
            k.formant[f].tick(src, fl, fb, fh);
            y += formantGain_[f] * fb * k.formant[f].k;
        }
        y *= 2.5f;
        break;
    }
    case TapeSet::Strings: {
        if ((count_ & 7) == 0)
            for (int i = 0; i < kStringSaws; ++i)
                k.singers[i].osc.set(midiToHz(k.pitch + (cents + k.singers[i].detune) * 0.01), sr_, 0.0f, 0.5f);
        float sum = 0.0f;
        for (int i = 0; i < kStringSaws; ++i) sum += k.singers[i].osc.next();
        float lo, bp, hi;
        k.singers[0].tilt.tick(sum * (1.0f / kStringSaws), lo, bp, hi);   // 1.1 kHz: the bowed body's roll-off
        y = 0.6f * lo + 0.25f * bp;
        break;
    }
    case TapeSet::Flute: {
        const double f = midiToHz(k.pitch + cents * 0.01);
        k.phase += f / sr_;
        if (k.phase >= 1.0) k.phase -= 1.0;
        const float s1 = sin01(k.phase), s2 = sin01(2.0 * k.phase), s3 = sin01(3.0 * k.phase);
        float lo, bp, hi;
        k.breath.tick(rng_.bipolar(), lo, bp, hi);
        const float chiff = k.age < 0.04 ? static_cast<float>(1.0 - k.age / 0.04) : 0.0f;
        y = 0.8f * s1 + 0.15f * s2 + 0.05f * s3 + (0.08f + 0.5f * chiff) * bp * k.breath.k;
        (void)sr;
        break;
    }
    default: break;
    }
    return k.tone.lp(y) * static_cast<float>(env) * k.gain * k.velocity;
}

void TapeKeys::process(float* out, int n)
{
    for (int i = 0; i < n; ++i) {
        if ((count_ & 31) == 0) {
            if ((count_ % 32768) == 0) driftTarget_ = static_cast<double>(rng_.bipolar());
            drift_ += (driftTarget_ - drift_) * 0.002;
            if (mod_.active()) {
                // The keyboard's matrix (Modulation.h); a moving tone retunes every key's head filter.
                const float ext[kModSources] = {};
                mod_.evaluate(count_, beatAt(count_), ext, mo_);
                if (mod_.targets(ModDest::Cutoff)) {
                    const float sr = static_cast<float>(sr_);
                    for (Key& k : keys_)
                        if (k.on) k.tone.set(std::min(0.45f * sr, s_.toneHz * static_cast<float>(std::exp2(0.4 * k.cents / 6.0 + mo_[static_cast<int>(ModDest::Cutoff)]))), 0.0f, sr);
                }
            }
        }
        wowPhase_ += 0.6 / sr_;
        if (wowPhase_ >= 1.0) wowPhase_ -= 1.0;
        flutterPhase_ += 7.5 / sr_;
        if (flutterPhase_ >= 1.0) flutterPhase_ -= 1.0;
        int down = 0;
        bool any = false;
        for (const Key& k : keys_) { down += k.held ? 1 : 0; any = any || k.on; }
        double speed = static_cast<double>(s_.wowCents) * (0.7 * static_cast<double>(sin01(wowPhase_)) + 0.3 * drift_)
                     + static_cast<double>(s_.flutterCents) * static_cast<double>(sin01(flutterPhase_))
                     - static_cast<double>(s_.sagCents) * std::max(0, down - 1);
        const bool mod = mod_.active();
        if (mod && mod_.targets(ModDest::Pitch)) speed += 100.0 * static_cast<double>(mo_[static_cast<int>(ModDest::Pitch)]);
        float y = 0.0f;
        for (Key& k : keys_) if (k.on) y += render(k, speed);
        y *= 0.3f;
        if (mod && mod_.targets(ModDest::Level)) y *= std::clamp(1.0f + mo_[static_cast<int>(ModDest::Level)], 0.0f, 2.0f);
        // The pressure pad's thump and the tape's hiss.
        if (thump_ > 1e-5f) { y += thump_ * rng_.bipolar(); thump_ *= 0.9974f; }
        if (any) y += (0.0003f + 0.001f * s_.age) * rng_.bipolar();
        out[i] = std::tanh(1.5f * y) / 1.5f;
        ++count_;
    }
}

} // namespace eph
