/**
 * @file Rooms.h
 * @brief The production guide's four rooms, the two that were missing (25.09.2026): the early reflections of a room
 *        without its tail (send A: distance and glue without a smear) and the effect hall (send D: a long plate with an
 *        octave-up pitch shifter in its feedback -- the shimmer of the modern-modular school, for drones, pads and the
 *        transitions). The blend room (B) and the hall (C) are the plate and the hall of Engine.h.
 * @author Rene Weller
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
    /**
     * @brief Allocates the line for a sample rate (120 ms) and sets the default reflections.
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare(double sampleRate)
    {
        m_sr = sampleRate;
        size_t len = 1;
        while (len < static_cast<size_t>(0.12 * sampleRate)) len <<= 1;
        m_buf.assign(len, 0.0f);
        m_mask = len - 1;
        m_write = 0;
        for (Svf* f : { &m_hpL, &m_hpR, &m_lpL, &m_lpR }) f->reset();
        set(35.0f, 200.0f, 8000.0f);
    }
    /**
     * @brief The span of the reflections in ms (10 .. 80) and the return's band.
     * @param[in] sizeMs     the span of the reflections in ms, 10 .. 80
     * @param[in] lowCutHz   the return's low cut in Hz
     * @param[in] highCutHz  the return's high cut in Hz
     */
    void set(float sizeMs, float lowCutHz, float highCutHz)
    {
        const float sr = static_cast<float>(m_sr);
        double sum = 0.0;
        for (int k = 0; k < Taps; ++k) {
            // Two golden-ratio walks through the span: the taps of the two sides never coincide.
            const double fl = std::fmod(0.11 + 0.6180339887 * k, 1.0), fr = std::fmod(0.37 + 0.7548776662 * k, 1.0);
            m_tapL[k] = std::max(1, static_cast<int>((3.0 + (sizeMs - 3.0) * fl) * 0.001 * m_sr));
            m_tapR[k] = std::max(1, static_cast<int>((3.0 + (sizeMs - 3.0) * fr) * 0.001 * m_sr));
            m_gain[k] = static_cast<float>(std::pow(0.82, k)) * (k % 2 == 0 ? 1.0f : -1.0f);
            sum += static_cast<double>(m_gain[k]) * m_gain[k];
        }
        const float norm = static_cast<float>(1.0 / std::sqrt(sum));
        for (float& g : m_gain) g *= norm;
        m_hpL.setQ(lowCutHz, 0.7071f, sr); m_hpR.copyCoefficients(m_hpL);
        m_lpL.setQ(highCutHz, 0.7071f, sr); m_lpR.copyCoefficients(m_lpL);
    }
    /**
     * @brief Adds the reflections of the send (@p inL, @p inR) to @p outL, @p outR.
     * @param[in]     inL   the left send
     * @param[in]     inR   the right send
     * @param[in,out] outL  the left output, added to
     * @param[in,out] outR  the right output, added to
     * @param[in]     n     how many samples
     */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i) {
            m_buf[m_write] = 0.5f * (inL[i] + inR[i]);
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < Taps; ++k) {
                l += m_gain[k] * m_buf[(m_write - static_cast<size_t>(m_tapL[k])) & m_mask];
                r += m_gain[k] * m_buf[(m_write - static_cast<size_t>(m_tapR[k])) & m_mask];
            }
            m_write = (m_write + 1) & m_mask;
            float lo, bp, hl, hr;
            m_hpL.tick(l, lo, bp, hl);
            m_hpR.tick(r, lo, bp, hr);
            outL[i] += m_lpL.lp(hl);
            outR[i] += m_lpR.lp(hr);
        }
    }

private:
    static constexpr int Taps = 12;   ///< taps a side
    double m_sr = 48000.0;   ///< the sample rate, Hz
    std::vector<float> m_buf;   ///< the send in mono, a ring
    size_t m_mask = 0;   ///< its size - 1
    size_t m_write = 0;   ///< where the next sample goes
    int m_tapL[Taps] = {};   ///< the left side's taps, samples
    int m_tapR[Taps] = {};   ///< the right side's taps, samples
    float m_gain[Taps] = {};   ///< the taps' gains (alternating, falling, normalised)
    Svf m_hpL;   ///< the return's high pass, left
    Svf m_hpR;   ///< ... right
    Svf m_lpL;   ///< the return's low pass, left
    Svf m_lpR;   ///< ... right
};

