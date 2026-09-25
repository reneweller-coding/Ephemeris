/**
 * @file Rooms.h
 * @brief The production guide's four rooms, the two that were missing (25.09.2026): the early reflections of a room
 *        without its tail (send A: distance and glue without a smear) and the effect hall (send D: a long plate with an
 *        octave-up pitch shifter in its feedback -- the shimmer of the modern-modular school, for drones, pads and the
 *        transitions). The blend room (B) and the hall (C) are the plate and the hall of Engine.h.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/fx/Plate.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace eph {

/**
 * @brief Early reflections alone: twelve taps a side between 3 ms and the size, their gains falling and their signs
 *        alternating, different on each side (so the pattern is wide), through the return's high and low pass.
 */
class EarlyReflections {
public:
    void prepare(double sampleRate)
    {
        sr_ = sampleRate;
        size_t len = 1;
        while (len < static_cast<size_t>(0.12 * sampleRate)) len <<= 1;
        buf_.assign(len, 0.0f);
        mask_ = len - 1;
        write_ = 0;
        for (Svf* f : { &hpL_, &hpR_, &lpL_, &lpR_ }) f->reset();
        set(35.0f, 200.0f, 8000.0f);
    }
    /** @brief The span of the reflections in ms (10 .. 80) and the return's band. */
    void set(float sizeMs, float lowCutHz, float highCutHz)
    {
        const float sr = static_cast<float>(sr_);
        double sum = 0.0;
        for (int k = 0; k < kTaps; ++k) {
            // Two golden-ratio walks through the span: the taps of the two sides never coincide.
            const double fl = std::fmod(0.11 + 0.6180339887 * k, 1.0), fr = std::fmod(0.37 + 0.7548776662 * k, 1.0);
            tapL_[k] = std::max(1, static_cast<int>((3.0 + (sizeMs - 3.0) * fl) * 0.001 * sr_));
            tapR_[k] = std::max(1, static_cast<int>((3.0 + (sizeMs - 3.0) * fr) * 0.001 * sr_));
            gain_[k] = static_cast<float>(std::pow(0.82, k)) * (k % 2 == 0 ? 1.0f : -1.0f);
            sum += static_cast<double>(gain_[k]) * gain_[k];
        }
        const float norm = static_cast<float>(1.0 / std::sqrt(sum));
        for (float& g : gain_) g *= norm;
        hpL_.setQ(lowCutHz, 0.7071f, sr); hpR_.copyCoefficients(hpL_);
        lpL_.setQ(highCutHz, 0.7071f, sr); lpR_.copyCoefficients(lpL_);
    }
    /** @brief Adds the reflections of the send (@p inL, @p inR) to @p outL, @p outR. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i) {
            buf_[write_] = 0.5f * (inL[i] + inR[i]);
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < kTaps; ++k) {
                l += gain_[k] * buf_[(write_ - static_cast<size_t>(tapL_[k])) & mask_];
                r += gain_[k] * buf_[(write_ - static_cast<size_t>(tapR_[k])) & mask_];
            }
            write_ = (write_ + 1) & mask_;
            float lo, bp, hl, hr;
            hpL_.tick(l, lo, bp, hl);
            hpR_.tick(r, lo, bp, hr);
            outL[i] += lpL_.lp(hl);
            outR[i] += lpR_.lp(hr);
        }
    }

private:
    static constexpr int kTaps = 12;
    double sr_ = 48000.0;
    std::vector<float> buf_;
    size_t mask_ = 0, write_ = 0;
    int tapL_[kTaps] = {}, tapR_[kTaps] = {};
    float gain_[kTaps] = {};
    Svf hpL_, hpR_, lpL_, lpR_;
};

/**
 * @brief The effect hall: a long plate whose output, shifted an octave up (two crossfaded taps running at twice the
 *        speed through a delay of its past, 43 ms windows), is fed back into it -- each round of the tail an octave
 *        brighter and fainter. An exact octave: it adds to the harmony, it never detunes it.
 */
class Shimmer {
public:
    void prepare(double sampleRate)
    {
        sr_ = sampleRate;
        plate_.prepare(sampleRate);
        shift_.assign(8192, 0.0f);
        write_ = 0;
        phase_ = 0.0;
        window_ = std::max(256.0, 0.043 * sampleRate);
        for (Svf* f : { &hpL_, &hpR_, &lpL_, &lpR_ }) f->reset();
        quiet_ = 0;
        set(8.0f, 0.35f, 400.0f, 4000.0f);
    }
    /** @brief The tail in seconds, the octave's share fed back (0 .. 0.6), the return's band. */
    void set(float decaySeconds, float amount, float lowCutHz, float highCutHz)
    {
        plate_.set(decaySeconds, 0.35f, 0.0f, 150.0f, 9000.0f);
        idleAfter_ = static_cast<long long>(3.0 * decaySeconds * sr_);
        amount_ = std::clamp(amount, 0.0f, 0.6f);
        const float sr = static_cast<float>(sr_);
        hpL_.setQ(lowCutHz, 0.7071f, sr); hpR_.copyCoefficients(hpL_);
        lpL_.setQ(highCutHz, 0.7071f, sr); lpR_.copyCoefficients(lpL_);
    }
    /** @brief Adds the effect of the send to @p outL, @p outR (n at most 64). */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        // Idle once nothing has come in for twice the tail: most of a piece sends nothing here.
        bool silent = true;
        for (int i = 0; i < n && silent; ++i) silent = inL[i] == 0.0f && inR[i] == 0.0f;
        quiet_ = silent ? quiet_ + n : 0;
        if (quiet_ > idleAfter_) return;
        float xl[64], xr[64], yl[64] = {}, yr[64] = {};
        const size_t mask = shift_.size() - 1;
        for (int i = 0; i < n; ++i) {
            // The octave up: the delay shrinks by a sample a sample; two taps half a window apart, sin^2 windows.
            // The shortest delay is 64 samples, so a tap never reads what this block has not written yet.
            phase_ -= 1.0 / window_;
            if (phase_ < 0.0) phase_ += 1.0;
            const double p2 = phase_ + 0.5 >= 1.0 ? phase_ - 0.5 : phase_ + 0.5;
            const size_t d1 = 64 + static_cast<size_t>(phase_ * window_), d2 = 64 + static_cast<size_t>(p2 * window_);
            const float w1 = sin01(0.5 * phase_), w2 = sin01(0.5 * p2);   // sin(pi p) from the table
            const float up = w1 * w1 * shift_[(write_ - d1) & mask] + w2 * w2 * shift_[(write_ - d2) & mask];
            xl[i] = inL[i] + amount_ * up;
            xr[i] = inR[i] + amount_ * up;
            write_ = (write_ + 1) & mask;
        }
        plate_.process(xl, xr, yl, yr, n);
        // What came out goes into the shifter's past, for the next rounds.
        size_t w = (write_ - static_cast<size_t>(n)) & mask;
        for (int i = 0; i < n; ++i) {
            shift_[w] = 0.5f * (yl[i] + yr[i]);
            w = (w + 1) & mask;
            float lo, bp, hl, hr;
            hpL_.tick(yl[i], lo, bp, hl);
            hpR_.tick(yr[i], lo, bp, hr);
            outL[i] += lpL_.lp(hl);
            outR[i] += lpR_.lp(hr);
        }
    }

private:
    double sr_ = 48000.0, phase_ = 0.0, window_ = 2048.0;
    Plate plate_;
    std::vector<float> shift_;
    size_t write_ = 0;
    float amount_ = 0.35f;
    long long quiet_ = 0, idleAfter_ = 1000000;
    Svf hpL_, hpR_, lpL_, lpR_;
};

