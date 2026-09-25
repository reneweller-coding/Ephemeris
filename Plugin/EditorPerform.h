/**
 * @file EditorPerform.h
 * @brief The perform page (PLAN 8.1): the performer's controls and their MIDI bindings.
 *
 * On top the perform module's controls (Params.h, perform): the filter the hand grabs, the transposition
 * key, the hold, the echo throw. Below, for each, the MIDI controller bound to it and a Learn button:
 * pressed, the next controller that moves is bound (EphemerisProcessor::learn). A line says what a
 * keyboard does without any binding: its keys transpose the rows from middle C.
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class ParamPage;

/** @brief The perform tab. */
class PerformPage final : public juce::Component, private juce::Timer {
public:
    /** @brief Builds the controls for @p p; @p params is the perform module's page, which this page owns. */
    PerformPage(EphemerisProcessor& p, std::unique_ptr<ParamPage> params);
    ~PerformPage() override;                  ///< stops the timer
    void resized() override;                  ///< the controls on top, the bindings below
    void paint(juce::Graphics& g) override;   ///< the note on the keys

private:
    void timerCallback() override;            ///< the bindings and the learn state as they are
    EphemerisProcessor& proc_;                ///< the processor
    std::unique_ptr<ParamPage> params_;       ///< the perform module's controls
    juce::OwnedArray<juce::Label> bindings_;  ///< "Filter: CC 1", one per control
    juce::OwnedArray<juce::TextButton> learn_;   ///< one per control
    std::vector<int> ids_;                    ///< the store ids of the controls
};