/**
 * @brief The effect hall: a long plate whose output, shifted an octave up (two crossfaded taps running at twice the
 *        speed through a delay of its past, 43 ms windows), is fed back into it -- each round of the tail an octave
 *        brighter and fainter. An exact octave: it adds to the harmony, it never detunes it.
 */
class Shimmer {
public:
    /**
     * @brief Prepares the plate and the shifter for a sample rate and sets the default effect.
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare(double sampleRate)
    {
        m_sr = sampleRate;
        m_plate.prepare(sampleRate);
        m_shift.assign(8192, 0.0f);
        m_write = 0;
        m_phase = 0.0;
        m_window = std::max(256.0, 0.043 * sampleRate);
        for (Svf* f : { &m_hpL, &m_hpR, &m_lpL, &m_lpR }) f->reset();
        m_quiet = 0;
        set(8.0f, 0.35f, 400.0f, 4000.0f);
    }
    /**
     * @brief The tail in seconds, the octave's share fed back (0 .. 0.6), the return's band.
     * @param[in] decaySeconds  the tail in seconds
     * @param[in] amount        the octave's share fed back, 0 .. 0.6
     * @param[in] lowCutHz      the return's low cut in Hz
     * @param[in] highCutHz     the return's high cut in Hz
     */
    void set(float decaySeconds, float amount, float lowCutHz, float highCutHz)
    {
        m_plate.set(decaySeconds, 0.35f, 0.0f, 150.0f, 9000.0f);
        m_idleAfter = static_cast<long long>(3.0 * decaySeconds * m_sr);
        m_amount = std::clamp(amount, 0.0f, 0.6f);
        const float sr = static_cast<float>(m_sr);
        m_hpL.setQ(lowCutHz, 0.7071f, sr); m_hpR.copyCoefficients(m_hpL);
        m_lpL.setQ(highCutHz, 0.7071f, sr); m_lpR.copyCoefficients(m_lpL);
    }
    /**
     * @brief Adds the effect of the send to @p outL, @p outR (n at most 64).
     * @param[in]     inL   the left send
     * @param[in]     inR   the right send
     * @param[in,out] outL  the left output, added to
     * @param[in,out] outR  the right output, added to
     * @param[in]     n     how many samples, at most 64
     */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        // Idle once nothing has come in for twice the tail: most of a piece sends nothing here.
        bool silent = true;
        for (int i = 0; i < n && silent; ++i) silent = inL[i] == 0.0f && inR[i] == 0.0f;
        m_quiet = silent ? m_quiet + n : 0;
        if (m_quiet > m_idleAfter) return;
        float xl[64], xr[64], yl[64] = {}, yr[64] = {};
        const size_t mask = m_shift.size() - 1;
        for (int i = 0; i < n; ++i) {
            // The octave up: the delay shrinks by a sample a sample; two taps half a window apart, sin^2 windows.
            // The shortest delay is 64 samples, so a tap never reads what this block has not written yet.
            m_phase -= 1.0 / m_window;
            if (m_phase < 0.0) m_phase += 1.0;
            const double p2 = m_phase + 0.5 >= 1.0 ? m_phase - 0.5 : m_phase + 0.5;
            const size_t d1 = 64 + static_cast<size_t>(m_phase * m_window), d2 = 64 + static_cast<size_t>(p2 * m_window);
            const float w1 = sin01(0.5 * m_phase), w2 = sin01(0.5 * p2);   // sin(pi p) from the table
            const float up = w1 * w1 * m_shift[(m_write - d1) & mask] + w2 * w2 * m_shift[(m_write - d2) & mask];
            xl[i] = inL[i] + m_amount * up;
            xr[i] = inR[i] + m_amount * up;
            m_write = (m_write + 1) & mask;
        }
        m_plate.process(xl, xr, yl, yr, n);
        // What came out goes into the shifter's past, for the next rounds.
        size_t w = (m_write - static_cast<size_t>(n)) & mask;
        for (int i = 0; i < n; ++i) {
            m_shift[w] = 0.5f * (yl[i] + yr[i]);
            w = (w + 1) & mask;
            float lo, bp, hl, hr;
            m_hpL.tick(yl[i], lo, bp, hl);
            m_hpR.tick(yr[i], lo, bp, hr);
            outL[i] += m_lpL.lp(hl);
            outR[i] += m_lpR.lp(hr);
        }
    }

