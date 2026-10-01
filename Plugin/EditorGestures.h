/**
 * @file EditorGestures.h
 * @brief The gestures page (PLAN 8.1): what the player's two hands do to the knobs over the whole piece.
 *
 * One lane per knob the composer's hands move (GestureEngine.h): the offset from the knob as a curve over
 * the piece, from -1 to +1 of the knob's range, coloured by the hand that moves it. On top, a bar per hand
 * where it is busy -- the two-hands rule of PLAN 6.4 made visible: never more than two gestures at once.
 * The sections of the piece lie behind the lanes, the playhead runs over them, and a click jumps there.
 * Everything comes from the score the engine plays, so it shows what sounds.
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

/** @brief The gesture lanes of the score that plays. */
class GestureView final : public juce::Component, private juce::Timer {
public:
    explicit GestureView(EphemerisProcessor& p);   ///< shows @p p's score and follows its playhead
    ~GestureView() override;                       ///< stops the timer
    void paint(juce::Graphics& g) override;        ///< the lanes, the hands, the sections, the playhead
    void resized() override;                       ///< the curves are drawn again at the new size
    void mouseDown(const juce::MouseEvent& e) override;   ///< jumps to the clicked beat

private:
    /** @brief One knob's gestures. */
    struct Lane {
        int param = -1;                    ///< the knob
        juce::String name;                 ///< its key ("voice1.cutoff")
        std::vector<eph::Gesture> gestures;   ///< in time order
        int hand = 0;                      ///< the hand of its first gesture (for the colour)
    };
    /** @brief Follows the playhead and a new score. */
    void timerCallback() override;
    void rebuild();                        ///< lanes and curves from a new score or size
    float xOf(double beat) const;          ///< the lanes' horizontal position of @p beat

    EphemerisProcessor& proc_;             ///< where the score and the playhead come from
    eph::Score score_;                     ///< a copy of the score the engine plays
    int version_ = -1;                     ///< which one (EphemerisProcessor::scoreVersion)
    std::vector<Lane> lanes_;              ///< one per gestured knob
    std::vector<juce::Path> curves_;       ///< the lanes' curves at the current size
    juce::Rectangle<float> area_;          ///< where the lanes are drawn
};
