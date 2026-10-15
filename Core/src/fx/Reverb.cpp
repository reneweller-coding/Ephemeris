/**
 * @file Reverb.cpp
 * @brief FDN reverb implementation.
 * @note Copied from Phosphene `Core/src/Reverb.cpp` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 * @author Rene Weller
 */
#include "eph/fx/Reverb.h"
#include "eph/Clock.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
/**
 * @brief The smallest power of two at least @p n.
 * @param[in] n  the lower bound
 * @return the smallest power of two at least n
 */
int pow2At(int n) { int p = 1; while (p < n) p <<= 1; return p; }
constexpr double PiD = 3.14159265358979323846;   ///< pi

/**
 * @brief Which output channel each delay line feeds, and which input it is driven by (1 = left).
 *
 * Not the first half against the second. The line set is ordered by length (29.7 .. 89.0 ms at size
 * one), so splitting it in the middle gave the left channel the four short lines and the right the
 * four long ones -- and a line's length is where its comb peaks sit, so the two returns were not the
 * same instrument: measured over the third octaves from 500 Hz to 8 kHz they differed by 1.45 dB rms
 * in the room and 1.36 in the hall, worst band 2.05 and 2.55 dB. Each channel now takes two short
 * lines and two long ones ({29.7, 46.6, 58.6, 68.4} against {31.4, 39.3, 55.1, 89.0}), which leaves
 * the two returns as decorrelated as before -- they still share no line -- while giving them the same
 * distribution of delays and therefore the same colour. The sign an output tap carries is the sign
 * that line's input carries, so the first pass through a line still adds rather than cancels.
 */
constexpr int Left[8] = { 1, 0, 0, 1, 0, 1, 1, 0 };

/**
 * @brief Linear-interpolated read @p delay samples behind the write position @p w.
 * @param[in] buf    the ring
 * @param[in] mask   its length minus one
 * @param[in] w      the write position
 * @param[in] delay  how many samples behind it, fractional
 * @return the interpolated sample
 */
inline float ringRead(const float* buf, int mask, int w, float delay)
{
    // In double: a float write index of 2^17 resolves only 1/64 of a sample.
    const double pos = static_cast<double>(w) - static_cast<double>(delay);
    const double fl = std::floor(pos);
    const float frac = static_cast<float>(pos - fl);
    const int i0 = static_cast<int>(fl) & mask;
    return buf[i0] + frac * (buf[(i0 + 1) & mask] - buf[i0]);
}
} // namespace

void Reverb::prepare(double sampleRate)
{
    m_sr = sampleRate;
    // The longest line at size 3 plus the pre-delay of up to half a second.
    const int size = pow2At(static_cast<int>(0.1 * 3.0 * m_sr + 0.6 * m_sr) + 64);
    m_mask = size - 1;
    for (auto& l : m_line) l.assign(static_cast<size_t>(size), 0.0f);
    for (auto& a : m_ap) a.assign(static_cast<size_t>(size), 0.0f);
    for (auto& a : m_apR) a.assign(static_cast<size_t>(size), 0.0f);
    for (auto& s : m_sc) s.assign(static_cast<size_t>(size), 0.0f);
    m_pre.assign(static_cast<size_t>(size), 0.0f);
    m_preR.assign(static_cast<size_t>(size), 0.0f);
    static const float ApMs[Allpasses] = { 5.1f, 7.3f, 11.3f, 13.7f };
    for (int k = 0; k < Allpasses; ++k) m_apLen[k] = std::max(1, static_cast<int>(ApMs[k] * m_sr / 1000.0));
    static const float ScMs[Lines] = { 1.9f, 2.3f, 2.9f, 3.7f, 4.3f, 5.3f, 6.1f, 7.1f };
    for (int l = 0; l < Lines; ++l) m_scLen[l] = std::max(1, static_cast<int>(ScMs[l] * m_sr / 1000.0));
    static const float ModHz[Lines] = { 0.11f, 0.13f, 0.17f, 0.19f, 0.23f, 0.29f, 0.31f, 0.37f };
    for (int l = 0; l < Lines; ++l) m_modRate[l] = ModHz[l];
    m_dcR = 1.0f - TwoPi * 5.0f / static_cast<float>(m_sr);
    reset();
    set(1.0f, 2.0f, 0.4f, 0.0f, 250.0f, 9000.0f);
}

