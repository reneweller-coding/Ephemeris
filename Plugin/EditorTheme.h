/**
 * @file EditorTheme.h
 * @brief The editor's look (26.09.2026): Ephemeris' skin of the shared frame (Frame.h, 01.10.2026), the function
 *        families, and how every synth's controls fall into groups.
 *
 * **The skin** (01.10.2026, the GUIs unified): an astronomer's plate at night -- the name in a serif as engraved on an
 * old ephemeris, the sun's amber as the accent, and behind the panel an orrery among the constellations
 * (Resources/backdrop.jpg); the knobs, menus, tabs and every way of working them are the frame's, as in the sibling
 * generators.
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
#include "Frame.h"
#include "eph/Params.h"
#include <vector>

namespace ephui {

/** @brief The palette. */
namespace colour {
const juce::Colour bg        { 0xff0b0e14 };   ///< the window: midnight
const juce::Colour panel     { 0xa0131820 };   ///< a page (the orrery shows faintly through it)
const juce::Colour group     { 0xff1a2029 };   ///< a group box
const juce::Colour raised    { 0xff222a36 };   ///< buttons, menus, the knobs' bodies
const juce::Colour edge      { 0xff2c3544 };   ///< hairlines
const juce::Colour ink       { 0xffe8dfcc };   ///< text: parchment
const juce::Colour dim       { 0xff8e949f };   ///< names, secondary text
const juce::Colour faint     { 0xff4a5262 };   ///< tracks, axes, the off state
const juce::Colour amber     { 0xffe8a948 };   ///< the sun: the accent
const juce::Colour onset     { 0xffe06a5f };   ///< the playhead's red, a mute
const juce::Colour green     { 0xff7fd49a };   ///< a meter in range
const juce::Colour red       { 0xffe06a5f };   ///< a meter over, mute
} // namespace colour

/** @brief The families of function, the logo's planets (the frame's, the same in every generator). */
using Family = frame::Family;
/** @brief Ephemeris' skin of the frame: the palette above, the orrery behind it, the logo. */
const frame::Skin& skin();
/** @brief The colour of a family. */
juce::Colour familyColour(Family f);
/** @brief The colour of a mixer channel (Engine channel order), from the families of what plays on it. */
juce::Colour channelColour(int channel);

/**
 * @brief One group of a synth's panel: its title, its family and its parameters by key ("*cutoff": a large one,
 *        "~lfo1_shape": a narrow menu, "@mod1": a slot of the modulation matrix).
 */
struct GroupSpec {
    const char* title;
    Family family;
    std::vector<const char*> keys;
};
/** @brief The panel of a module: its groups, in order (empty: one group of everything). */
const std::vector<GroupSpec>& layoutOf(eph::Module m);

} // namespace ephui
