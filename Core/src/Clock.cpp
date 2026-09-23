/**
 * @file Clock.cpp
 * @brief Note divisions and the tempo map.
 * @note Copied from Phosphene `Core/src/Clock.cpp` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#include "eph/Clock.h"
#include <algorithm>
#include <cmath>

namespace eph {

const char* const kSyncDivNames[kNumSyncDivs] = {
    "Free", "64 bars", "32 bars", "16 bars", "8 bars", "4 bars", "2 bars", "1 bar",
    "1/2", "1/2 T", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 T", "1/32",
};

const double kSyncBeats[kNumSyncDivs] = {
    0.0, 256.0, 128.0, 64.0, 32.0, 16.0, 8.0, 4.0,
    2.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125,
};

namespace {

double clampBpm(double bpm) { return bpm < 20.0 ? 20.0 : (bpm > 400.0 ? 400.0 : bpm); }

/**
 * @brief Seconds spent from beat offset 0 to @p db inside a segment starting at tempo @p b0 with
 *        slope @p s (BPM per beat).
 *
 * dt/db = 60 / (b0 + s db), so t = (60 / s) ln(1 + s db / b0), and 60 db / b0 when s is zero. The
 * log1p form stays accurate for the tiny slopes of a slow drift.
 */
double segmentSeconds(double b0, double s, double db)
{
    if (std::fabs(s) < 1e-12) return 60.0 * db / b0;
    return 60.0 / s * std::log1p(s * db / b0);
}

/** @brief Inverse of segmentSeconds: the beat offset reached after @p dt seconds. */
double segmentBeats(double b0, double s, double dt)
{
    if (std::fabs(s) < 1e-12) return dt * b0 / 60.0;
    return b0 * std::expm1(s * dt / 60.0) / s;
}

} // namespace

void TempoMap::setConstant(double bpm)
{
    points_.assign(1, TempoPoint{ 0.0, clampBpm(bpm), false });
    rebuild();
}

void TempoMap::add(double beat, double bpm, bool rampToNext)
{
    TempoPoint p{ beat < 0.0 ? 0.0 : beat, clampBpm(bpm), rampToNext };
    auto it = std::lower_bound(points_.begin(), points_.end(), p.beat,
                               [](const TempoPoint& a, double b) { return a.beat < b; });
    if (it != points_.end() && it->beat == p.beat) *it = p;
    else points_.insert(it, p);
    if (points_.front().beat > 0.0) points_.insert(points_.begin(), TempoPoint{ 0.0, points_.front().bpm, false });
    rebuild();
}

void TempoMap::rebuild()
{
    seconds_.assign(points_.size(), 0.0);
    for (size_t i = 1; i < points_.size(); ++i) {
        const TempoPoint& a = points_[i - 1];
        const double db = points_[i].beat - a.beat;
        const double s = a.rampToNext ? (points_[i].bpm - a.bpm) / db : 0.0;
        seconds_[i] = seconds_[i - 1] + segmentSeconds(a.bpm, s, db);
    }
}

int TempoMap::segmentFor(double beat) const
{
    auto it = std::upper_bound(points_.begin(), points_.end(), beat,
                               [](double b, const TempoPoint& a) { return b < a.beat; });
    const long idx = static_cast<long>(it - points_.begin()) - 1;
    return idx < 0 ? 0 : static_cast<int>(idx);
}

double TempoMap::bpmAt(double beat) const
{
    const int i = segmentFor(beat);
    const TempoPoint& a = points_[static_cast<size_t>(i)];
    if (!a.rampToNext || i + 1 >= static_cast<int>(points_.size())) return a.bpm;
    const TempoPoint& b = points_[static_cast<size_t>(i + 1)];
    const double x = (beat - a.beat) / (b.beat - a.beat);
    return a.bpm + (b.bpm - a.bpm) * (x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x));
}

double TempoMap::secondsAt(double beat) const
{
    if (beat <= 0.0) return 60.0 * beat / points_.front().bpm;
    const int i = segmentFor(beat);
    const TempoPoint& a = points_[static_cast<size_t>(i)];
    double s = 0.0;
    if (a.rampToNext && i + 1 < static_cast<int>(points_.size()))
        s = (points_[static_cast<size_t>(i + 1)].bpm - a.bpm) / (points_[static_cast<size_t>(i + 1)].beat - a.beat);
    return seconds_[static_cast<size_t>(i)] + segmentSeconds(a.bpm, s, beat - a.beat);
}

double TempoMap::beatAt(double seconds) const
{
    if (seconds <= 0.0) return seconds * points_.front().bpm / 60.0;
    auto it = std::upper_bound(seconds_.begin(), seconds_.end(), seconds);
    const long idx = static_cast<long>(it - seconds_.begin()) - 1;
    const int i = idx < 0 ? 0 : static_cast<int>(idx);
    const TempoPoint& a = points_[static_cast<size_t>(i)];
    double s = 0.0;
    if (a.rampToNext && i + 1 < static_cast<int>(points_.size()))
        s = (points_[static_cast<size_t>(i + 1)].bpm - a.bpm) / (points_[static_cast<size_t>(i + 1)].beat - a.beat);
    return a.beat + segmentBeats(a.bpm, s, seconds - seconds_[static_cast<size_t>(i)]);
}

} // namespace eph