private:
    double m_sr = 48000.0;   ///< the sample rate, Hz
    double m_phase = 0.0;   ///< the shifter's phase, 0..1
    double m_window = 2048.0;   ///< the shifter's window, samples (43 ms)
    Plate m_plate;   ///< the long plate
    std::vector<float> m_shift;   ///< the plate's output, a ring: what the octave reads
    size_t m_write = 0;   ///< where the next sample goes
    float m_amount = 0.35f;   ///< the octave's share fed back
    long long m_quiet = 0;   ///< samples nothing has come in
    long long m_idleAfter = 1000000;   ///< samples of silence after which it rests (three tails)
    Svf m_hpL;   ///< the return's high pass, left
    Svf m_hpR;   ///< ... right
    Svf m_lpL;   ///< the return's low pass, left
    Svf m_lpR;   ///< ... right
};

/**
 * @brief A resonance suppressor in six bands (350 Hz .. 4.8 kHz, Q 2.5): each band's follower (3 ms up, 60 ms down)
 *        against its neighbours'; a band more than 4.5 dB over them -- a local peak, as a resonance is, whatever the
 *        spectrum's tilt -- is pulled back, up to 3:1 at depth 1. Where nothing sticks out it passes the sound as is.
 */
class ResonanceTamer {
public:
    static constexpr int Bands = 6;   ///< the bands
    /**
     * @brief Sets the bands (350 Hz .. 4.8 kHz) and the followers (3 ms up, 60 ms down) for a sample rate.
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare(double sampleRate)
    {
        static const float Centre[Bands] = { 350.0f, 600.0f, 1000.0f, 1700.0f, 2900.0f, 4800.0f };
        const float sr = static_cast<float>(sampleRate);
        for (int k = 0; k < Bands; ++k) {
            m_l[k].setQ(Centre[k], 2.5f, sr); m_l[k].reset();
            m_r[k].setQ(Centre[k], 2.5f, sr); m_r[k].reset();
            m_env[k] = 0.0f;
        }
        m_attack = std::exp(-1.0f / (0.003f * sr));
        m_release = std::exp(-1.0f / (0.06f * sr));
    }
    /**
     * @brief Works on @p L, @p R in place; @p depth 0 leaves them alone.
     * @param[in,out] L      the left channel, processed in place
     * @param[in,out] R      the right channel, processed in place
     * @param[in]     n      how many samples
     * @param[in]     depth  how far it tames, 0 for not at all
     */
    void process(float* L, float* R, int n, float depth)
    {
        if (depth <= 0.0f) return;
        for (int i = 0; i < n; ++i) {
            float bl[Bands], br[Bands];
            for (int k = 0; k < Bands; ++k) {
                float lo, hp;
                m_l[k].tick(L[i], lo, bl[k], hp);
                m_r[k].tick(R[i], lo, br[k], hp);
                bl[k] *= m_l[k].m_k;
                br[k] *= m_r[k].m_k;
                const float x = std::max(std::fabs(bl[k]), std::fabs(br[k]));
                m_env[k] = x + (x > m_env[k] ? m_attack : m_release) * (m_env[k] - x);
            }
            for (int k = 0; k < Bands; ++k) {
                const float around = k == 0 ? m_env[1] : (k == Bands - 1 ? m_env[Bands - 2] : 0.5f * (m_env[k - 1] + m_env[k + 1]));
                const float limit = 1.68f * around;
                if (m_env[k] <= limit || m_env[k] < 1e-6f) continue;
                const float g = std::pow(limit / m_env[k], 0.667f * depth);
                L[i] += (g - 1.0f) * bl[k];
                R[i] += (g - 1.0f) * br[k];
            }
        }
    }

private:
    Svf m_l[Bands];   ///< the bands' band passes, left
    Svf m_r[Bands];   ///< ... right
    float m_env[Bands] = {};   ///< the bands' followers
    float m_attack = 0.0f;   ///< the followers' attack coefficient
    float m_release = 0.0f;   ///< their release coefficient
};

} // namespace eph
