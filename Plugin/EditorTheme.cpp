/**
 * @file EditorTheme.cpp
 * @brief The palette, the families, the synths' panels and the look and feel (EditorTheme.h).
 */
#include "EditorTheme.h"
#include "eph/Engine.h"
#include <cmath>

namespace ephui {

using namespace colour;

juce::Colour familyColour(Family f)
{
    switch (f) {
    case Family::Source:   return juce::Colour(0xffe0a458);   // amber
    case Family::Filter:   return juce::Colour(0xffd9825b);   // copper
    case Family::Envelope: return juce::Colour(0xff9fbf6f);   // sage
    case Family::Motion:   return juce::Colour(0xff6fb8ae);   // teal
    default:               return juce::Colour(0xff6f8fb8);   // steel blue
    }
}

juce::Colour channelColour(int c)
{
    // The rows the sources' amber, the lead the filter's copper, the drone the room's blue, the tape keys a warm
    // sand, the strings sage, the pad synth teal, the drums a brick red, the atmosphere a dusk lavender.
    if (c < eph::kRows) return juce::Colour(0xffe0a458);
    switch (c - eph::kRows) {
    case 0: return juce::Colour(0xffd9825b);
    case 1: return juce::Colour(0xff6f8fb8);
    case 2: return juce::Colour(0xffc2a27e);
    case 3: return juce::Colour(0xff9fbf6f);
    case 4: return juce::Colour(0xff6fb8ae);
    case 5: return juce::Colour(0xffc4695a);
    default: return juce::Colour(0xff8f93c8);
    }
}

const std::vector<GroupSpec>& layoutOf(eph::Module m)
{
    using eph::Module;
    using F = Family;
    static const std::vector<GroupSpec> none;
    static const std::vector<GroupSpec> sends = { { "Sends", F::Space, { "echo", "echo2", "reverb", "blend", "early", "shimmer" } } };
    auto with = [](std::vector<GroupSpec> a, const std::vector<GroupSpec>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
    static const std::vector<GroupSpec> voice = {
        { "Oscillators", F::Source, { "wave", "detune", "pw", "drift", "drive" } },
        { "Wavetable", F::Source, { "table", "table_pos", "table_mod" } },
        { "Filter", F::Filter, { "*cutoff", "*resonance", "env_amount", "keytrack", "accent" } },
        { "Envelopes", F::Envelope, { "decay", "amp_decay" } },
        { "Glide", F::Motion, { "glide" } } };
    static const std::vector<GroupSpec> lead = with({
        { "Oscillators", F::Source, { "wave", "detune", "pw", "drift", "drive" } },
        { "Filter", F::Filter, { "*cutoff", "*resonance", "env_amount", "keytrack", "accent" } },
        { "Envelopes", F::Envelope, { "decay", "amp_decay" } },
        { "Performance", F::Motion, { "glide", "vibrato", "vibrato_rate", "auto_pan" } },
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends);
    static const std::vector<GroupSpec> tape = with({
        { "Tapes", F::Source, { "set", "*vowel", "age" } },
        { "Tone", F::Filter, { "*tone" } },
        { "Transport", F::Motion, { "wow", "flutter", "sag" } },
        { "Mix", F::Space, { "*level", "pan", "spread", "low_cut", "distance" } } }, sends);
    static const std::vector<GroupSpec> strings = with({
        { "Registers", F::Source, { "*registration", "feet", "animate", "animate_rate" } },
        { "Tone", F::Filter, { "*tone" } },
        { "Crescendo", F::Envelope, { "attack", "release" } },
        { "Ensemble", F::Motion, { "ensemble_type", "*ensemble", "phaser" } },
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends);
    static const std::vector<GroupSpec> poly = with({
        { "Wavetable", F::Source, { "table", "*position", "scan", "scan_rate" } },
        { "Oscillators", F::Source, { "detune", "spread", "drift" } },
        { "Filter", F::Filter, { "*cutoff", "*resonance", "env_amount" } },
        { "Envelope", F::Envelope, { "attack", "release" } },
        { "Ensemble", F::Motion, { "*chorus" } },
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends);
    static const std::vector<GroupSpec> atmos = {
        { "Wind", F::Source, { "*wind", "wind_tone" } },
        { "Sweeps", F::Motion, { "sweeps", "sweep_level" } },
        { "Bleeps", F::Source, { "bleeps", "bleep_level" } },
        { "Grains", F::Motion, { "grains", "grain_density" } },
        { "Mix", F::Space, { "*level", "low_cut" } },
        { "Sends", F::Space, { "echo", "reverb", "shimmer" } } };
    static const std::vector<GroupSpec> drums = {
        { "Kit", F::Source, { "*kick_hz", "decay", "tone" } },
        { "Mix", F::Space, { "*level", "low_cut" } },
        { "Sends", F::Space, { "echo", "reverb", "blend", "early" } } };
    static const std::vector<GroupSpec> echo = {
        { "Tape Echo", F::Motion, { "type", "*time", "*feedback", "tone", "drive" } },
        { "Tape", F::Motion, { "wow", "flutter" } },
        { "Return", F::Space, { "pingpong", "return", "low_cut", "duck" } } };
    static const std::vector<GroupSpec> spring = { { "Spring", F::Space, { "*decay", "tone", "return" } } };
    static const std::vector<GroupSpec> delay = {
        { "Echo 2", F::Motion, { "*time", "*feedback", "tone", "pingpong", "return", "low_cut" } } };
    static const std::vector<GroupSpec> reverb = {
        { "Hall", F::Space, { "type", "*size", "*decay", "damping", "predelay" } },
        { "Hall Return", F::Space, { "lowcut", "highcut", "return", "duck" } } };
    static const std::vector<GroupSpec> blend = {
        { "Blend Room", F::Space, { "*decay", "predelay", "damping", "low_cut", "high_cut", "return", "into_hall" } } };
    static const std::vector<GroupSpec> early = { { "Early Reflections", F::Space, { "*size", "low_cut", "high_cut", "return" } } };
    static const std::vector<GroupSpec> shimmer = { { "Shimmer", F::Space, { "*decay", "*amount", "low_cut", "high_cut", "return" } } };
    static const std::vector<GroupSpec> master = {
        { "Output", F::Space, { "*level", "compress", "ceiling", "clip" } },
        { "Stereo", F::Space, { "width", "mono" } },
        { "Low End", F::Filter, { "sub_ceiling", "sub_solo" } },
        { "Mix Bus", F::Motion, { "cascade", "tame", "motion" } } };
    static const std::vector<GroupSpec> compose = {
        { "Composer", F::Source, { "style", "key", "scale", "bpm", "style_tempo", "piece_minutes", "pick_sounds" } },
        { "Concert", F::Motion, { "concert_minutes", "night_set", "morph_to", "concert_arc", "album" } } };
    static const std::vector<GroupSpec> cue = { { "Cues", F::Space, { "enabled", "port" } } };
    static const std::vector<GroupSpec> perform = { { "Perform", F::Motion, { "*filter", "transpose", "hold", "throw" } } };
    static const std::vector<GroupSpec> row = with({
        { "Sequence", F::Source, { "active", "mode", "length", "division", "direction", "octave", "transpose" } },
        { "Change", F::Motion, { "mutation", "gate", "*sweep", "punch" } },
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends);
    static const std::vector<GroupSpec> custom = {
        { "Use", F::Source, { "use" } },
        { "Tempo and Form", F::Source, { "bpm_low", "bpm_high", "minutes_low", "minutes_high", "phases_low", "phases_high",
                                         "intro", "coda", "new_tempo", "new_key" } },
        { "Layers", F::Motion, { "peak_rows", "mutation", "tape", "strings", "lead", "bleeps", "drums", "lead_density" } },
        { "Hands and Sound", F::Envelope, { "hand_move", "hand_rest", "darkness", "hall", "level" } } };
    switch (m) {
    case Module::Voice: return voice;
    case Module::Lead: case Module::Drone: return lead;
    case Module::Tape: return tape;
    case Module::Strings: return strings;
    case Module::Poly: return poly;
    case Module::Atmos: return atmos;
    case Module::Drums: return drums;
    case Module::Echo: return echo;
    case Module::Spring: return spring;
    case Module::Echo2: return delay;
    case Module::Reverb: return reverb;
    case Module::Blend: return blend;
    case Module::Early: return early;
    case Module::Shimmer: return shimmer;
    case Module::Master: return master;
    case Module::Compose: return compose;
    case Module::Cue: return cue;
    case Module::Perform: return perform;
    case Module::Row: return row;
    case Module::Custom: return custom;
    default: return none;
    }
}

// ---------------------------------------------------------------------------------------------------------------------

LookAndFeel::LookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::Label::textColourId, ink);
    setColour(juce::Slider::textBoxTextColourId, dim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, amber.withAlpha(0.35f));
    setColour(juce::Slider::rotarySliderFillColourId, amber);
    setColour(juce::Slider::thumbColourId, amber);
    setColour(juce::Slider::trackColourId, amber.withAlpha(0.6f));
    setColour(juce::Slider::backgroundColourId, faint);
    setColour(juce::ComboBox::backgroundColourId, raised);
    setColour(juce::ComboBox::outlineColourId, edge);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::arrowColourId, dim);
    setColour(juce::PopupMenu::backgroundColourId, group);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::PopupMenu::headerTextColourId, amber);
    setColour(juce::TextButton::buttonColourId, raised);
    setColour(juce::TextButton::buttonOnColourId, amber.withAlpha(0.35f));
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, ink);
    setColour(juce::ToggleButton::textColourId, dim);
    setColour(juce::ToggleButton::tickColourId, amber);
    setColour(juce::TextEditor::backgroundColourId, raised);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::outlineColourId, edge);
    setColour(juce::TabbedComponent::backgroundColourId, panel);
    setColour(juce::TabbedComponent::outlineColourId, edge);
    setColour(juce::ScrollBar::thumbColourId, faint);
    setColour(juce::TooltipWindow::backgroundColourId, group);
    setColour(juce::TooltipWindow::textColourId, ink);
    setColour(juce::TooltipWindow::outlineColourId, edge);
    setColour(juce::AlertWindow::backgroundColourId, group);
    setColour(juce::AlertWindow::textColourId, ink);
    setColour(juce::AlertWindow::outlineColourId, edge);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float startAngle,
                                   float endAngle, juce::Slider& slider)
{
    // A knob as on an instrument's panel: a dark cap with its pointer, and around it the value as an arc in the colour
    // of its family -- from the top where the parameter spans zero (pan, transposition), from the start otherwise.
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const float size = std::min(bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float ring = size * 0.5f - 2.0f, track = std::max(2.5f, size * 0.07f);
    const juce::Colour colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const float angle = startAngle + pos * (endAngle - startAngle);
    float from = startAngle;
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        from = startAngle + static_cast<float>(slider.valueToProportionOfLength(0.0)) * (endAngle - startAngle);
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, ring - track * 0.5f, ring - track * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour(faint.withAlpha(0.55f));
    g.strokePath(arc, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path value;
    value.addCentredArc(c.x, c.y, ring - track * 0.5f, ring - track * 0.5f, 0.0f, std::min(from, angle), std::max(from, angle), true);
    g.setColour(slider.isEnabled() ? colour : faint);
    g.strokePath(value, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // The cap, lit a little from above, with a hairline rim.
    const float cap = ring - track - std::max(2.0f, size * 0.05f);
    g.setGradientFill(juce::ColourGradient(raised.brighter(0.25f), c.x, c.y - cap, raised.darker(0.35f), c.x, c.y + cap, false));
    g.fillEllipse(c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap);
    g.setColour(edge.brighter(0.2f));
    g.drawEllipse(c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap, 1.0f);
    // The pointer.
    const float s = std::sin(angle), co = -std::cos(angle);
    g.setColour(ink);
    g.drawLine(c.x + s * cap * 0.25f, c.y + co * cap * 0.25f, c.x + s * cap * 0.85f, c.y + co * cap * 0.85f, std::max(1.5f, size * 0.035f));
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, pos, 0.0f, 0.0f, style, slider);
        return;
    }
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    g.setColour(faint);
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(width), 4.0f, 2.0f);
    g.setColour(slider.findColour(juce::Slider::trackColourId));
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, pos - static_cast<float>(x), 4.0f, 2.0f);
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.fillEllipse(pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // A small switch with its lamp: lit in the button's colour when on.
    const auto r = b.getLocalBounds().toFloat();
    const float h = std::min(16.0f, r.getHeight() - 4.0f);
    const juce::Rectangle<float> box(r.getX() + 2.0f, r.getCentreY() - h * 0.5f, h * 1.7f, h);
    const bool on = b.getToggleState();
    const juce::Colour colour = b.findColour(juce::ToggleButton::tickColourId);
    g.setColour(on ? colour.withAlpha(0.35f) : raised);
    g.fillRoundedRectangle(box, h * 0.5f);
    g.setColour(highlighted ? edge.brighter(0.4f) : edge);
    g.drawRoundedRectangle(box, h * 0.5f, 1.0f);
    const float d = h - 6.0f;
    g.setColour(on ? colour : dim);
    g.fillEllipse(on ? box.getRight() - d - 3.0f : box.getX() + 3.0f, box.getY() + 3.0f, d, d);
    if (b.getButtonText().isNotEmpty()) {
        g.setColour(on ? ink : dim);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(b.getButtonText(), box.getRight() + 6.0f > r.getRight() ? r.toNearestInt() : r.withLeft(box.getRight() + 6.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);
    }
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(box.hasKeyboardFocus(true) || box.isMouseOver(true) ? edge.brighter(0.4f) : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    const float ax = static_cast<float>(width) - 14.0f, ay = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    juce::Colour fill = b.getToggleState() ? b.findColour(juce::TextButton::buttonOnColourId) : background;
    if (down) fill = fill.brighter(0.15f);
    else if (highlighted) fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(highlighted ? edge.brighter(0.45f) : edge);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

void LookAndFeel::drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool isMouseOver, bool)
{
    // Flat tabs: the name, the page in front lit amber and underlined.
    const auto r = b.getLocalBounds().toFloat();
    const bool front = b.isFrontTab();
    if (front) {
        g.setColour(panel);
        g.fillRect(r);
        g.setColour(amber);
        g.fillRect(r.getX() + 6.0f, r.getBottom() - 2.5f, r.getWidth() - 12.0f, 2.5f);
    } else if (isMouseOver) {
        g.setColour(group);
        g.fillRect(r);
    }
    g.setColour(front ? amber : (isMouseOver ? ink : dim));
    g.setFont(getTabButtonFont(b, r.getHeight()));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.setColour(bg);
    g.fillRect(bar.getLocalBounds());
    g.setColour(edge);
    g.fillRect(0, bar.getHeight() - 1, bar.getWidth(), 1);
}

void LookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

int LookAndFeel::getTabButtonBestWidth(juce::TabBarButton& b, int tabDepth)
{
    return juce::GlyphArrangement::getStringWidthInt(getTabButtonFont(b, static_cast<float>(tabDepth)), b.getButtonText()) + 22;
}

juce::Font LookAndFeel::getTabButtonFont(juce::TabBarButton&, float) { return juce::FontOptions(14.0f); }
juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return juce::FontOptions(13.5f); }
juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int) { return juce::FontOptions(13.5f); }
juce::Font LookAndFeel::getPopupMenuFont() { return juce::FontOptions(14.0f); }

juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(slider);
    l->setFont(juce::FontOptions(12.0f));
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::textColourId, dim);
    return l;
}

} // namespace ephui
