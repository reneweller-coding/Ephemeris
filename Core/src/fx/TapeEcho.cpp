/**
 * @file TapeEcho.cpp
 * @brief The tape echo.
 * @author Rene Weller
 */
#include "eph/fx/TapeEcho.h"
#include <algorithm>
#include <cmath>

namespace eph {

void TapeEcho::prepare(double sampleRate, double maxSeconds, uint64_t seed)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    size_t n = 4;
    while (static_cast<double>(n) < (maxSeconds + 0.05) * m_sr + 8.0) n <<= 1;
    m_bufL.assign(n, 0.0f);
    m_bufR.assign(n, 0.0f);
    m_mask = n - 1;
    m_rng.seed(seed);
    m_delay.setTime(0.12f, m_sr);   // the tape takes about a tenth of a second to change speed
    reset();
}

void TapeEcho::reset()
{
    std::fill(m_bufL.begin(), m_bufL.end(), 0.0f);
    std::fill(m_bufR.begin(), m_bufR.end(), 0.0f);
    m_write = 0;
    m_wowPhase = m_flutterPhase = 0.0;
    m_drift = m_driftTarget = 0.0;
    m_count = 0;
    m_fresh = true;
    m_lpL.reset(); m_lpR.reset(); m_hpL.reset(); m_hpR.reset();
}

void TapeEcho::set(const EchoSettings& s)
{
    m_s = s;
    const double maxDelay = static_cast<double>(m_mask) - 8.0;
    m_target = std::clamp(s.m_delaySeconds * m_sr, 4.0, maxDelay);
    if (m_fresh) { m_delay.snap(static_cast<float>(m_target)); m_fresh = false; }
    const float sr = static_cast<float>(m_sr);
    m_lpL.set(s.m_toneHz, 0.0f, sr); m_lpR.copyCoefficients(m_lpL);
    m_hpL.set(s.m_lowCutHz, 0.0f, sr);    m_hpR.copyCoefficients(m_hpL);
    m_drive = dbToGain(s.m_driveDb);
    m_driveNorm = 1.0f / m_drive;
}

float TapeEcho::read(const std::vector<float>& buf, double d) const
{
    // Four-point Hermite around the fractional read position.
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

void TapeEcho::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const double wowHz = 0.7, flutterHz = 9.0;
    for (int i = 0; i < n; ++i) {
        if ((m_count & 31) == 0) {
            // A new drift target every ~0.7 s, approached slowly: the part of wow that is not periodic.
            if ((m_count % 32768) == 0) m_driftTarget = static_cast<double>(m_rng.bipolar());
            m_drift += (m_driftTarget - m_drift) * 0.002;
        }
        ++m_count;
        m_wowPhase += wowHz / m_sr;
        if (m_wowPhase >= 1.0) m_wowPhase -= 1.0;
        m_flutterPhase += flutterHz / m_sr;
        if (m_flutterPhase >= 1.0) m_flutterPhase -= 1.0;
        const double wander = 0.001 * m_sr * (static_cast<double>(m_s.m_wowMs) * (0.7 * sin01(m_wowPhase) + 0.3 * m_drift)
                                             + static_cast<double>(m_s.m_flutterMs) * sin01(m_flutterPhase));
        const double d = std::max(4.0, static_cast<double>(m_delay.next(static_cast<float>(m_target))) + wander);
        const float yl = read(m_bufL, d), yr = read(m_bufR, d);
        // The loop: tone, rumble filter, saturation, gain.
        float lo, bo, hl, hr;
        m_hpL.tick(m_lpL.lp(yl), lo, bo, hl);
        m_hpR.tick(m_lpR.lp(yr), lo, bo, hr);
        const float ll = std::tanh(m_drive * hl) * m_driveNorm;
        const float lr = std::tanh(m_drive * hr) * m_driveNorm;
        const float fb = m_s.m_feedback;
        const float inMono = 0.5f * (inL[i] + inR[i]);
        if (m_s.m_pingPong) {
            m_bufL[m_write] = inMono + fb * lr;
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
