/**
 * @file Plate.cpp
 * @brief Dattorro's plate (Plate.h).
 */
#include "eph/fx/Plate.h"
#include "eph/Dsp.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr double kRef = 29761.0;   ///< the sample rate Dattorro's lengths are given at
}

void Plate::Line::init(int len)
{
    length = std::max(1, len);
    size_t n = 4;
    while (n < static_cast<size_t>(length) + 64) n <<= 1;
    buf.assign(n, 0.0f);
    mask = n - 1;
    write = 0;
}

float Plate::allpass(Line& l, float x, float g, float delayed)
{
    const float w = x + g * delayed;
    l.push(w);
    return delayed - g * w;
}

void Plate::prepare(double sampleRate)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double k = sr_ / kRef;
    auto len = [k](int n) { return static_cast<int>(std::lround(n * k)); };
    pre_.init(static_cast<int>(0.2 * sr_));
    const int inLen[4] = { 142, 107, 379, 277 };
    for (int i = 0; i < 4; ++i) in_[i].init(len(inLen[i]));
    excursion_ = static_cast<float>(16.0 * k);
    in1_ = len(672);
    in2_ = len(908);
    apL_.init(in1_ + static_cast<int>(excursion_) + 2);
    apR_.init(in2_ + static_cast<int>(excursion_) + 2);
    dL1_.init(len(4453));
    ap2L_.init(len(1800));
    dL2_.init(len(3720));
    dR1_.init(len(4217));
    ap2R_.init(len(2656));
    dR2_.init(len(3163));
    // Output taps (Dattorro, table 2): left 266, 2974 in the left first delay; 1913 in the left second all-pass;
    // 1996 in the left second delay; 1990 in the right first delay; 187 in the right second all-pass; 1066 in the
    // right second delay -- and the mirror for the right.
    const int l[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
    const int r[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };
    for (int i = 0; i < 7; ++i) { tapL_[i] = len(l[i]); tapR_[i] = len(r[i]); }
    reset();
    set(3.0f, 0.3f, 0.0f, 80.0f, 12000.0f);
}

void Plate::reset()
{
    for (Line* line : { &pre_, &in_[0], &in_[1], &in_[2], &in_[3], &apL_, &apR_, &dL1_, &dL2_, &dR1_, &dR2_, &ap2L_, &ap2R_ })
        std::fill(line->buf.begin(), line->buf.end(), 0.0f);
    bwState_ = dampL_ = dampR_ = fbL_ = fbR_ = 0.0f;
    hpL_ = hpR_ = hpXL_ = hpXR_ = 0.0f;
    lfo_ = 0.0;
}

void Plate::set(float decaySeconds, float damping, float preDelaySamples, float lowCutHz, float highCutHz)
{
    decay_ = std::clamp(static_cast<float>(std::pow(0.001, 0.179 / std::max(0.2, static_cast<double>(decaySeconds)))), 0.0f, 0.97f);
    damp_ = 0.02f + 0.6f * std::clamp(damping, 0.0f, 1.0f);
    pre_d_ = std::clamp(preDelaySamples, 0.0f, static_cast<float>(pre_.length - 1));
    bw_ = static_cast<float>(1.0 - std::exp(-kTwoPi * std::clamp(static_cast<double>(highCutHz), 500.0, 0.45 * sr_) / sr_));
    hpCoef_ = static_cast<float>(std::exp(-kTwoPi * std::max(20.0, static_cast<double>(lowCutHz)) / sr_));
}

void Plate::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float lfoInc = static_cast<float>(1.0 / sr_);   // 1 Hz
    for (int i = 0; i < n; ++i) {
        // Pre-delay and band limit, then the four input diffusers.
        pre_.push(0.5f * (inL[i] + inR[i]));
        const float pd = pre_.at(static_cast<int>(pre_d_));
        bwState_ += bw_ * (pd - bwState_);
        float x = bwState_;
        x = allpass(in_[0], x, 0.75f, in_[0].out());
        x = allpass(in_[1], x, 0.75f, in_[1].out());
        x = allpass(in_[2], x, 0.625f, in_[2].out());
        x = allpass(in_[3], x, 0.625f, in_[3].out());

        // The tank's modulated all-passes read a moving tap (linear interpolation), the two in quadrature.
        lfo_ += lfoInc;
        if (lfo_ >= 1.0) lfo_ -= 1.0;
        auto moving = [](const Line& l, float d) {
            const int di = static_cast<int>(d);
            const float f = d - static_cast<float>(di);
            return l.at(di) + f * (l.at(di + 1) - l.at(di));
        };
        const float mL = static_cast<float>(in1_) + excursion_ * sin01(lfo_);
        const float mR = static_cast<float>(in2_) + excursion_ * sin01(lfo_ + 0.25);

        // Left half: fed by the input and the right half's end.
        float a = allpass(apL_, x + fbR_, -0.7f, moving(apL_, mL));
        dL1_.push(a);
        float b = dL1_.out();
        dampL_ += (1.0f - damp_) * (b - dampL_);
        b = dampL_ * decay_;
        b = allpass(ap2L_, b, 0.5f, ap2L_.out());
        dL2_.push(b);
        const float endL = dL2_.out() * decay_;

        // Right half: fed by the input and the left half's end.
        a = allpass(apR_, x + fbL_, -0.7f, moving(apR_, mR));
        dR1_.push(a);
        b = dR1_.out();
        dampR_ += (1.0f - damp_) * (b - dampR_);
        b = dampR_ * decay_;
        b = allpass(ap2R_, b, 0.5f, ap2R_.out());
        dR2_.push(b);
        const float endR = dR2_.out() * decay_;
        fbL_ = endL;
        fbR_ = endR;

        // The fourteen taps.
        float yl = dR1_.at(tapL_[0]) + dR1_.at(tapL_[1]) - ap2R_.at(tapL_[2]) + dR2_.at(tapL_[3])
                 - dL1_.at(tapL_[4]) - ap2L_.at(tapL_[5]) - dL2_.at(tapL_[6]);
        float yr = dL1_.at(tapR_[0]) + dL1_.at(tapR_[1]) - ap2L_.at(tapR_[2]) + dL2_.at(tapR_[3])
                 - dR1_.at(tapR_[4]) - ap2R_.at(tapR_[5]) - dR2_.at(tapR_[6]);
        // Dattorro's 0.6, lowered by 6.9 dB to the hall's return: measured in the rooms stem of the same piece
        // (Cosmic, seed 5), -24.0 against -30.9 dBFS. The room knob then changes the room, not the level.
        yl *= 0.27f;
        yr *= 0.27f;
        // The return's high pass (one pole): the low cut of the hall's knob.
        hpL_ = hpCoef_ * (hpL_ + yl - hpXL_); hpXL_ = yl;
        hpR_ = hpCoef_ * (hpR_ + yr - hpXR_); hpXR_ = yr;
        outL[i] += hpL_;
        outR[i] += hpR_;
    }
}

} // namespace eph