void Reverb::reset()
{
    for (auto& l : m_line) std::fill(l.begin(), l.end(), 0.0f);
    for (auto& a : m_ap) std::fill(a.begin(), a.end(), 0.0f);
    for (auto& a : m_apR) std::fill(a.begin(), a.end(), 0.0f);
    for (auto& s : m_sc) std::fill(s.begin(), s.end(), 0.0f);
    std::fill(m_pre.begin(), m_pre.end(), 0.0f);
    std::fill(m_preR.begin(), m_preR.end(), 0.0f);
    m_w = 0;
    for (int l = 0; l < Lines; ++l) { m_lp[l] = 0.0f; m_modPh[l] = l / static_cast<double>(Lines); m_lenCur[l] = 0.0f; }
    m_preCur = -1.0f;
    m_dcX[0] = m_dcX[1] = m_dcY[0] = m_dcY[1] = 0.0f;
    m_hcL = m_hcR = m_lcL1 = m_lcR1 = m_lcL2 = m_lcR2 = 0.0f;
    m_duckEnv = 0.0f;
}

void Reverb::set(float size, float decaySeconds, float damping, float preDelaySamples, float lowCutHz, float highCutHz)
{
    // Noctuary's colourless line set (Tools/optimise_fdn.py, 0.314 dB third-octave spread).
    static const float FlatMs[Lines] = { 29.7f, 31.4f, 39.3f, 46.6f, 55.1f, 58.6f, 68.4f, 89.0f };
    const float s = clampv(size, 0.3f, 3.0f);
    const float decay = std::max(decaySeconds, 0.1f);
    m_damp = clampv(damping, 0.0f, 1.0f);
    const float maxLen = static_cast<float>(m_mask) - 8.0f;
    for (int l = 0; l < Lines; ++l) {
        m_lenTarget[l] = std::min(FlatMs[l] * s * static_cast<float>(m_sr / 1000.0), maxLen);
        if (m_lenCur[l] <= 0.0f) m_lenCur[l] = m_lenTarget[l];
        m_gain[l] = std::pow(10.0f, -3.0f * m_lenTarget[l] / (decay * static_cast<float>(m_sr)));
    }
    m_preTarget = clampv(preDelaySamples, 0.0f, maxLen - m_lenTarget[Lines - 1]);
    if (m_preCur < 0.0f) m_preCur = m_preTarget;
    const float hc = clampv(highCutHz, 500.0f, 20000.0f);
    m_hcCoef = hc >= 19000.0f ? 1.0f : 1.0f - std::exp(-TwoPi * hc / static_cast<float>(m_sr));
    // Two cascaded one-poles are 6 dB down at their corner and 3 dB down at 1.554 times it.
    // Phosphene kept the return above 150 Hz for its phase-locked kick and bass; Ephemeris has none,
    // and a hall under a drone may reach lower. The caller's knob decides, down to 40 Hz.
    const float lc = std::max(40.0f, lowCutHz);
    m_lcCoef = 1.0f - std::exp(-TwoPi * (lc / 1.5538f) / static_cast<float>(m_sr));
}

