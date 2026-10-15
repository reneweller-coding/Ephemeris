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
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_rng.seed(seed);
    m_grainRng.seed(mixSeed(seed, 77));
    for (Grain& g : m_grains) g = Grain{};
    m_grainsOn = 0;
    m_windL.reset(); m_windR.reset(); m_sweep.reset();
    m_wanderL.m_x = m_wanderR.m_x = 0.0;
    m_pinkL = m_pinkR = 0.0f;
    m_sweepAge = -1.0;
    m_bleepsLeft = 0;
    m_count = 0;
    m_windNow = 0.0f;
}

void Atmos::set(const AtmosSettings& s) { m_s = s; }

bool Atmos::active() const
{
    return m_s.m_windGain > 1e-5f || m_windNow > 1e-5f || m_sweepAge >= 0.0 || m_bleepsLeft > 0
        || m_s.m_sweepsPerMinute > 0.0f || m_s.m_bleepsPerMinute > 0.0f || m_s.m_grainGain > 0.0f || m_grainsOn > 0;
}

void Atmos::process(float* L, float* R, float* bleepL, float* bleepR, int n)
{
    const float sr = static_cast<float>(m_sr);
    for (int i = 0; i < n; ++i) {
        if ((m_count & 31) == 0) {
            const double dt = 32.0 / m_sr;
            // The wind's centres wander half an octave on a quarter-minute time constant, apart.
            const double cl = m_s.m_windHz * std::exp2(0.5 * m_wanderL.step(dt, 15.0, 1.0, m_rng));
            const double cr = m_s.m_windHz * std::exp2(0.5 * m_wanderR.step(dt, 15.0, 1.0, m_rng));
            m_windL.setQ(static_cast<float>(std::min(cl, 0.4 * m_sr)), 2.5f, sr);
            m_windR.setQ(static_cast<float>(std::min(cr, 0.4 * m_sr)), 2.5f, sr);
            // Poisson starts: the chance of an event in this cell.
            if (m_sweepAge < 0.0 && m_s.m_sweepsPerMinute > 0.0f && m_rng.uniform() < m_s.m_sweepsPerMinute * dt / 60.0) {
                m_sweepAge = 0.0;
                m_sweepLen = 4.0 + 8.0 * static_cast<double>(m_rng.uniform());
                const bool up = m_rng.uniform() < 0.5f;
                m_sweepFrom = up ? 200.0 : 5000.0;
                m_sweepTo = up ? 5000.0 : 200.0;
                m_sweepPan = m_rng.bipolar() * 0.7f;
            }
            if (m_bleepsLeft == 0 && m_s.m_bleepsPerMinute > 0.0f && m_rng.uniform() < m_s.m_bleepsPerMinute * dt / 60.0) {
                m_bleepsLeft = 2 + m_rng.below(4);
                m_bleepAge = 0.0;
                m_bleepEvery = 0.08 + 0.12 * static_cast<double>(m_rng.uniform());
                m_bleepPan = m_rng.bipolar() * 0.8f;
            }
            // A new grain: the chance of one in this cell (Poisson), on the grains' own stream.
            if (m_s.m_grainGain > 0.0f && m_grainsOn < Grains && m_grainRng.uniform() < m_s.m_grainsPerSecond * dt) {
                for (Grain& g : m_grains) {
                    if (g.m_on) continue;
                    const int d = m_grainRng.below(scaleSize(m_s.m_scale) * 2);
                    g.m_on = true;
                    g.m_inc = midiToHz(72 + m_s.m_rootPc % 12 + scaleSemitones(m_s.m_scale, d)) / m_sr;
                    g.m_phase = 0.0;
                    g.m_age = 0.0;
                    g.m_length = 0.04 + 0.21 * static_cast<double>(m_grainRng.uniform());
                    g.m_amp = 0.5f + 0.5f * m_grainRng.uniform();
                    const float pan = m_grainRng.bipolar();
                    g.m_gainL = std::sqrt(0.5f * (1.0f - pan));
                    g.m_gainR = std::sqrt(0.5f * (1.0f + pan));
                    ++m_grainsOn;
                    break;
                }
            }
            if (m_sweepAge >= 0.0) {
                const double t = std::min(1.0, m_sweepAge / m_sweepLen);
                m_sweep.setQ(static_cast<float>(m_sweepFrom * std::pow(m_sweepTo / m_sweepFrom, t)), 8.0f, sr);
            }
        }
        ++m_count;
        m_windNow += (m_s.m_windGain - m_windNow) * 0.0005f;
        // Two pink-ish noises (a one-pole tilt on white), one per side.
        m_pinkL += 0.08f * (m_rng.bipolar() - m_pinkL);
        m_pinkR += 0.08f * (m_rng.bipolar() - m_pinkR);
        float lo, bp, hi;
        m_windL.tick(m_pinkL, lo, bp, hi);
        const float wl = bp * m_windL.m_k;
        m_windR.tick(m_pinkR, lo, bp, hi);
        const float wr = bp * m_windR.m_k;
        float l = 2.0f * m_windNow * wl, r = 2.0f * m_windNow * wr;
        if (m_sweepAge >= 0.0) {
            const double t = m_sweepAge / m_sweepLen;
            const float env = static_cast<float>(std::sin(Pi * std::min(1.0, t)));   // in and out over its length
            m_sweep.tick(0.5f * (m_pinkL + m_pinkR), lo, bp, hi);
            const float v = 1.5f * m_s.m_sweepGain * env * bp;
            l += v * (1.0f - m_sweepPan) * 0.7f;
            r += v * (1.0f + m_sweepPan) * 0.7f;
            m_sweepAge += 1.0 / m_sr;
            if (m_sweepAge >= m_sweepLen) m_sweepAge = -1.0;
        }
        if (m_bleepsLeft > 0) {
            if (m_bleepAge <= 0.0) {
                // Sample and hold: a random note of the scale over the root, two or three octaves up.
                const int d = m_rng.below(scaleSize(m_s.m_scale) * 2);
                m_bleepHz = midiToHz(72 + m_s.m_rootPc % 12 + scaleSemitones(m_s.m_scale, d));
                m_bleepPhase = 0.0;
            }
            m_bleepPhase += m_bleepHz / m_sr;
            if (m_bleepPhase >= 1.0) m_bleepPhase -= 1.0;
            const double env = std::exp(-m_bleepAge / (0.25 * m_bleepEvery));
            const float v = m_s.m_bleepGain * static_cast<float>(env) * sin01(m_bleepPhase);
            const float bl = v * (1.0f - m_bleepPan) * 0.7f, br = v * (1.0f + m_bleepPan) * 0.7f;
            l += bl; r += br;
            bleepL[i] += bl; bleepR[i] += br;
            m_bleepAge += 1.0 / m_sr;
            if (m_bleepAge >= m_bleepEvery) { m_bleepAge = 0.0; --m_bleepsLeft; }
        }
        if (m_grainsOn > 0) {
            // The cloud: each grain a sine under a Hann window over its length.
            float gl = 0.0f, gr = 0.0f;
            for (Grain& g : m_grains) {
                if (!g.m_on) continue;
                const float w = sin01(0.5 * g.m_age / g.m_length);
                const float v = g.m_amp * w * w * sin01(g.m_phase);
                gl += v * g.m_gainL;
                gr += v * g.m_gainR;
                g.m_phase += g.m_inc;
                if (g.m_phase >= 1.0) g.m_phase -= 1.0;
                g.m_age += 1.0 / m_sr;
                if (g.m_age >= g.m_length) { g.m_on = false; --m_grainsOn; }
            }
            l += 0.35f * m_s.m_grainGain * gl;
            r += 0.35f * m_s.m_grainGain * gr;
        }
        L[i] += l;
        R[i] += r;
    }
}

} // namespace eph
