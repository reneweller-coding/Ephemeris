/**
 * @file Score.cpp
 * @brief Gesture curves and score housekeeping.
 * @author Rene Weller
 */
#include "eph/Score.h"
#include <algorithm>

namespace eph
{

const char *const PartNames[NumParts] = {
    "row1", "row2", "row3",    "row4", "row5",  "row6",  "row7",  "row8",
    "lead", "tape", "strings", "pad",  "drone", "atmos", "drums",
};

double gestureShape( GestureShapeE shape, double t )
{
    t = t < 0.0 ? 0.0 : ( t > 1.0 ? 1.0 : t );
    switch ( shape )
    {
    case GestureShapeE::MINIMUM_JERK: return t * t * t * ( 10.0 + t * ( -15.0 + 6.0 * t ) );
    case GestureShapeE::LINEAR: return t;
    case GestureShapeE::EASE_IN: return t * t;
    case GestureShapeE::EASE_OUT: return 1.0 - ( 1.0 - t ) * ( 1.0 - t );
    case GestureShapeE::STEP: return 1.0;
    }
    return t;
}

float gestureValue( const Gesture &g, double beat )
{
    if ( beat < g.m_beat ) return g.m_from;
    if ( g.m_length <= 0.0 || g.m_shape == GestureShapeE::STEP || beat >= g.m_beat + g.m_length ) return g.m_to;
    const double p = gestureShape( g.m_shape, ( beat - g.m_beat ) / g.m_length );
    return static_cast<float>( static_cast<double>( g.m_from ) +
                               p * ( static_cast<double>( g.m_to ) - static_cast<double>( g.m_from ) ) );
}

void Score::sort()
{
    std::stable_sort( m_notes.begin(), m_notes.end(),
                      []( const NoteEvent &a, const NoteEvent &b ) { return a.m_beat < b.m_beat; } );
    std::stable_sort( m_gestures.begin(), m_gestures.end(),
                      []( const Gesture &a, const Gesture &b ) { return a.m_beat < b.m_beat; } );
    std::stable_sort( m_rack.begin(), m_rack.end(),
                      []( const RackEvent &a, const RackEvent &b ) { return a.m_beat < b.m_beat; } );
    std::stable_sort( m_markers.begin(), m_markers.end(),
                      []( const Marker &a, const Marker &b ) { return a.m_beat < b.m_beat; } );
    std::stable_sort( m_knobs.begin(), m_knobs.end(),
                      []( const KnobSet &a, const KnobSet &b ) { return a.m_beat < b.m_beat; } );
    std::stable_sort( m_levels.begin(), m_levels.end(),
                      []( const LevelMark &a, const LevelMark &b ) { return a.m_beat < b.m_beat; } );
}

void Score::clear( double bpm )
{
    m_tempo.setConstant( bpm );
    m_lengthBeats = 0.0;
    m_notes.clear();
    m_gestures.clear();
    m_rack.clear();
    m_markers.clear();
    m_rootShifts.clear();
    m_scaleShifts.clear();
    m_rowShapes.clear();
    m_sounds.clear();
    m_knobs.clear();
    m_levels.clear();
}

int rootShiftAt( const std::vector<std::pair<double, int>> &shifts, double beat )
{
    int s = 0;
    for ( const auto &e : shifts )
    {
        if ( e.first > beat ) break;
        s = e.second;
    }
    return s;
}

float Score::gestureOffset( int param, double beat ) const
{
    const Gesture *latest = nullptr;
    for ( const Gesture &g : m_gestures )
    {
        if ( g.m_param != param || g.m_beat > beat ) continue;
        if ( latest == nullptr || g.m_beat >= latest->m_beat ) latest = &g;
    }
    return latest == nullptr ? 0.0f : gestureValue( *latest, beat );
}

}   // namespace eph
