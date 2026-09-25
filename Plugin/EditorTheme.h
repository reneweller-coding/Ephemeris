/**
 * @file EditorTheme.h
 * @brief The editor's look (26.09.2026): one palette, the function families, the knob, and how every synth's
 *        controls fall into groups.
 *
 * **The palette** is the logo's. A night studio -- a deep midnight ground, warm parchment for the text -- and the
 * orrery's five planets as the five families of function, the same on every page, as the colour-coded panels of
 * the modular systems the style was made on: amber for the sources (the oscillators, the tapes, the registers),
 * copper for the filters, sage for the envelopes, teal for everything that moves by itself (the LFOs, the scans,
 * the ensembles, the echoes' time), steel blue for the room and the mix (levels, sends, halls). The sun's amber is
 * the accent: the title, the page in front, the transport.
 *
 * **The groups.** A synth page is not a grid of every knob of its table but the panel of an instrument: its
 * controls in titled groups (Oscillators, Filter, Envelopes, Mix, Sends ...), the ones a hand goes to first --
 * the cutoff, the resonance, the string machine's registration and ensemble as on a Streichfett, the pad synth's
 * place in its table -- as large encoders. layoutOf() says which; a parameter it does not name lands in a group
 * "More" at the end, so nothing added to a table is ever lost from the editor.
 */
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "eph/Params.h"
#include <vector>

namespace ephui {

/** @brief The palette. */
namespace colour {
const juce::Colour bg        { 0xff0e1117 };   ///< the window: midnight
const juce::Colour panel     { 0xff131820 };   ///< a page
const juce::Colour group     { 0xff1a2029 };   ///< a group box
const juce::Colour raised    { 0xff222a36 };   ///< buttons, menus, the knobs' bodies
const juce::Colour edge      { 0xff2c3544 };   ///< hairlines
const juce::Colour ink       { 0xffe8dfcc };   ///< text: parchment
const juce::Colour dim       { 0xff8e949f };   ///< names, secondary text
const juce::Colour faint     { 0xff4a5262 };   ///< tracks, axes, the off state
const juce::Colour amber     { 0xffe8a948 };   ///< the sun: the accent
const juce::Colour green     { 0xff7fd49a };   ///< a meter in range
const juce::Colour red       { 0xffe06a5f };   ///< a meter over, mute
} // namespace colour

/** @brief The families of function, the logo's planets. */
enum class Family { Source, Filter, Envelope, Motion, Space };
/** @brief The colour of a family. */
juce::Colour familyColour(Family f);
/** @brief The colour of a mixer channel (Engine channel order), from the families of what plays on it. */
juce::Colour channelColour(int channel);

/** @brief One group of a synth's panel: its title, its family and its parameters by key ("*cutoff": a large one). */
struct GroupSpec {
    const char* title;
    Family family;
    std::vector<const char*> keys;
};
/** @brief The panel of a module: its groups, in order (empty: one group of everything). */
const std::vector<GroupSpec>& layoutOf(eph::Module m);

/** @brief The editor's look and feel: the knob, the switches, the menus, the tabs, the buttons. */
class LookAndFeel final : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    juce::Font getTabButtonFont(juce::TabBarButton&, float height) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
};

} // namespace ephui
