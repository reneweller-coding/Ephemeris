/**
 * @file Clock.h
 * @brief Musical time: note divisions and the tempo map of a set.
 *
 * A psytrance set moves within a few BPM, often rising slowly, and a change of style profile ramps
 * the tempo over 16 to 32 bars. The tempo map is a list of points in beats; between two points the
 * tempo is either held or ramped linearly *in beats*. Time in seconds is the integral of 60/bpm over
 * the beats, which for a linear ramp has a closed form, so beats and seconds convert exactly in both
 * directions without accumulating error over a three-hour set.
 *
 * @note SyncDiv copied from Noctuary `Core/include/ambient/Clock.h` at b60a2fe (15.09.2026); the
 *       tempo map is new.
 * @note Copied from Phosphene `Core/include/phos/Clock.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include <vector>

namespace eph {

/** @brief Note divisions in quarter-note beats (4/4). */
enum class SyncDivE : int {
    FREE = 0, BARS64, BARS32, BARS16, BARS8, BARS4, BARS2, BAR1,
    HALF, HALF_T, QUARTER, QUARTER_D, QUARTER_T, EIGHTH, EIGHTH_D, EIGHTH_T, SIXTEENTH, SIXTEENTH_T, THIRTY_SECOND,
    COUNT
};
constexpr int NumSyncDivs = static_cast<int>(SyncDivE::COUNT);   ///< number of divisions
extern const char* const SyncDivNames[NumSyncDivs];              ///< display names
extern const double      SyncBeats[NumSyncDivs];                 ///< length in beats, 0 for Free

/** @brief Beats per bar; Ephemeris is 4/4 throughout; the rows of the rack are polymetric inside it. */
constexpr int BeatsPerBar = 4;

/** @brief One point of a tempo map. */
struct TempoPoint {
    double m_beat = 0.0;         ///< position in beats
    double m_bpm = 145.0;        ///< tempo at that position
    bool   m_rampToNext = false; ///< ramp linearly (in beats) to the next point instead of holding
};

/**
 * @brief Piecewise constant or linear tempo over beats, with exact beat/second conversion.
 *
 * Always holds at least one point at beat 0. Before the first and after the last point the tempo
 * of that point is held.
 */
class TempoMap {
public:
    TempoMap() { setConstant(145.0); }

    /** @brief One tempo for everything. */
    void setConstant(double bpm);
    /**
     * @brief Adds or replaces a point.
     * @param beat       position (>= 0); a point at the same beat is replaced
     * @param bpm        tempo, clamped to 20..400
     * @param rampToNext whether the tempo ramps from here to the next point
     */
    void add(double beat, double bpm, bool rampToNext);
    /** @brief The points, sorted by beat. */
    const std::vector<TempoPoint>& points() const { return m_points; }

    /** @brief Tempo at @p beat. */
    double bpmAt(double beat) const;
    /** @brief Seconds from beat 0 to @p beat. */
    double secondsAt(double beat) const;
    /** @brief Beat reached after @p seconds (inverse of secondsAt). */
    double beatAt(double seconds) const;

private:
    /** @brief Computes m_seconds from the points. */
    void rebuild();
    /** @brief The index of the point whose segment holds @p beat. */
    int segmentFor(double beat) const;
    std::vector<TempoPoint> m_points;   ///< the tempo points, by beat
    std::vector<double> m_seconds;   ///< seconds at each point
};

} // namespace eph
