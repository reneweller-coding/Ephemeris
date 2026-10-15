/**
 * @file EditorStyle.h
 * @brief The style page (PLAN 8.1): the five style profiles side by side, and a style of the user's own.
 *
 * A style is not a sound but a set of ranges and chances the composer draws from (Style.h): the tempo, the
 * length and number of phases, how many rows play at the peak, how often a layer comes in, how fast the hands
 * move, how dark and how wide the room. The table shows the five built-in profiles and the user's own in one
 * view, the one in use lit. On the right, the user's own style (module custom): "Copy ... into Custom" takes
 * the chosen style's numbers as a starting point, and "Use Custom Style" makes the composer take them instead
 * (the counter rows' lengths, the transposer, the tape set and the drum patterns still come from the chosen style). The tempi and
 * levels of the built-in profiles come from measuring reference recordings (Tools/analyze_ref.py).
 * @author Rene Weller
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class ParamPage;

/** @brief The style profiles as a table, and the user's own style's controls. */
class StylePage final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief Shows the profiles; @p custom is the custom module's parameter page, which the style page owns.
     * @param[in,out] p       the processor
     * @param[in]     custom  the custom module's parameter page
     */
    StylePage( EphemerisProcessor &p, std::unique_ptr<ParamPage> custom );
    ~StylePage() override;   ///< stops the timer
    /**
     * the table
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    void resized() override;   ///< the table on the left, the custom style on the right

private:
    void timerCallback() override;   ///< follows compose.style and the custom style
    /** @brief Writes the chosen style's numbers into the custom module, through the host parameters. */
    void copyStyle();
    EphemerisProcessor        &m_proc;                ///< where the chosen style comes from
    std::unique_ptr<ParamPage> m_custom;              ///< the custom module's controls
    juce::Viewport             m_view;                ///< scrolls the custom controls where they do not fit
    juce::TextButton           m_copy;                ///< "Copy \<style\> into Custom"
    juce::Rectangle<int>       m_table;               ///< where the table is drawn
    int                        m_style = -1;          ///< the style lit
    float                      m_customHash = 0.0f;   ///< a sum of the custom values, to repaint when they move
};
