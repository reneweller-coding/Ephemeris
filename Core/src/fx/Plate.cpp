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
constexpr double Ref = 29761.0;   ///< the sample rate Dattorro's lengths are given at
}

void Plate::Line::init(int len)
{
    m_length = std::max(1, len);
    size_t n = 4;
    while (n < static_cast<size_t>(m_length) + 64) n <<= 1;
    m_buf.assign(n, 0.0f);
    m_mask = n - 1;
    m_write = 0;
}

float Plate::allpass(Line& l, float x, float g, float delayed)
{
    const float w = x + g * delayed;
    l.push(w);
    return delayed - g * w;
}

void Plate::prepare(double sampleRate)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double k = m_sr / Ref;
    auto len = [k](int n) { return static_cast<int>(std::lround(n * k)); };
    m_pre.init(static_cast<int>(0.2 * m_sr));
    const int inLen[4] = { 142, 107, 379, 277 };
    for (int i = 0; i < 4; ++i) m_in[i].init(len(inLen[i]));
    m_excursion = static_cast<float>(16.0 * k);
    m_in1 = len(672);
    m_in2 = len(908);
    m_apL.init(m_in1 + static_cast<int>(m_excursion) + 2);
    m_apR.init(m_in2 + static_cast<int>(m_excursion) + 2);
    m_dL1.init(len(4453));
    m_ap2L.init(len(1800));
    m_dL2.init(len(3720));
    m_dR1.init(len(4217));
    m_ap2R.init(len(2656));
    m_dR2.init(len(3163));
    // Output taps (Dattorro, table 2): left 266, 2974 in the left first delay; 1913 in the left second all-pass;
    // 1996 in the left second delay; 1990 in the right first delay; 187 in the right second all-pass; 1066 in the
    // right second delay -- and the mirror for the right.
    const int l[7] = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
    const int r[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };
    for (int i = 0; i < 7; ++i) { m_tapL[i] = len(l[i]); m_tapR[i] = len(r[i]); }
    reset();
    set(3.0f, 0.3f, 0.0f, 80.0f, 12000.0f);
}

void Plate::reset()
{
    for (Line* line : { &m_pre, &m_in[0], &m_in[1], &m_in[2], &m_in[3], &m_apL, &m_apR, &m_dL1, &m_dL2, &m_dR1, &m_dR2, &m_ap2L, &m_ap2R })
        std::fill(line->m_buf.begin(), line->m_buf.end(), 0.0f);
    m_bwState = m_dampL = m_dampR = m_fbL = m_fbR = 0.0f;
    m_hpL = m_hpR = m_hpXL = m_hpXR = 0.0f;
    m_lfo = 0.0;
}

void Plate::set(float decaySeconds, float damping, float preDelaySamples, float lowCutHz, float highCutHz)
{
    m_decay = std::clamp(static_cast<float>(std::pow(0.001, 0.179 / std::max(0.2, static_cast<double>(decaySeconds)))), 0.0f, 0.97f);
    m_damp = 0.02f + 0.6f * std::clamp(damping, 0.0f, 1.0f);
    m_pre_d = std::clamp(preDelaySamples, 0.0f, static_cast<float>(m_pre.m_length - 1));
    m_bw = static_cast<float>(1.0 - std::exp(-TwoPi * std::clamp(static_cast<double>(highCutHz), 500.0, 0.45 * m_sr) / m_sr));
    m_hpCoef = static_cast<float>(std::exp(-TwoPi * std::max(20.0, static_cast<double>(lowCutHz)) / m_sr));
}

void Plate::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float lfoInc = static_cast<float>(1.0 / m_sr);   // 1 Hz
    for (int i = 0; i < n; ++i) {
        // Pre-delay and band limit, then the four input diffusers.
        m_pre.push(0.5f * (inL[i] + inR[i]));
        const float pd = m_pre.at(static_cast<int>(m_pre_d));
        m_bwState += m_bw * (pd - m_bwState);
        float x = m_bwState;
        x = allpass(m_in[0], x, 0.75f, m_in[0].out());
        x = allpass(m_in[1], x, 0.75f, m_in[1].out());
        x = allpass(m_in[2], x, 0.625f, m_in[2].out());
        x = allpass(m_in[3], x, 0.625f, m_in[3].out());

        // The tank's modulated all-passes read a moving tap (linear interpolation), the two in quadrature.
        m_lfo += lfoInc;
        if (m_lfo >= 1.0) m_lfo -= 1.0;
        auto moving = [](const Line& l, float d) {
            const int di = static_cast<int>(d);
            const float f = d - static_cast<float>(di);
            return l.at(di) + f * (l.at(di + 1) - l.at(di));
        };
        const float mL = static_cast<float>(m_in1) + m_excursion * sin01(m_lfo);
        const float mR = static_cast<float>(m_in2) + m_excursion * sin01(m_lfo + 0.25);

        // Left half: fed by the input and the right half's end.
        float a = allpass(m_apL, x + m_fbR, -0.7f, moving(m_apL, mL));
        m_dL1.push(a);
        float b = m_dL1.out();
        m_dampL += (1.0f - m_damp) * (b - m_dampL);
        b = m_dampL * m_decay;
        b = allpass(m_ap2L, b, 0.5f, m_ap2L.out());
        m_dL2.push(b);
        const float endL = m_dL2.out() * m_decay;

        // Right half: fed by the input and the left half's end.
        a = allpass(m_apR, x + m_fbL, -0.7f, moving(m_apR, mR));
        m_dR1.push(a);
        b = m_dR1.out();
        m_dampR += (1.0f - m_damp) * (b - m_dampR);
        b = m_dampR * m_decay;
        b = allpass(m_ap2R, b, 0.5f, m_ap2R.out());
        m_dR2.push(b);
        const float endR = m_dR2.out() * m_decay;
        m_fbL = endL;
        m_fbR = endR;

        // The fourteen taps.
        float yl = m_dR1.at(m_tapL[0]) + m_dR1.at(m_tapL[1]) - m_ap2R.at(m_tapL[2]) + m_dR2.at(m_tapL[3])
                 - m_dL1.at(m_tapL[4]) - m_ap2L.at(m_tapL[5]) - m_dL2.at(m_tapL[6]);
        float yr = m_dL1.at(m_tapR[0]) + m_dL1.at(m_tapR[1]) - m_ap2L.at(m_tapR[2]) + m_dL2.at(m_tapR[3])
                 - m_dR1.at(m_tapR[4]) - m_ap2R.at(m_tapR[5]) - m_dR2.at(m_tapR[6]);
        // Dattorro's 0.6, lowered by 6.9 dB to the hall's return: measured in the rooms stem of the same piece
        // (Cosmic, seed 5), -24.0 against -30.9 dBFS. The room knob then changes the room, not the level.
        yl *= 0.27f;
        yr *= 0.27f;
        // The return's high pass (one pole): the low cut of the hall's knob.
        m_hpL = m_hpCoef * (m_hpL + yl - m_hpXL); m_hpXL = yl;
        m_hpR = m_hpCoef * (m_hpR + yr - m_hpXR); m_hpXR = yr;
        outL[i] += m_hpL;
        outR[i] += m_hpR;
    }
}

} // namespace eph
