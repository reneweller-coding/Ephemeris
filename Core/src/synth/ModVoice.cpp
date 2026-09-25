/**
 * @file ModVoice.cpp
 * @brief The modular voice.
 */
#include "eph/synth/ModVoice.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
const HalfbandDesign& halfband()
{
    static const HalfbandDesign d = designHalfband(96.0, 0.1);
    return d;
}
constexpr double kDriftTau1 = 14.0, kDriftTau2 = 19.0;   ///< seconds; two, so the VCOs wander apart
}

void ModVoice::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    sr2_ = 2.0 * sr_;
    rng_.seed(seed);
    filt_.setSampleRate(sr_);
    amp_.setSampleRate(sr_);
    down_.setup(halfband());
    dc_.prepare(sr_, 8.0f);
    drift1_.x = drift2_.x = 0.0;
    reset();
    set(s_);
}

void ModVoice::reset()
{
    ladder_.reset();
    down_.reset();
    sat_.reset();
    dc_.reset();
    filt_.kill();
    amp_.kill();
    held_ = -1;
    sampleCount_ = 0;
}

void ModVoice::set(const VoiceSettings& s)
{
    s_ = s;
    filt_.setTimes(0.0015f, s.decayMs * 0.001f, 0.0f, s.decayMs * 0.001f);
    amp_.setTimes(0.002f, 0.05f, 1.0f, s.releaseMs * 0.001f);
    glideCoef_ = 1.0 - std::exp(-1.0 / (std::max(1.0, static_cast<double>(s.glideMs)) * 0.001 * sr_ / 3.0));
    vibCoef_ = 1.0 - std::exp(-1.0 / (0.4 * sr_ / 3.0));
    driveGain_ = dbToGain(s.driveDb);
    // Level the drive: loud enough to bend, without the voice getting louder by the same amount.
    driveNorm_ = 1.0f / std::sqrt(driveGain_);
}

void ModVoice::noteOn(int pitch, float velocity, bool accent, bool legato, int id)
{
    target_ = pitch;
    if (!(legato && held_ >= 0)) {
        pitch_ = target_;
        filt_.noteOn();
        amp_.noteOn();
        velocity_ = velocity;
        accentAmt_ = accent ? s_.accent : 0.0f;
        noteCents_ = 0.15 * static_cast<double>(s_.driftCents) * static_cast<double>(rng_.bipolar());
        vibLevel_ = 0.0;
    }
    held_ = id;
}

void ModVoice::noteOff(int id)
{
    if (id != held_) return;
    held_ = -1;
    amp_.noteOff();
}

void ModVoice::process(float* out, int n)
{
    const double inv2 = 1.0 / sr2_;
    const float k = 4.0f * std::clamp(s_.resonance, 0.0f, 1.0f) * 0.985f;
    const float nyq = static_cast<float>(0.42 * sr2_);
    for (int i = 0; i < n; ++i) {
        if ((sampleCount_ & 31) == 0) {
            const double dt = 32.0 / sr_;
            drift1_.step(dt, kDriftTau1, s_.driftCents, rng_);
            drift2_.step(dt, kDriftTau2, s_.driftCents, rng_);
        }
        ++sampleCount_;
        // Pitch, envelopes and cutoff once per output sample; oscillators, mixer and ladder at 2x.
        pitch_ += (target_ - pitch_) * glideCoef_;
        double vib = 0.0;
        if (s_.vibratoCents > 0.0f) {
            vibLevel_ += ((held_ >= 0 ? 1.0 : 0.0) - vibLevel_) * vibCoef_;
            vibPhase_ += static_cast<double>(s_.vibratoHz) / sr_;
            if (vibPhase_ >= 1.0) vibPhase_ -= 1.0;
            vib = static_cast<double>(s_.vibratoCents) * vibLevel_ * static_cast<double>(sin01(vibPhase_));
        }
        const double f1 = midiToHz(pitch_ + (drift1_.x + noteCents_ + vib) * 0.01);
        const double f2 = midiToHz(pitch_ + (drift2_.x + noteCents_ + vib + s_.detuneCents) * 0.01);
        osc1_.set(f1, sr2_, s_.wave, s_.pulseWidth);
        osc2_.set(f2, sr2_, s_.wave, s_.pulseWidth);
        const float fe = filt_.process();
        const float gain = amp_.process() * velocity_ * (1.0f + 0.4f * accentAmt_);
        const float octs = s_.envOctaves * fe * (1.0f + accentAmt_) + s_.keyTrack * static_cast<float>((pitch_ - 60.0) / 12.0);
        const float fc = std::min(nyq, s_.cutoffHz * std::exp2(octs));
        const float g = std::tan(kPi * fc * static_cast<float>(inv2));
        float two[2];
        for (int h = 0; h < 2; ++h) {
            const float mix = 0.5f * (osc1_.next() + osc2_.next());
            const float x = sat_(mix * driveGain_) * driveNorm_;
            two[h] = ladder_.tick(x, g, k, 0.5f) * gain;
        }
        out[i] = dc_.process(down_.process(two[0], two[1]));
    }
}

} // namespace eph
