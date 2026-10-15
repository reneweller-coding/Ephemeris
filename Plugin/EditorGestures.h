/**
 * @file EditorGestures.h
 * @brief The gestures page (PLAN 8.1): what the player's two hands do to the knobs over the whole piece.
 *
 * One lane per knob the composer's hands move (GestureEngine.h): the offset from the knob as a curve over
 * the piece, from -1 to +1 of the knob's range, coloured by the hand that moves it. On top, a bar per hand
 * where it is busy -- the two-hands rule of PLAN 6.4 made visible: never more than two gestures at once.
 * The sections of the piece lie behind the lanes, the playhead runs over them, and a click jumps there.
 * Everything comes from the score the engine plays, so it shows what sounds.
 * @author Rene Weller
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

/** @brief The gesture lanes of the score that plays. */
class GestureView final : public juce::Component, private juce::Timer
{
public:
    /**
     * shows @p p's score and follows its playhead
     * @param[in,out] p  the processor
     */
    explicit GestureView( EphemerisProcessor &p );
    ~GestureView() override;   ///< stops the timer
    /**
     * the lanes, the hands, the sections, the playhead
     * @param[in,out] g  the graphics context
     */
    void paint( juce::Graphics &g ) override;
    void resized() override;   ///< the curves are drawn again at the new size
    /**
     * jumps to the clicked beat
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override;

private:
    /** @brief One knob's gestures. */
    struct Lane
    {
        int                       m_param = -1;   ///< the knob
        juce::String              m_name;         ///< its key ("voice1.cutoff")
        std::vector<eph::Gesture> m_gestures;     ///< in time order
        int                       m_hand = 0;     ///< the hand of its first gesture (for the colour)
    };
    /** @brief Follows the playhead and a new score. */
    void timerCallback() override;
    void rebuild();   ///< lanes and curves from a new score or size
    /**
     * the lanes' horizontal position of @p beat
     * @param[in] beat  the position in beats
     * @return its x in pixels
     */
    float xOf( double beat ) const;

    EphemerisProcessor     &m_proc;           ///< where the score and the playhead come from
    eph::Score              m_score;          ///< a copy of the score the engine plays
    int                     m_version = -1;   ///< which one (EphemerisProcessor::scoreVersion)
    std::vector<Lane>       m_lanes;          ///< one per gestured knob
    std::vector<juce::Path> m_curves;         ///< the lanes' curves at the current size
    juce::Rectangle<float>  m_area;           ///< where the lanes are drawn
};