void Reverb::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float lpc = 1.0f - 0.92f * m_damp;
    constexpr float inGain = 0.5f, glide = 0.0005f;
    for (int i = 0; i < n; ++i) {
        const float xl0 = inL[i] - m_dcX[0] + m_dcR * m_dcY[0]; m_dcX[0] = inL[i]; m_dcY[0] = xl0;
        const float xr0 = inR[i] - m_dcX[1] + m_dcR * m_dcY[1]; m_dcX[1] = inR[i]; m_dcY[1] = xr0;
        m_pre[static_cast<size_t>(m_w & m_mask)] = xl0;
        m_preR[static_cast<size_t>(m_w & m_mask)] = xr0;
        m_preCur += (m_preTarget - m_preCur) * glide;
        float xl = ringRead(m_pre.data(), m_mask, m_w, m_preCur + 1.0f);
        float xr = ringRead(m_preR.data(), m_mask, m_w, m_preCur + 1.0f);
        for (int k = 0; k < Allpasses; ++k) {
            float* a = m_ap[k].data();
            float* ar = m_apR[k].data();
            const float d = a[(m_w - m_apLen[k]) & m_mask];
            const float dr = ar[(m_w - m_apLen[k]) & m_mask];
            const float y = d - 0.6f * xl;
            const float yr = dr - 0.6f * xr;
            a[m_w & m_mask] = xl + 0.6f * y;
            ar[m_w & m_mask] = xr + 0.6f * yr;
            xl = y;
            xr = yr;
        }
        float o[Lines];
        float sum = 0.0f;
        for (int l = 0; l < Lines; ++l) {
            m_lenCur[l] += (m_lenTarget[l] - m_lenCur[l]) * glide;
            m_modPh[l] += m_modRate[l] / m_sr;
            if (m_modPh[l] >= 1.0) m_modPh[l] -= 1.0;
            const float d = m_lenCur[l] + 1.5f * sin01(m_modPh[l]) + 2.0f;
            float v = ringRead(m_line[l].data(), m_mask, m_w, d);
            float* sb = m_sc[l].data();
            const float sd = sb[(m_w - m_scLen[l]) & m_mask];
            const float sy = sd - 0.5f * v;
            sb[m_w & m_mask] = v + 0.5f * sy;
            v = sy;
            m_lp[l] += lpc * (v - m_lp[l]);
            o[l] = m_lp[l];
            sum += o[l];
        }
        const float hh = sum * (2.0f / static_cast<float>(Lines));   // Householder reflection
        float wl = 0.0f, wr = 0.0f;
        for (int l = 0; l < Lines; ++l) {
            const float in = Left[l] ? xl : xr;
            m_line[l][static_cast<size_t>(m_w & m_mask)] = m_gain[l] * (o[l] - hh) + ((l & 1) ? -inGain : inGain) * in;
            const float tap = (l & 1) ? -o[l] : o[l];
            if (Left[l] != 0) wl += tap; else wr += tap;
        }
        const float wetL = 0.3f * wl;
        const float wetR = 0.3f * wr;
        m_hcL += m_hcCoef * (wetL - m_hcL);
        m_hcR += m_hcCoef * (wetR - m_hcR);
        m_lcL1 += m_lcCoef * (m_hcL - m_lcL1);   const float aL = m_hcL - m_lcL1;
        m_lcR1 += m_lcCoef * (m_hcR - m_lcR1);   const float aR = m_hcR - m_lcR1;
        m_lcL2 += m_lcCoef * (aL - m_lcL2);
        m_lcR2 += m_lcCoef * (aR - m_lcR2);
        outL[i] = aL - m_lcL2;
        outR[i] = aR - m_lcR2;
        m_w = (m_w + 1) & m_mask;
    }
}

void Reverb::setDuck(float depth, float thresholdDb, float attackS, float releaseS)
{
    m_duckDepth = clampv(depth, 0.0f, 1.0f);
    m_duckThreshold = dbToGain(thresholdDb);
    m_duckAttackC = 1.0f - std::exp(-1.0f / (std::max(attackS, 0.0005f) * static_cast<float>(m_sr)));
    m_duckReleaseC = 1.0f - std::exp(-1.0f / (std::max(releaseS, 0.0005f) * static_cast<float>(m_sr)));
}

void Reverb::processDucked(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    process(inL, inR, outL, outR, n);
    for (int i = 0; i < n; ++i) {
        // The send's own peak, above or below the threshold: no note events needed, the duck follows
        // whatever actually reached this bus (stutters, level automation, the works).
        const float lvl = std::max(std::fabs(inL[i]), std::fabs(inR[i]));
        const float target = lvl > m_duckThreshold ? 1.0f : 0.0f;
        m_duckEnv += (target - m_duckEnv) * (target > m_duckEnv ? m_duckAttackC : m_duckReleaseC);
        const float g = 1.0f - m_duckDepth * m_duckEnv;
        outL[i] *= g;
        outR[i] *= g;
    }
}

float Reverb::barGate(double beat, double closeBeats, double holdBeats, double openBeats, float floorGain)
{
    const double bars = beat / static_cast<double>(BeatsPerBar);
    double sinceBar = (bars - std::floor(bars)) * static_cast<double>(BeatsPerBar);
    if (sinceBar < 0.0) sinceBar += static_cast<double>(BeatsPerBar);   // guard: beat < 0 in a test
    const double c = std::max(closeBeats, 1e-6), h = std::max(holdBeats, 0.0), o = std::max(openBeats, 1e-6);
    if (sinceBar < c) {
        const double x = sinceBar / c;
        return floorGain + (1.0f - floorGain) * static_cast<float>(0.5 + 0.5 * std::cos(PiD * x));
    }
    if (sinceBar < c + h) return floorGain;
    if (sinceBar < c + h + o) {
        const double x = (sinceBar - c - h) / o;
        return floorGain + (1.0f - floorGain) * static_cast<float>(0.5 - 0.5 * std::cos(PiD * x));
    }
    return 1.0f;
}

} // namespace eph
