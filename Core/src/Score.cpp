/**
 * @file Score.cpp
 * @brief Gesture curves and score housekeeping.
 */
#include "eph/Score.h"
#include <algorithm>

namespace eph {

const char* const kPartNames[kNumParts] = {
    "row1", "row2", "row3", "row4", "row5", "row6", "row7", "row8",
    "lead", "tape", "strings", "pad", "drone", "atmos", "drums",
};

double gestureShape(GestureShape shape, double t)
{
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    switch (shape) {
    case GestureShape::MinimumJerk: return t * t * t * (10.0 + t * (-15.0 + 6.0 * t));
    case GestureShape::Linear:      return t;
    case GestureShape::EaseIn:      return t * t;
    case GestureShape::EaseOut:     return 1.0 - (1.0 - t) * (1.0 - t);
    case GestureShape::Step:        return 1.0;
    }
    return t;
}

float gestureValue(const Gesture& g, double beat)
{
    if (beat < g.beat) return g.from;
    if (g.length <= 0.0 || g.shape == GestureShape::Step || beat >= g.beat + g.length) return g.to;
    const double p = gestureShape(g.shape, (beat - g.beat) / g.length);
    return static_cast<float>(static_cast<double>(g.from) + p * (static_cast<double>(g.to) - static_cast<double>(g.from)));
}

void Score::sort()
{
    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.beat < b.beat; });
    std::stable_sort(gestures.begin(), gestures.end(), [](const Gesture& a, const Gesture& b) { return a.beat < b.beat; });
    std::stable_sort(rack.begin(), rack.end(), [](const RackEvent& a, const RackEvent& b) { return a.beat < b.beat; });
    std::stable_sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; });
}

void Score::clear(double bpm)
{
    tempo.setConstant(bpm);
    lengthBeats = 0.0;
    notes.clear();
    gestures.clear();
    rack.clear();
    markers.clear();
    rootShifts.clear();
    scaleShifts.clear();
    rowShapes.clear();
}

int rootShiftAt(const std::vector<std::pair<double, int>>& shifts, double beat)
{
    int s = 0;
    for (const auto& e : shifts) {
        if (e.first > beat) break;
        s = e.second;
    }
    return s;
}

float Score::gestureOffset(int param, double beat) const
{
    const Gesture* latest = nullptr;
    for (const Gesture& g : gestures) {
        if (g.param != param || g.beat > beat) continue;
        if (latest == nullptr || g.beat >= latest->beat) latest = &g;
    }
    return latest == nullptr ? 0.0f : gestureValue(*latest, beat);
}

} // namespace eph
