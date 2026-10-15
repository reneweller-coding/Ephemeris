/**
 * @file Bbd.cpp
 * @brief The bucket-brigade delay (Bbd.h).
 * @author Rene Weller
 */
#include "eph/fx/Bbd.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr double Stages = 4096.0;   ///< the MN3005's buckets
}

void BbdEcho::prepare(double sampleRate, double maxSeconds, uint64_t seed)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    size_t n = 4;
    while (static_cast<double>(n) < maxSeconds * m_sr + 16.0) n <<= 1;
    m_bufL.assign(n, 0.0f);
    m_bufR.assign(n, 0.0f);
    m_mask = n - 1;
    m_rng.seed(seed);
    m_delay.setTime(0.25f, m_sr);
    reset();
}

void BbdEcho::reset()
{
    std::fill(m_bufL.begin(), m_bufL.end(), 0.0f);
    std::fill(m_bufR.begin(), m_bufR.end(), 0.0f);
    m_write = 0;
    m_fresh = true;
    m_lfo = 0.0;
    for (Svf* f : { &m_lp1L, &m_lp2L, &m_lp1R, &m_lp2R, &m_hpL, &m_hpR }) f->reset();
}

void BbdEcho::set(const EchoSettings& s)
{
    m_s = s;
    const double maxDelay = static_cast<double>(m_mask) - 8.0;
    m_target = std::clamp(s.m_delaySeconds * m_sr, 4.0, maxDelay);
    if (m_fresh) { m_delay.snap(static_cast<float>(m_target)); m_fresh = false; }
    // The clock for this delay, and the filters at a third of it: longer delays are darker.
    const double clock = Stages / (2.0 * std::max(0.01, s.m_delaySeconds));
    const float sr = static_cast<float>(m_sr);
    const float cutoff = static_cast<float>(std::min({ static_cast<double>(s.m_toneHz), clock / 3.0, 0.45 * m_sr }));
    m_lp1L.setQ(cutoff, 0.54f, sr); m_lp2L.setQ(cutoff, 1.31f, sr);   // a fourth-order Butterworth
    m_lp1R.copyCoefficients(m_lp1L); m_lp2R.copyCoefficients(m_lp2L);
    m_hpL.set(60.0f, 0.0f, sr); m_hpR.copyCoefficients(m_hpL);
    m_drive = dbToGain(s.m_driveDb);
    m_driveNorm = 1.0f / m_drive;
    m_hiss = 0.00015f * (1.0f + s.m_feedback);   // the compander's floor, a little more with more repeats
}

float BbdEcho::read(const std::vector<float>& buf, double d) const
{
    const double pos = static_cast<double>(m_write) - d;
    const double fl = std::floor(pos);
    const float t = static_cast<float>(pos - fl);
    const size_t i = static_cast<size_t>(static_cast<int64_t>(fl)) & m_mask;
    const float y0 = buf[(i - 1) & m_mask], y1 = buf[i], y2 = buf[(i + 1) & m_mask], y3 = buf[(i + 2) & m_mask];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void BbdEcho::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i) {
        // The clock's slow wander (0.4 Hz), a chorus in the repeats rather than a tape's wow.
        m_lfo += 0.4 / m_sr;
        if (m_lfo >= 1.0) m_lfo -= 1.0;
        const double wander = 0.001 * m_sr * static_cast<double>(m_s.m_wowMs) * sin01(m_lfo);
        const double d = std::max(4.0, static_cast<double>(m_delay.next(static_cast<float>(m_target))) + wander);
        float yl = read(m_bufL, d), yr = read(m_bufR, d);
        // Reconstruction filter on the way out of the chain.
        yl = m_lp2L.lp(m_lp1L.lp(yl));
        yr = m_lp2R.lp(m_lp1R.lp(yr));
        float lo, bo, hl, hr;
        m_hpL.tick(yl, lo, bo, hl);
        m_hpR.tick(yr, lo, bo, hr);
        // The compander's saturation and hiss in the loop.
        const float ll = std::tanh(m_drive * hl) * m_driveNorm + m_hiss * m_rng.bipolar();
        const float lr = std::tanh(m_drive * hr) * m_driveNorm + m_hiss * m_rng.bipolar();
        const float fb = m_s.m_feedback;
        if (m_s.m_pingPong) {
            m_bufL[m_write] = 0.5f * (inL[i] + inR[i]) + fb * lr;
            m_bufR[m_write] = fb * ll;
        } else {
            m_bufL[m_write] = inL[i] + fb * ll;
            m_bufR[m_write] = inR[i] + fb * lr;
        }
        m_write = (m_write + 1) & m_mask;
        outL[i] += yl;
        outR[i] += yr;
    }
}

} // namespace eph
