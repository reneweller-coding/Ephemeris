/**
 * @file EditorPerform.h
 * @brief The perform page (PLAN 8.1): the performer's controls and their MIDI bindings.
 *
 * On top the perform module's controls (Params.h, perform): the filter the hand grabs, the transposition
 * key, the hold, the echo throw. Below, for each, the MIDI controller bound to it and a Learn button:
 * pressed, the next controller that moves is bound (EphemerisProcessor::learn). A line says what a
 * keyboard does without any binding: its keys transpose the rows from middle C. While a headset sends its hands (or
 * the settings say always), the frame's headset box beside them: the hands as they stand and what they do.
 * @author Rene Weller
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class ParamPage;

/** @brief The perform tab. */
class PerformPage final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief Builds the controls for @p p; @p params is the perform module's page, which this page owns.
     * @param[in,out] p       the processor
     * @param[in]     params  the perform module's page
     */
    PerformPage( EphemerisProcessor &p, std::unique_ptr<ParamPage> params );
    ~PerformPage() override;   ///< stops the timer
    void resized() override;   ///< the controls on top, the bindings below
    /**
     * the note on the keys
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;

private:
    void timerCallback() override;                                ///< the bindings and the learn state as they are
    EphemerisProcessor                 &m_proc;                   ///< the processor
    std::unique_ptr<ParamPage>          m_params;                 ///< the perform module's controls
    int                                 m_controlsHeight = 150;   ///< the height the controls took at the last layout
    juce::OwnedArray<juce::Label>       m_bindings;               ///< "Filter: CC 1", one per control
    juce::OwnedArray<frame::IconButton> m_learn;             ///< one per control: a MIDI socket, lit while it learns
    std::vector<int>                    m_ids;               ///< the store ids of the controls
    bool                                m_headset = false;   ///< the headset's box is shown (the frame)
    juce::Rectangle<int>                m_headsetArea;       ///< where it is drawn
};
