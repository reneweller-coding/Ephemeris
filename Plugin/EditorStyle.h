/**
 * @file EditorStyle.h
 * @brief The style page (PLAN 8.1): the five style profiles side by side.
 *
 * A style is not a sound but a set of ranges and chances the composer draws from (Style.h): the tempo, the
 * length and number of phases, how many rows play at the peak and how long they are, how often a layer
 * comes in, how fast the hands move, how dark and how wide the room. This page shows them all in one table,
 * the chosen style's column lit, so that what makes a Cosmic piece different from a Drift one can be read
 * rather than guessed. The profiles are fixed in the program; their tempi and levels come from measuring
 * reference recordings (Tools/analyze_ref.py).
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** @brief The style profiles as a table. */
class StylePage final : public juce::Component, private juce::Timer {
public:
    explicit StylePage(EphemerisProcessor& p);   ///< shows the profiles, the chosen one lit
    ~StylePage() override;                       ///< stops the timer
    void paint(juce::Graphics& g) override;      ///< the table

private:
    void timerCallback() override;               ///< follows compose.style
    EphemerisProcessor& proc_;                   ///< where the chosen style comes from
    int style_ = -1;                             ///< the style lit
};
