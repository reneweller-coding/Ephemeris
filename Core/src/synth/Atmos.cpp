/**
 * @file Atmos.cpp
 * @brief Wind, sweeps, bleeps.
 */
#include "eph/synth/Atmos.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph {

void Atmos::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    rng_.seed(seed);
    windL_.reset(); windR_.reset(); sweep_.reset();
    wanderL_.x = wanderR_.x = 0.0;
    pinkL_ = pinkR_ = 0.0f;
    sweepAge_ = -1.0;
    bleepsLeft_ = 0;
    count_ = 0;
    windNow_ = 0.0f;
}

void Atmos::set(const AtmosSettings& s) { s_ = s; }

bool Atmos::active() const
{
    return s_.windGain > 1e-5f || windNow_ > 1e-5f || sweepAge_ >= 0.0 || bleepsLeft_ > 0
        || s_.sweepsPerMinute > 0.0f || s_.bleepsPerMinute > 0.0f;
}

void Atmos::process(float* L, float* R, float* bleepL, float* bleepR, int n)
{
    const float sr = static_cast<float>(sr_);
    for (int i = 0; i < n; ++i) {
        if ((count_ & 31) == 0) {
            const double dt = 32.0 / sr_;
            // The wind's centres wander half an octave on a quarter-minute time constant, apart.
            const double cl = s_.windHz * std::exp2(0.5 * wanderL_.step(dt, 15.0, 1.0, rng_));
            const double cr = s_.windHz * std::exp2(0.5 * wanderR_.step(dt, 15.0, 1.0, rng_));
            windL_.setQ(static_cast<float>(std::min(cl, 0.4 * sr_)), 2.5f, sr);
            windR_.setQ(static_cast<float>(std::min(cr, 0.4 * sr_)), 2.5f, sr);
            // Poisson starts: the chance of an event in this cell.
            if (sweepAge_ < 0.0 && s_.sweepsPerMinute > 0.0f && rng_.uniform() < s_.sweepsPerMinute * dt / 60.0) {
                sweepAge_ = 0.0;
                sweepLen_ = 4.0 + 8.0 * static_cast<double>(rng_.uniform());
                const bool up = rng_.uniform() < 0.5f;
                sweepFrom_ = up ? 200.0 : 5000.0;
                sweepTo_ = up ? 5000.0 : 200.0;
                sweepPan_ = rng_.bipolar() * 0.7f;
            }
            if (bleepsLeft_ == 0 && s_.bleepsPerMinute > 0.0f && rng_.uniform() < s_.bleepsPerMinute * dt / 60.0) {
                bleepsLeft_ = 2 + rng_.below(4);
                bleepAge_ = 0.0;
                bleepEvery_ = 0.08 + 0.12 * static_cast<double>(rng_.uniform());
                bleepPan_ = rng_.bipolar() * 0.8f;
            }
            if (sweepAge_ >= 0.0) {
                const double t = std::min(1.0, sweepAge_ / sweepLen_);
                sweep_.setQ(static_cast<float>(sweepFrom_ * std::pow(sweepTo_ / sweepFrom_, t)), 8.0f, sr);
            }
        }
        ++count_;
        windNow_ += (s_.windGain - windNow_) * 0.0005f;
        // Two pink-ish noises (a one-pole tilt on white), one per side.
        pinkL_ += 0.08f * (rng_.bipolar() - pinkL_);
        pinkR_ += 0.08f * (rng_.bipolar() - pinkR_);
        float lo, bp, hi;
        windL_.tick(pinkL_, lo, bp, hi);
        const float wl = bp * windL_.k;
        windR_.tick(pinkR_, lo, bp, hi);
        const float wr = bp * windR_.k;
        float l = 2.0f * windNow_ * wl, r = 2.0f * windNow_ * wr;
        if (sweepAge_ >= 0.0) {
            const double t = sweepAge_ / sweepLen_;
            const float env = static_cast<float>(std::sin(kPi * std::min(1.0, t)));   // in and out over its length
            sweep_.tick(0.5f * (pinkL_ + pinkR_), lo, bp, hi);
            const float v = 1.5f * s_.sweepGain * env * bp;
            l += v * (1.0f - sweepPan_) * 0.7f;
            r += v * (1.0f + sweepPan_) * 0.7f;
            sweepAge_ += 1.0 / sr_;
            if (sweepAge_ >= sweepLen_) sweepAge_ = -1.0;
        }
        if (bleepsLeft_ > 0) {
            if (bleepAge_ <= 0.0) {
                // Sample and hold: a random note of the scale over the root, two or three octaves up.
                const int d = rng_.below(scaleSize(s_.scale) * 2);
                bleepHz_ = midiToHz(72 + s_.rootPc % 12 + scaleSemitones(s_.scale, d));
                bleepPhase_ = 0.0;
            }
            bleepPhase_ += bleepHz_ / sr_;
            if (bleepPhase_ >= 1.0) bleepPhase_ -= 1.0;
            const double env = std::exp(-bleepAge_ / (0.25 * bleepEvery_));
            const float v = s_.bleepGain * static_cast<float>(env) * sin01(bleepPhase_);
            const float bl = v * (1.0f - bleepPan_) * 0.7f, br = v * (1.0f + bleepPan_) * 0.7f;
            l += bl; r += br;
            bleepL[i] += bl; bleepR[i] += br;
            bleepAge_ += 1.0 / sr_;
            if (bleepAge_ >= bleepEvery_) { bleepAge_ = 0.0; --bleepsLeft_; }
        }
        L[i] += l;
        R[i] += r;
    }
}

} // namespace eph
