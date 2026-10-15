/**
 * @file Clock.cpp
 * @brief Note divisions and the tempo map.
 * @note Copied from Phosphene `Core/src/Clock.cpp` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 * @author Rene Weller
 */
#include "eph/Clock.h"
#include <algorithm>
#include <cmath>

namespace eph {

const char* const SyncDivNames[NumSyncDivs] = {
    "Free", "64 bars", "32 bars", "16 bars", "8 bars", "4 bars", "2 bars", "1 bar",
    "1/2", "1/2 T", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 T", "1/32",
};

const double SyncBeats[NumSyncDivs] = {
    0.0, 256.0, 128.0, 64.0, 32.0, 16.0, 8.0, 4.0,
    2.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125,
};

namespace {

/**
 * @brief @p bpm limited to 20 .. 400.
 * @param[in] bpm  the tempo
 * @return the tempo limited to 20 .. 400
 */
double clampBpm(double bpm) { return bpm < 20.0 ? 20.0 : (bpm > 400.0 ? 400.0 : bpm); }

/**
 * @brief Seconds spent from beat offset 0 to @p db inside a segment starting at tempo @p b0 with
 *        slope @p s (BPM per beat).
 *
 * dt/db = 60 / (b0 + s db), so t = (60 / s) ln(1 + s db / b0), and 60 db / b0 when s is zero. The
 * log1p form stays accurate for the tiny slopes of a slow drift.
 * @param[in] b0  the tempo at the segment's start
 * @param[in] s   the slope in BPM per beat
 * @param[in] db  the beat offset into the segment
 * @return the seconds spent up to it
 */
double segmentSeconds(double b0, double s, double db)
{
    if (std::fabs(s) < 1e-12) return 60.0 * db / b0;
    return 60.0 / s * std::log1p(s * db / b0);
}

/**
 * @brief Inverse of segmentSeconds: the beat offset reached after @p dt seconds.
 * @param[in] b0  the tempo at the segment's start
 * @param[in] s   the slope in BPM per beat
 * @param[in] dt  the seconds into the segment
 * @return the beat offset reached
 */
double segmentBeats(double b0, double s, double dt)
{
    if (std::fabs(s) < 1e-12) return dt * b0 / 60.0;
    return b0 * std::expm1(s * dt / 60.0) / s;
}

} // namespace

void TempoMap::setConstant(double bpm)
{
    m_points.assign(1, TempoPoint{ 0.0, clampBpm(bpm), false });
    rebuild();
}

void TempoMap::add(double beat, double bpm, bool rampToNext)
{
    TempoPoint p{ beat < 0.0 ? 0.0 : beat, clampBpm(bpm), rampToNext };
    auto it = std::lower_bound(m_points.begin(), m_points.end(), p.m_beat,
                               [](const TempoPoint& a, double b) { return a.m_beat < b; });
    if (it != m_points.end() && it->m_beat == p.m_beat) *it = p;
    else m_points.insert(it, p);
    if (m_points.front().m_beat > 0.0) m_points.insert(m_points.begin(), TempoPoint{ 0.0, m_points.front().m_bpm, false });
    rebuild();
}

void TempoMap::rebuild()
{
    m_seconds.assign(m_points.size(), 0.0);
    for (size_t i = 1; i < m_points.size(); ++i) {
        const TempoPoint& a = m_points[i - 1];
        const double db = m_points[i].m_beat - a.m_beat;
        const double s = a.m_rampToNext ? (m_points[i].m_bpm - a.m_bpm) / db : 0.0;
        m_seconds[i] = m_seconds[i - 1] + segmentSeconds(a.m_bpm, s, db);
    }
}

int TempoMap::segmentFor(double beat) const
{
    auto it = std::upper_bound(m_points.begin(), m_points.end(), beat,
                               [](double b, const TempoPoint& a) { return b < a.m_beat; });
    const long idx = static_cast<long>(it - m_points.begin()) - 1;
    return idx < 0 ? 0 : static_cast<int>(idx);
}

double TempoMap::bpmAt(double beat) const
{
    const int i = segmentFor(beat);
    const TempoPoint& a = m_points[static_cast<size_t>(i)];
    if (!a.m_rampToNext || i + 1 >= static_cast<int>(m_points.size())) return a.m_bpm;
    const TempoPoint& b = m_points[static_cast<size_t>(i + 1)];
    const double x = (beat - a.m_beat) / (b.m_beat - a.m_beat);
    return a.m_bpm + (b.m_bpm - a.m_bpm) * (x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x));
}

double TempoMap::secondsAt(double beat) const
{
    if (beat <= 0.0) return 60.0 * beat / m_points.front().m_bpm;
    const int i = segmentFor(beat);
    const TempoPoint& a = m_points[static_cast<size_t>(i)];
    double s = 0.0;
    if (a.m_rampToNext && i + 1 < static_cast<int>(m_points.size()))
        s = (m_points[static_cast<size_t>(i + 1)].m_bpm - a.m_bpm) / (m_points[static_cast<size_t>(i + 1)].m_beat - a.m_beat);
    return m_seconds[static_cast<size_t>(i)] + segmentSeconds(a.m_bpm, s, beat - a.m_beat);
}

double TempoMap::beatAt(double seconds) const
{
    if (seconds <= 0.0) return seconds * m_points.front().m_bpm / 60.0;
    auto it = std::upper_bound(m_seconds.begin(), m_seconds.end(), seconds);
    const long idx = static_cast<long>(it - m_seconds.begin()) - 1;
    const int i = idx < 0 ? 0 : static_cast<int>(idx);
    const TempoPoint& a = m_points[static_cast<size_t>(i)];
    double s = 0.0;
    if (a.m_rampToNext && i + 1 < static_cast<int>(m_points.size()))
        s = (m_points[static_cast<size_t>(i + 1)].m_bpm - a.m_bpm) / (m_points[static_cast<size_t>(i + 1)].m_beat - a.m_beat);
    return a.m_beat + segmentBeats(a.m_bpm, s, seconds - m_seconds[static_cast<size_t>(i)]);
}

} // namespace eph