/**
 * @brief A resonance suppressor in six bands (350 Hz .. 4.8 kHz, Q 2.5): each band's follower (3 ms up, 60 ms down)
 *        against its neighbours'; a band more than 4.5 dB over them -- a local peak, as a resonance is, whatever the
 *        spectrum's tilt -- is pulled back, up to 3:1 at depth 1. Where nothing sticks out it passes the sound as is.
 */
class ResonanceTamer {
public:
    static constexpr int kBands = 6;
    void prepare(double sampleRate)
    {
        static const float kCentre[kBands] = { 350.0f, 600.0f, 1000.0f, 1700.0f, 2900.0f, 4800.0f };
        const float sr = static_cast<float>(sampleRate);
        for (int k = 0; k < kBands; ++k) {
            l_[k].setQ(kCentre[k], 2.5f, sr); l_[k].reset();
            r_[k].setQ(kCentre[k], 2.5f, sr); r_[k].reset();
            env_[k] = 0.0f;
        }
        attack_ = std::exp(-1.0f / (0.003f * sr));
        release_ = std::exp(-1.0f / (0.06f * sr));
    }
    /** @brief Works on @p L, @p R in place; @p depth 0 leaves them alone. */
    void process(float* L, float* R, int n, float depth)
    {
        if (depth <= 0.0f) return;
        for (int i = 0; i < n; ++i) {
            float bl[kBands], br[kBands];
            for (int k = 0; k < kBands; ++k) {
                float lo, hp;
                l_[k].tick(L[i], lo, bl[k], hp);
                r_[k].tick(R[i], lo, br[k], hp);
                bl[k] *= l_[k].k;
                br[k] *= r_[k].k;
                const float x = std::max(std::fabs(bl[k]), std::fabs(br[k]));
                env_[k] = x + (x > env_[k] ? attack_ : release_) * (env_[k] - x);
            }
            for (int k = 0; k < kBands; ++k) {
                const float around = k == 0 ? env_[1] : (k == kBands - 1 ? env_[kBands - 2] : 0.5f * (env_[k - 1] + env_[k + 1]));
                const float limit = 1.68f * around;
                if (env_[k] <= limit || env_[k] < 1e-6f) continue;
                const float g = std::pow(limit / env_[k], 0.667f * depth);
                L[i] += (g - 1.0f) * bl[k];
                R[i] += (g - 1.0f) * br[k];
            }
        }
    }

private:
    Svf l_[kBands], r_[kBands];
    float env_[kBands] = {}, attack_ = 0.0f, release_ = 0.0f;
};

} // namespace eph
