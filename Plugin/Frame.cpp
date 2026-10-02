/**
 * @file Frame.cpp
 * @brief The frame the generators share (Frame.h).
 */
#include "Frame.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <array>
#include <algorithm>
#include <map>

namespace frame {

namespace {

/** @brief The skin-less default font at @p height, bold or not (the help and the menus). */
juce::Font font(float height, bool bold = false) { return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain)); }

/** @brief The headset ports: each instrument its own, so two can listen side by side. */
int portOf(const juce::String& app)
{
    if (app == "Noctuary") return 9000;   // its OSC port since 2.0, which the Quest app's bridge already sends to
    if (app == "Phosphene") return 9101;
    if (app == "Ephemeris") return 9102;
    if (app == "Totality") return 9103;
    if (app == "Parhelion") return 9104;
    return 9100;
}

} // namespace

// ================================================================================================ the look and feel

LookAndFeel::LookAndFeel(const Skin& skin) : skin_(skin)
{
    const Skin& s = skin_;
    setColour(juce::ResizableWindow::backgroundColourId, s.bg);
    setColour(juce::Label::textColourId, s.ink);
    setColour(juce::Slider::textBoxTextColourId, s.dim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, s.accent.withAlpha(0.35f));
    setColour(juce::Slider::rotarySliderFillColourId, s.accent);
    setColour(juce::Slider::thumbColourId, s.accent);
    setColour(juce::Slider::trackColourId, s.accent.withAlpha(0.6f));
    setColour(juce::Slider::backgroundColourId, s.faint);
    setColour(juce::ComboBox::backgroundColourId, s.raised);
    setColour(juce::ComboBox::outlineColourId, s.edge);
    setColour(juce::ComboBox::textColourId, s.ink);
    setColour(juce::ComboBox::arrowColourId, s.dim);
    setColour(juce::PopupMenu::backgroundColourId, s.group);
    setColour(juce::PopupMenu::textColourId, s.ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, s.accent.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, s.ink);
    setColour(juce::PopupMenu::headerTextColourId, s.accent);
    setColour(juce::TextButton::buttonColourId, s.raised);
    setColour(juce::TextButton::buttonOnColourId, s.accent.withAlpha(0.35f));
    setColour(juce::TextButton::textColourOffId, s.ink);
    setColour(juce::TextButton::textColourOnId, s.ink);
    setColour(juce::ToggleButton::textColourId, s.dim);
    setColour(juce::ToggleButton::tickColourId, s.accent);
    setColour(juce::TextEditor::backgroundColourId, s.raised);
    setColour(juce::TextEditor::textColourId, s.ink);
    setColour(juce::TextEditor::outlineColourId, s.edge);
    setColour(juce::TextEditor::focusedOutlineColourId, s.accent.withAlpha(0.6f));
    setColour(juce::TextEditor::highlightColourId, s.accent.withAlpha(0.3f));
    setColour(juce::CaretComponent::caretColourId, s.accent);
    setColour(juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedComponent::outlineColourId, s.edge);
    setColour(juce::ScrollBar::thumbColourId, s.faint);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ListBox::textColourId, s.ink);
    setColour(juce::TooltipWindow::backgroundColourId, s.group);
    setColour(juce::TooltipWindow::textColourId, s.ink);
    setColour(juce::TooltipWindow::outlineColourId, s.edge);
    setColour(juce::AlertWindow::backgroundColourId, s.group);
    setColour(juce::AlertWindow::textColourId, s.ink);
    setColour(juce::AlertWindow::outlineColourId, s.edge);
    setColour(juce::HyperlinkButton::textColourId, s.accent);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float startAngle,
                                   float endAngle, juce::Slider& slider)
{
    // A knob as on an instrument's panel: a dark cap with its pointer, and around it the value as an arc in the colour
    // of its family -- from the top where the parameter spans zero (pan, transposition), from the start otherwise.
    const Skin& s = skin_;
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const float size = std::min(bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float ring = size * 0.5f - 3.0f, track = std::max(2.5f, size * 0.07f);
    const juce::Colour colour = slider.isEnabled() ? slider.findColour(juce::Slider::rotarySliderFillColourId) : s.faint;
    const float angle = startAngle + pos * (endAngle - startAngle);
    float from = startAngle;
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        from = startAngle + static_cast<float>(slider.valueToProportionOfLength(0.0)) * (endAngle - startAngle);
    const float r = ring - track * 0.5f;
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour(s.faint.withAlpha(0.55f));
    g.strokePath(arc, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path value;
    value.addCentredArc(c.x, c.y, r, r, 0.0f, std::min(from, angle), std::max(from, angle), true);
    if (s.glow) {
        g.setColour(colour.withAlpha(0.18f));
        g.strokePath(value, juce::PathStrokeType(track * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(colour);
    g.strokePath(value, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // The live ring: where the value plays (the score's automation, a section, a match), outside the track.
    const juce::var live = slider.getProperties()["live"];
    if (!live.isVoid()) {
        const float lp = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(live)));
        if (std::abs(lp - pos) > 0.004f) {
            const float la = startAngle + lp * (endAngle - startAngle), lr = ring + 1.5f;
            juce::Path l;
            l.addCentredArc(c.x, c.y, lr, lr, 0.0f, std::min(angle, la), std::max(angle, la), true);
            g.setColour(colour.brighter(0.7f).withAlpha(0.95f));
            g.strokePath(l, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse(c.x + std::sin(la) * lr - 2.2f, c.y - std::cos(la) * lr - 2.2f, 4.4f, 4.4f);
        }
    }
    // The cap, lit a little from above, with a hairline rim.
    const float cap = ring - track - std::max(2.0f, size * 0.05f);
    g.setGradientFill(juce::ColourGradient(s.raised.brighter(0.25f), c.x, c.y - cap, s.raised.darker(0.35f), c.x, c.y + cap, false));
    g.fillEllipse(c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap);
    g.setColour(s.edge.brighter(0.2f));
    g.drawEllipse(c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap, 1.0f);
    // The pointer; on a skin that shows the value inside the knob, short, so the number stays readable.
    const float sn = std::sin(angle), co = -std::cos(angle);
    const bool inside = s.valueInKnob && slider.getTextBoxPosition() == juce::Slider::NoTextBox;
    g.setColour(s.ink);
    if (inside) {
        g.drawLine(c.x + sn * cap * 0.72f, c.y + co * cap * 0.72f, c.x + sn * cap * 0.98f, c.y + co * cap * 0.98f, std::max(1.5f, size * 0.035f));
        const juce::String v = slider.getTextFromValue(slider.getValue()).trim();
        g.setColour(s.ink.withAlpha(slider.isEnabled() ? 0.92f : 0.4f));
        g.setFont(font(juce::jlimit(8.5f, 15.0f, size * 0.18f)));
        g.drawFittedText(v, juce::Rectangle<float>(c.x - cap * 0.82f, c.y - cap * 0.4f, cap * 1.64f, cap * 0.8f).toNearestInt(),
                         juce::Justification::centred, 1, 0.45f);
    } else {
        g.drawLine(c.x + sn * cap * 0.25f, c.y + co * cap * 0.25f, c.x + sn * cap * 0.85f, c.y + co * cap * 0.85f, std::max(1.5f, size * 0.035f));
    }
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const Skin& s = skin_;
    const juce::var live = slider.getProperties()["live"];
    if (style == juce::Slider::LinearVertical) {
        // A fader: its slot, the level up to the thumb, a cap across it; the live value as a bright tick beside it.
        const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float top = static_cast<float>(y), bottom = static_cast<float>(y + height);
        g.setColour(slider.findColour(juce::Slider::backgroundColourId));
        g.fillRoundedRectangle(cx - 2.5f, top, 5.0f, bottom - top, 2.5f);
        g.setColour(slider.findColour(juce::Slider::trackColourId));
        g.fillRoundedRectangle(cx - 2.5f, pos, 5.0f, bottom - pos, 2.5f);
        if (!live.isVoid()) {
            const float lp = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(live)));
            const float ly = bottom - lp * (bottom - top);
            if (std::abs(ly - pos) > 1.5f) {
                g.setColour(slider.findColour(juce::Slider::trackColourId).brighter(0.7f));
                g.fillRect(cx + 5.0f, ly - 1.0f, 6.0f, 2.0f);
            }
        }
        const float tw = std::min(static_cast<float>(width) - 4.0f, 26.0f);
        g.setColour(s.raised.brighter(0.3f));
        g.fillRoundedRectangle(cx - tw * 0.5f, pos - 5.0f, tw, 10.0f, 2.5f);
        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillRect(cx - tw * 0.5f + 3.0f, pos - 0.75f, tw - 6.0f, 1.5f);
        return;
    }
    if (style != juce::Slider::LinearHorizontal) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, pos, 0.0f, 0.0f, style, slider);
        return;
    }
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    g.setColour(s.faint);
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(width), 4.0f, 2.0f);
    g.setColour(slider.findColour(juce::Slider::trackColourId));
    // A slider that spans zero (a matrix slot's amount, the master filter) fills from its middle.
    float from = static_cast<float>(x);
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        from = static_cast<float>(x) + static_cast<float>(slider.valueToProportionOfLength(0.0)) * static_cast<float>(width);
    g.fillRoundedRectangle(std::min(from, pos), cy - 2.0f, std::fabs(pos - from), 4.0f, 2.0f);
    if (!live.isVoid()) {
        const float lp = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(live)));
        const float lx = static_cast<float>(x) + lp * static_cast<float>(width);
        if (std::abs(lx - pos) > 1.5f) {
            g.setColour(slider.findColour(juce::Slider::trackColourId).brighter(0.7f));
            g.fillRect(lx - 1.0f, cy - 8.0f, 2.0f, 5.0f);
        }
    }
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.fillEllipse(pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // A small switch with its lamp: lit in the button's colour when on.
    const Skin& s = skin_;
    const auto r = b.getLocalBounds().toFloat();
    const float h = std::min(16.0f, r.getHeight() - 4.0f);
    const juce::Rectangle<float> box(r.getX() + 2.0f, r.getCentreY() - h * 0.5f, h * 1.7f, h);
    const bool on = b.getToggleState();
    const juce::Colour colour = b.findColour(juce::ToggleButton::tickColourId);
    g.setColour(on ? colour.withAlpha(0.35f) : s.raised);
    g.fillRoundedRectangle(box, h * 0.5f);
    g.setColour(highlighted ? s.edge.brighter(0.4f) : s.edge);
    g.drawRoundedRectangle(box, h * 0.5f, 1.0f);
    const float d = h - 6.0f;
    g.setColour(on ? colour : s.dim);
    g.fillEllipse(on ? box.getRight() - d - 3.0f : box.getX() + 3.0f, box.getY() + 3.0f, d, d);
    if (b.getButtonText().isNotEmpty()) {
        g.setColour(on ? s.ink : s.dim);
        g.setFont(font(13.0f));
        g.drawFittedText(b.getButtonText(), box.getRight() + 6.0f > r.getRight() ? r.toNearestInt() : r.withLeft(box.getRight() + 6.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);
    }
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const Skin& s = skin_;
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, s.radius);
    g.setColour(box.isMouseOver(true) ? s.edge.brighter(0.4f) : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(r, s.radius, 1.0f);
    const float ax = static_cast<float>(width) - 14.0f, ay = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted, bool down)
{
    const Skin& s = skin_;
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    juce::Colour fill = b.getToggleState() ? b.findColour(juce::TextButton::buttonOnColourId) : background;
    if (down) fill = fill.brighter(0.15f);
    else if (highlighted) fill = fill.brighter(0.08f);
    juce::Path p;
    p.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), s.radius, s.radius,
                          !b.isConnectedOnLeft() && !b.isConnectedOnTop(), !b.isConnectedOnRight() && !b.isConnectedOnTop(),
                          !b.isConnectedOnLeft() && !b.isConnectedOnBottom(), !b.isConnectedOnRight() && !b.isConnectedOnBottom());
    g.setColour(fill);
    g.fillPath(p);
    g.setColour(highlighted ? s.edge.brighter(0.45f) : s.edge);
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

void LookAndFeel::drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool isMouseOver, bool)
{
    // Flat tabs: the name, the page in front lit in the accent and underlined.
    const Skin& s = skin_;
    const auto r = b.getLocalBounds().toFloat();
    const bool front = b.isFrontTab();
    if (front) {
        g.setColour(s.panel);
        g.fillRect(r);
        g.setColour(s.accent);
        g.fillRect(r.getX() + 6.0f, r.getBottom() - 2.5f, r.getWidth() - 12.0f, 2.5f);
    } else if (isMouseOver) {
        g.setColour(s.group.withAlpha(0.8f));
        g.fillRect(r);
    }
    g.setColour(front ? s.accent : (isMouseOver ? s.ink : s.dim));
    g.setFont(getTabButtonFont(b, r.getHeight()));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.setColour(skin_.edge);
    g.fillRect(0, bar.getHeight() - 1, bar.getWidth(), 1);
}

void LookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

int LookAndFeel::getTabButtonBestWidth(juce::TabBarButton& b, int tabDepth)
{
    return juce::GlyphArrangement::getStringWidthInt(getTabButtonFont(b, static_cast<float>(tabDepth)), b.getButtonText()) + 22;
}

juce::Font LookAndFeel::getTabButtonFont(juce::TabBarButton&, float) { return font(14.0f); }
juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return font(13.5f); }
juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int) { return font(13.5f); }
juce::Font LookAndFeel::getPopupMenuFont() { return font(14.0f); }

juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(slider);
    l->setFont(font(12.0f));
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::textColourId, skin_.dim);
    return l;
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(skin_.group);
    g.setColour(skin_.edge);
    g.drawRect(0, 0, width, height, 1);
}

void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll(skin_.group);
    g.setColour(skin_.edge);
    g.drawRect(0, 0, width, height, 1);
    juce::AttributedString a;
    a.setJustification(juce::Justification::centredLeft);
    a.append(text, font(13.0f), skin_.ink);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths(a, static_cast<float>(width) - 12.0f);
    tl.draw(g, juce::Rectangle<float>(6.0f, 4.0f, static_cast<float>(width) - 12.0f, static_cast<float>(height) - 8.0f));
}

namespace {
/** @brief The skin's title font at @p height, with its tracking (the logo's lettering). */
juce::Font titleFont(const Skin& skin, float height)
{
    const int style = skin.titleBold ? juce::Font::bold : juce::Font::plain;
    juce::Font f = skin.typeface.isNotEmpty() ? juce::Font(juce::FontOptions(skin.typeface, height, style)) : font(height, skin.titleBold);
    f.setExtraKerningFactor(skin.tracking);
    return f;
}
} // namespace

float titleWidth(const Skin& skin, float height)
{
    return juce::GlyphArrangement::getStringWidth(titleFont(skin, height), skin.name.toUpperCase()) + 4.0f;
}

void drawTitle(juce::Graphics& g, const Skin& skin, juce::Rectangle<float> r, float height)
{
    const juce::String t = skin.name.toUpperCase();
    const juce::Font f = titleFont(skin, height);
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, t, r.getX(), r.getCentreY() + height * 0.36f);
    juce::Path p;
    ga.createPath(p);
    if (skin.glow) {
        g.setColour(skin.accent.withAlpha(0.22f));
        g.strokePath(p, juce::PathStrokeType(height * 0.18f));
    }
    g.setColour(skin.accent);
    g.fillPath(p);
}

// ======================================================================================================== flat tabs

void FlatTab::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    auto* lf = dynamic_cast<LookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr) return;
    const Skin& s = lf->skin();
    const auto r = getLocalBounds().toFloat();
    const bool front = getToggleState();
    if (front) {
        g.setColour(s.panel);
        g.fillRect(r);
        g.setColour(s.accent);
        g.fillRect(r.getX() + 6.0f, r.getBottom() - 2.5f, r.getWidth() - 12.0f, 2.5f);
    } else if (highlighted) {
        g.setColour(s.group.withAlpha(0.8f));
        g.fillRect(r);
    }
    g.setColour(front ? s.accent : (highlighted ? s.ink : s.dim));
    g.setFont(font(14.0f));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

int FlatTab::bestWidth() const { return juce::GlyphArrangement::getStringWidthInt(font(14.0f), getButtonText()) + 22; }

// ============================================================================================================ icons

IconButton::IconButton(Icon icon, const juce::String& tooltip) : juce::Button(tooltip), icon_(icon)
{
    setTooltip(tooltip);
    setWantsKeyboardFocus(false);
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();
    lf.drawButtonBackground(g, *this, findColour(getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId),
                            highlighted && isEnabled(), down);
    const auto b = getLocalBounds().toFloat();
    const float s = std::min(b.getWidth(), b.getHeight()) * 0.56f;
    const auto r = b.withSizeKeepingCentre(s, s);
    const juce::Colour ink = findColour(juce::TextButton::textColourOffId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.35f);
    g.setColour(ink);
    const float w = std::max(1.4f, s * 0.11f);
    const juce::PathStrokeType stroke(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (icon_) {
    case Icon::Undo:
    case Icon::Redo: {
        // A curved arrow turning back (undo) or on (redo).
        const bool back = icon_ == Icon::Undo;
        const float cx = r.getCentreX(), cy = r.getCentreY() + s * 0.1f, rad = s * 0.36f;
        p.addCentredArc(cx, cy, rad, rad, 0.0f, back ? -juce::MathConstants<float>::halfPi * 1.6f : juce::MathConstants<float>::halfPi * 1.6f,
                        back ? juce::MathConstants<float>::halfPi * 0.9f : -juce::MathConstants<float>::halfPi * 0.9f, true);
        g.strokePath(p, stroke);
        const float ax = cx + (back ? -1.0f : 1.0f) * rad * std::sin(juce::MathConstants<float>::halfPi * 1.6f);
        const float ay = cy - rad * std::cos(juce::MathConstants<float>::halfPi * 1.6f);
        juce::Path head;
        const float k = s * 0.22f, dir = back ? -1.0f : 1.0f;
        head.addTriangle(ax - dir * k * 0.2f, ay - k, ax + dir * k * 0.9f, ay - k * 0.1f, ax - dir * k * 0.5f, ay + k * 0.7f);
        g.fillPath(head);
        break;
    }
    case Icon::Help: {
        g.setFont(font(s * 1.25f, true));
        g.drawText("?", b, juce::Justification::centred);
        break;
    }
    case Icon::Settings: {
        // A cog: a ring with eight teeth and a hole.
        const auto c = r.getCentre();
        const float ro = s * 0.5f, ri = s * 0.36f;
        for (int i = 0; i < 16; ++i) {
            const float a0 = juce::MathConstants<float>::twoPi * (static_cast<float>(i) - 0.5f) / 16.0f;
            const float a1 = juce::MathConstants<float>::twoPi * (static_cast<float>(i) + 0.5f) / 16.0f;
            const float rr = (i % 2 == 0) ? ro : ri;
            if (i == 0) p.startNewSubPath(c.x + rr * std::sin(a0), c.y - rr * std::cos(a0));
            else p.lineTo(c.x + rr * std::sin(a0), c.y - rr * std::cos(a0));
            p.lineTo(c.x + rr * std::sin(a1), c.y - rr * std::cos(a1));
        }
        p.closeSubPath();
        p.addEllipse(c.x - s * 0.15f, c.y - s * 0.15f, s * 0.3f, s * 0.3f);
        p.setUsingNonZeroWinding(false);
        g.fillPath(p);
        break;
    }
    case Icon::ThumbUp:
    case Icon::ThumbDown: {
        // A thumb: the cuff and the hand with the thumb raised; down is the same turned over.
        juce::Path t;
        t.addRoundedRectangle(0.0f, 0.42f, 0.22f, 0.58f, 0.04f);
        t.startNewSubPath(0.3f, 0.45f);
        t.lineTo(0.48f, 0.08f);
        t.quadraticTo(0.62f, 0.0f, 0.62f, 0.18f);
        t.lineTo(0.56f, 0.38f);
        t.lineTo(0.9f, 0.38f);
        t.quadraticTo(1.0f, 0.4f, 0.98f, 0.52f);
        t.lineTo(0.9f, 0.92f);
        t.quadraticTo(0.86f, 1.0f, 0.78f, 1.0f);
        t.lineTo(0.3f, 1.0f);
        t.closeSubPath();
        juce::AffineTransform at = juce::AffineTransform::scale(s, s);
        if (icon_ == Icon::ThumbDown) at = juce::AffineTransform::verticalFlip(1.0f).scaled(s, s);
        t.applyTransform(at.translated(r.getX(), r.getY()));
        g.fillPath(t);
        break;
    }
    case Icon::Headset: {
        // A visor: a rounded band with two lenses.
        juce::Path v;
        v.addRoundedRectangle(r.getX(), r.getCentreY() - s * 0.25f, s, s * 0.5f, s * 0.18f);
        g.strokePath(v, stroke);
        g.fillEllipse(r.getX() + s * 0.18f, r.getCentreY() - s * 0.08f, s * 0.2f, s * 0.16f);
        g.fillEllipse(r.getRight() - s * 0.38f, r.getCentreY() - s * 0.08f, s * 0.2f, s * 0.16f);
        break;
    }
    }
}

// =========================================================================================================== header

void layoutHeader(juce::Rectangle<int>& area, const Header& h)
{
    // The first row: who, what, how long -- then make, play, silence. What does not fit squeezes the length slider.
    auto top = area.removeFromTop(kHeaderRow);
    if (h.mute != nullptr) h.mute->setBounds(top.removeFromRight(68).reduced(3));
    if (h.play != nullptr) h.play->setBounds(top.removeFromRight(66).reduced(3));
    for (auto it = h.actions.rbegin(); it != h.actions.rend(); ++it)
        it->first->setBounds(top.removeFromRight(it->second > 0 ? it->second : 96).reduced(3));
    top.removeFromRight(6);
    if (h.logo != nullptr) *h.logo = top.removeFromLeft(34).toFloat().reduced(2.0f);
    if (h.title != nullptr) h.title->setBounds(top.removeFromLeft(h.titleWidth > 0 ? h.titleWidth : 110));
    for (const auto& [c, w] : h.choices) c->setBounds(top.removeFromLeft(w > 0 ? w : 110).reduced(3));
    top.removeFromLeft(8);
    for (const auto& [c, w] : h.modes) c->setBounds(top.removeFromLeft(w > 0 ? w : 70).reduced(0, 3));
    top.removeFromLeft(4);
    if (h.lengthLabel != nullptr) h.lengthLabel->setBounds(top.removeFromLeft(top.getWidth() > 200 ? 50 : 0));
    if (h.length != nullptr) h.length->setBounds(top.withWidth(std::min(top.getWidth(), 230)).reduced(2));
    area.removeFromTop(4);
    // The second: the ratings, what plays and what was rerolled; the tools at its right end.
    auto row = area.removeFromTop(kStatusRow);
    for (auto it = h.tools.rbegin(); it != h.tools.rend(); ++it) {
        if (*it == nullptr) row.removeFromRight(8);
        else if ((*it)->isVisible()) (*it)->setBounds(row.removeFromRight(30).reduced(2, 0));
    }
    row.removeFromRight(8);
    if (h.update != nullptr) h.update->setBounds(row.removeFromRight(h.update->isVisible() ? 180 : 0));
    if (h.like != nullptr) h.like->setBounds(row.removeFromLeft(28).reduced(1, 0));
    if (h.dislike != nullptr) h.dislike->setBounds(row.removeFromLeft(28).reduced(1, 0));
    row.removeFromLeft(6);
    if (h.status != nullptr && h.curation != nullptr) {
        h.status->setBounds(row.removeFromLeft(row.getWidth() * 3 / 5));
        h.curation->setBounds(row);
    } else if (h.status != nullptr) {
        h.status->setBounds(row);
    }
    area.removeFromTop(4);
}

void connectModes(std::initializer_list<juce::Button*> modes, juce::Colour on)
{
    const int n = static_cast<int>(modes.size());
    int i = 0;
    for (juce::Button* b : modes) {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < n - 1) edges |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges(edges);
        b->setColour(juce::TextButton::buttonOnColourId, on);
        ++i;
    }
}

// ========================================================================================================= backdrop

void Backdrop::paint(juce::Graphics& g, juce::Rectangle<int> area, int headerBottom, const Skin& skin, bool on)
{
    g.setColour(skin.bg);
    g.fillRect(area);
    const juce::Image picture = skin.backdrop();
    if (!on || !picture.isValid() || area.isEmpty()) return;
    if (cache_.isNull() || cachedFor_ != area || cachedHeader_ != headerBottom || cachedOn_ != on) {
        // Rendered once per size: the picture covering the window (cropped, centred), strong behind the header and fading
        // to the page strength below it, under a vignette that keeps the edges and the corners quiet.
        cache_ = juce::Image(juce::Image::RGB, area.getWidth(), area.getHeight(), true);
        juce::Graphics c(cache_);
        c.fillAll(skin.bg);
        const auto& src = picture;
        const juce::Rectangle<int> crop(juce::roundToInt(skin.crop.getX() * static_cast<float>(src.getWidth())),
                                        juce::roundToInt(skin.crop.getY() * static_cast<float>(src.getHeight())),
                                        juce::roundToInt(skin.crop.getWidth() * static_cast<float>(src.getWidth())),
                                        juce::roundToInt(skin.crop.getHeight() * static_cast<float>(src.getHeight())));
        const juce::Image part = src.getClippedImage(crop);
        const float sw = static_cast<float>(area.getWidth()) / static_cast<float>(part.getWidth());
        const float sh = static_cast<float>(area.getHeight()) / static_cast<float>(part.getHeight());
        const float k = std::max(sw, sh);
        const float w = static_cast<float>(part.getWidth()) * k, h = static_cast<float>(part.getHeight()) * k;
        const juce::Rectangle<float> dest((static_cast<float>(area.getWidth()) - w) * 0.5f, 0.0f, w, h);
        c.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        c.setOpacity(skin.backdropTop);
        c.drawImage(part, dest);
        c.setOpacity(1.0f);
        // Down from the header the picture fades to its page strength.
        const float hb = static_cast<float>(headerBottom), H = static_cast<float>(area.getHeight());
        juce::ColourGradient fade(skin.bg.withAlpha(0.0f), 0.0f, hb * 0.6f, skin.bg.withAlpha(1.0f - skin.backdropPage / std::max(0.01f, skin.backdropTop)),
                                  0.0f, hb + 40.0f, false);
        c.setGradientFill(fade);
        c.fillRect(0.0f, hb * 0.6f, static_cast<float>(area.getWidth()), H);
        // The header's text stays readable: a soft darkening behind its left and right ends.
        c.setGradientFill(juce::ColourGradient(skin.bg.withAlpha(0.55f), 0.0f, 0.0f, skin.bg.withAlpha(0.0f), 0.0f, hb, false));
        c.fillRect(0.0f, 0.0f, static_cast<float>(area.getWidth()), hb);
        // The vignette.
        juce::ColourGradient v(skin.bg.withAlpha(0.0f), static_cast<float>(area.getWidth()) * 0.5f, H * 0.4f, skin.bg.withAlpha(0.65f), 0.0f, H, true);
        c.setGradientFill(v);
        c.fillRect(0.0f, 0.0f, static_cast<float>(area.getWidth()), H);
        cachedFor_ = area;
        cachedHeader_ = headerBottom;
        cachedOn_ = on;
    }
    g.drawImageAt(cache_, area.getX(), area.getY());
}

// ========================================================================================================= settings

namespace {
/** @brief Every instrument's settings in this process, deleted when JUCE shuts down (not at the DLL's unloading). */
struct SettingsRegistry final : public juce::DeletedAtShutdown {
    std::map<juce::String, std::unique_ptr<Settings>> all;   ///< the settings by instrument name
    /** @brief The registry, created on first use; null after JUCE has deleted it. */
    static SettingsRegistry*& instance() { static SettingsRegistry* r = nullptr; return r; }
    ~SettingsRegistry() override { instance() = nullptr; }
};
} // namespace

Settings& Settings::of(const juce::String& app)
{
    SettingsRegistry*& r = SettingsRegistry::instance();
    if (r == nullptr) r = new SettingsRegistry();
    auto& s = r->all[app];
    if (s == nullptr) s = std::make_unique<Settings>(app);
    return *s;
}

Settings::Settings(const juce::String& app) : app_(app)
{
    juce::PropertiesFile::Options o;
    o.applicationName = app;
    o.filenameSuffix = ".frame";   // beside <app>.settings (the standalone's window and audio) and <app>.updates
    o.folderName = app;
    o.osxLibrarySubFolder = "Application Support";
    file_ = std::make_unique<juce::PropertiesFile>(o);
}

Settings::HeadsetMode Settings::headset() const
{
    const juce::String m = file_->getValue("headset", "auto");
    return m == "on" ? HeadsetMode::On : m == "off" ? HeadsetMode::Off : HeadsetMode::Auto;
}

void Settings::setHeadset(HeadsetMode m)
{
    file_->setValue("headset", m == HeadsetMode::On ? "on" : m == HeadsetMode::Off ? "off" : "auto");
    file_->saveIfNeeded();
    sendChangeMessage();
}

int Settings::headsetPort() const { return file_->getIntValue("headsetPort", portOf(app_)); }

bool Settings::backdrop() const { return file_->getBoolValue("backdrop", true); }

void Settings::setBackdrop(bool on)
{
    file_->setValue("backdrop", on);
    file_->saveIfNeeded();
    sendChangeMessage();
}

bool Settings::overview() const { return file_->getBoolValue("overview", true); }

void Settings::setOverview(bool on)
{
    file_->setValue("overview", on);
    file_->saveIfNeeded();
    sendChangeMessage();
}

bool Settings::link() const { return file_->getBoolValue("link", false); }

void Settings::setLink(bool on)
{
    file_->setValue("link", on);
    file_->saveIfNeeded();
    sendChangeMessage();
}

// FAMILY_CUES=<port> switches the cues on for one process, to that port (a test aid, as FAMILY_LINK is Link's).
bool Settings::cues() const { return std::getenv("FAMILY_CUES") != nullptr || file_->getBoolValue("cues", false); }

void Settings::setCues(bool on)
{
    file_->setValue("cues", on);
    file_->saveIfNeeded();
    sendChangeMessage();
}

int Settings::cuePort() const
{
    if (const char* p = std::getenv("FAMILY_CUES")) return juce::jlimit(1024, 65535, std::atoi(p));
    return juce::jlimit(1024, 65535, file_->getIntValue("cuePort", 9000));
}

Settings::JamRole Settings::jamRole() const
{
    // FAMILY_JAM=lead|follow sets the role for one process (a test aid, as FAMILY_LINK is Link's).
    if (const char* e = std::getenv("FAMILY_JAM")) return std::strcmp(e, "lead") == 0 ? JamRole::Lead : JamRole::Follow;
    const int r = file_->getIntValue("jam", 0);
    return r == 1 ? JamRole::Lead : r == 2 ? JamRole::Follow : JamRole::Off;
}

void Settings::setJamRole(JamRole r)
{
    file_->setValue("jam", r == JamRole::Lead ? 1 : r == JamRole::Follow ? 2 : 0);
    file_->saveIfNeeded();
    sendChangeMessage();
}

void SettingsMenu::show(juce::Component& target) const
{
    Settings& s = Settings::of(app);
    juce::PopupMenu m;
    m.addSectionHeader(app + " " + version);
    if (updatesOn && setUpdates) m.addItem("Check for updates once a day", true, updatesOn(), [f = setUpdates, on = updatesOn()] { f(!on); });
    m.addItem("Picture behind the panel", true, s.backdrop(), [&s] { s.setBackdrop(!s.backdrop()); });
    m.addItem("Overview above the tabs (off: the pages get its height)", true, s.overview(), [&s] { s.setOverview(!s.overview()); });
    {
        juce::PopupMenu h;
        const auto mode = s.headset();
        using M = Settings::HeadsetMode;
        h.addItem("Auto: show its controls when a headset sends", true, mode == M::Auto, [&s] { s.setHeadset(M::Auto); });
        h.addItem("Always show them", true, mode == M::On, [&s] { s.setHeadset(M::On); });
        h.addItem(headsetOffText, true, mode == M::Off, [&s] { s.setHeadset(M::Off); });
        if (headsetItems) {
            h.addSeparator();
            headsetItems(h);
        }
        if (headsetStatus) {
            h.addSeparator();
            h.addItem(headsetStatus(), false, false, nullptr);
        }
        m.addSubMenu("Headset (Meta Quest)", h);
    }
    if (linkStatus)   // the standalone only (02.10.2026): a DAW's transport rules the plugin
        m.addItem("Ableton Link: tempo and beat with other apps (" + linkStatus() + ")", true, s.link(), [&s] { s.setLink(!s.link()); });
    if (cueStatus)
        m.addItem("Score cues for a visualiser: OSC to port " + juce::String(s.cuePort()) + " (" + cueStatus() + ")", true, s.cues(),
                  [&s] { s.setCues(!s.cues()); });
    if (jamStatus) {
        // The family jam (02.10.2026, Jam.h): one instrument leads -- its key, its sections, its breaks and drops go out
        // to the others on the network --, the ones that follow take them up; tempo and bars come from Ableton Link.
        juce::PopupMenu j;
        using R = Settings::JamRole;
        const R role = s.jamRole();
        j.addItem("Off", true, role == R::Off, [&s] { s.setJamRole(R::Off); });
        j.addItem("Lead: the others follow this one's key, sections, breaks and drops", true, role == R::Lead, [&s] { s.setJamRole(R::Lead); });
        j.addItem("Follow: take up the leader's key, energy, breaks and drops", true, role == R::Follow, [&s] { s.setJamRole(R::Follow); });
        j.addSeparator();
        j.addItem(jamStatus(), false, false, nullptr);
        m.addSubMenu("Family jam", j);
    }
    if (setWindowScale) {
        juce::PopupMenu w;
        for (int pct : { 75, 100, 125, 150, 200 })
            w.addItem(juce::String(pct) + " %", [f = setWindowScale, pct] { f(static_cast<float>(pct) / 100.0f); });
        m.addSubMenu("Window size", w);
    }
    if (canFullScreen && canFullScreen() && toggleFullScreen)
        m.addItem("Full screen (F11)", true, isFullScreen && isFullScreen(), [f = toggleFullScreen] { f(); });
    if (moreItems) {
        m.addSeparator();
        moreItems(m);
    }
    m.addSeparator();
    m.addItem("Keys ...", [n = app] {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon, n + ": keys", keysText(), "Close");
    });
    m.addItem("About " + app + " ...", [n = app, v = version, more = about, own = showAbout] {
        if (own) { own(); return; }
        juce::String t;
        t << n << " " << v << "\nbuilt " << juce::String(__DATE__).trim() << "\n\n" << (more ? more() : juce::String());
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon, "About " + n, t, "Close");
    });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target));
}

// ============================================================================================================= keys

bool handleKey(const juce::KeyPress& key, const Keys& k)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    auto run = [](const std::function<void()>& f) { if (!f) return false; f(); return true; };
    if (code == juce::KeyPress::spaceKey && !mods.isAnyModifierKeyDown()) return run(k.playStop);
    if (code == juce::KeyPress::F1Key) return run(k.help);
    if (code == juce::KeyPress::F11Key) return run(k.fullScreen);
    if (code == juce::KeyPress::escapeKey) return run(k.escape);
    if (mods.isCommandDown()) {
        const juce::juce_wchar c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter() != 0 ? key.getTextCharacter() : static_cast<juce::juce_wchar>(code));
        if (c == 'z' && mods.isShiftDown()) return run(k.redo);
        if (c == 'z') return run(k.undo);
        if (c == 'y') return run(k.redo);
        if (c == 's') return run(k.save);
        if (c == 'o') return run(k.open);
        if (c == 'e') return run(k.exportFile);
    }
    return false;
}

juce::String keysText()
{
    return "Space\tplay / stop\n"
           "Ctrl+Z\tundo\n"
           "Ctrl+Y, Ctrl+Shift+Z\tredo\n"
           "F1\thelp\n"
           "F11\tfull screen (the standalone)\n"
           "Esc\tclose the help, leave full screen\n"
           "Ctrl+S\tsave the set\n"
           "Ctrl+O\topen a set\n"
           "Ctrl+E\texport\n\n"
           "On every control: double click its default, right click MIDI learn, forget, default.";
}

void keepKeysForEditor(juce::Component& root)
{
    for (auto* c : root.getChildren()) {
        if (dynamic_cast<juce::Button*>(c) != nullptr || dynamic_cast<juce::ComboBox*>(c) != nullptr
            || dynamic_cast<juce::Slider*>(c) != nullptr || dynamic_cast<juce::TabBarButton*>(c) != nullptr) {
            c->setWantsKeyboardFocus(false);
            c->setMouseClickGrabsKeyboardFocus(false);
        }
        keepKeysForEditor(*c);
    }
}

// ============================================================================================================= help

HelpView::HelpView(const juce::String& chapters, const Skin& skin) : skin_(skin)
{
    // chapters.txt as (name, text): paragraphs joined, lines indented by four spaces kept as they stand.
    juce::String name, textOf, para;
    auto flushPara = [&] { if (para.isNotEmpty()) { textOf << para.trimEnd() << "\n\n"; para.clear(); } };
    auto flushChapter = [&] { flushPara(); if (name.isNotEmpty()) chapters_.emplace_back(name, textOf.trimEnd()); textOf.clear(); };
    for (const juce::String& raw : juce::StringArray::fromLines(chapters)) {
        const juce::String l = raw.trimEnd();
        if (l.startsWith("#")) continue;
        if (l.startsWith("== ") && l.endsWith(" ==")) {
            flushChapter();
            name = l.substring(3, l.length() - 3).upToFirstOccurrenceOf("|", false, false).trim();
            continue;
        }
        if (name.isEmpty()) continue;
        if (l.isEmpty()) { flushPara(); continue; }
        if (l.startsWith("    ")) { flushPara(); textOf << l.substring(4) << "\n"; continue; }
        if (l.startsWith("[") && l.endsWith("]")) continue;   // a picture of the printed manual
        if (para.isNotEmpty()) para << " ";
        para << l.trim();
    }
    flushChapter();
    list_.setModel(this);
    list_.setRowHeight(26);
    list_.setColour(juce::ListBox::backgroundColourId, skin_.group);
    addAndMakeVisible(list_);
    text_.setMultiLine(true, true);
    text_.setReadOnly(true);
    text_.setScrollbarsShown(true);
    text_.setCaretVisible(false);
    text_.setFont(font(15.0f));
    text_.setColour(juce::TextEditor::backgroundColourId, skin_.group);
    text_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text_.setIndents(14, 12);
    addAndMakeVisible(text_);
    close_.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(close_);
    setWantsKeyboardFocus(true);
}

void HelpView::refresh()
{
    const juce::String was = names_[list_.getSelectedRow()];
    names_.clear();
    texts_.clear();
    if (extraTopics)
        for (const auto& [n, t] : extraTopics()) { names_.add(n); texts_.add(t); }
    for (const auto& [n, t] : chapters_) { names_.add(n); texts_.add(t); }
    list_.updateContent();
    const int keep = names_.indexOf(was);
    showTopic(keep >= 0 ? keep : 0);
}

void HelpView::showTopic(int index)
{
    if (index < 0 || index >= names_.size()) return;
    list_.selectRow(index, false, true);
    text_.setText(texts_[index], false);
    text_.setCaretPosition(0);
    text_.scrollEditorToPositionCaret(0, 0);
}

void HelpView::selectedRowsChanged(int row)
{
    if (row >= 0 && row < texts_.size()) {
        text_.setText(texts_[row], false);
        text_.setCaretPosition(0);
        text_.scrollEditorToPositionCaret(0, 0);
    }
}

void HelpView::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected) {
        g.setColour(skin_.accent.withAlpha(0.22f));
        g.fillRect(0, 0, width, height);
        g.setColour(skin_.accent);
        g.fillRect(0, 0, 3, height);
    }
    g.setColour(selected ? skin_.ink : skin_.dim);
    g.setFont(font(14.0f));
    g.drawText(names_[row], 12, 0, width - 16, height, juce::Justification::centredLeft, true);
}

void HelpView::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(30);
    close_.setBounds(top.removeFromRight(90).reduced(2));
    r.removeFromTop(6);
    list_.setBounds(r.removeFromLeft(std::min(260, r.getWidth() / 3)));
    r.removeFromLeft(10);
    text_.setBounds(r);
}

void HelpView::paint(juce::Graphics& g)
{
    g.fillAll(skin_.bg);
    g.setColour(skin_.accent);
    g.setFont(font(16.0f, true));
    g.drawText("HELP", getLocalBounds().reduced(14, 10).withHeight(30), juce::Justification::centredLeft);
    g.setColour(skin_.dim);
    g.setFont(font(12.5f));
    g.drawText("F1 opens it, Esc closes it", getLocalBounds().reduced(80, 10).withHeight(30), juce::Justification::centredLeft);
}

bool HelpView::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key.getKeyCode() == juce::KeyPress::F1Key) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

// ============================================================================================================= undo

void UndoHistory::begin(const juce::String& what, std::vector<float> values, juce::String extra, bool allValues)
{
    if (depth_++ > 0) {
        all_ = all_ || allValues;
        return;
    }
    what_ = what;
    before_ = std::move(values);
    extra_ = std::move(extra);
    touched_.clear();
    all_ = allValues;
}

void UndoHistory::end(const std::vector<float>& values, const juce::String& extra)
{
    if (depth_ == 0 || --depth_ > 0) return;
    UndoStep s;
    s.what = what_;
    s.extraBefore = extra_;
    s.extraAfter = extra;
    const size_t n = std::min(before_.size(), values.size());
    auto add = [&](size_t i) {
        if (i < n && before_[i] != values[i]) s.values.emplace_back(static_cast<int>(i), before_[i], values[i]);
    };
    if (all_) {
        for (size_t i = 0; i < n; ++i) add(i);
    } else {
        std::sort(touched_.begin(), touched_.end());
        touched_.erase(std::unique(touched_.begin(), touched_.end()), touched_.end());
        for (int id : touched_) if (id >= 0) add(static_cast<size_t>(id));
    }
    before_.clear();
    touched_.clear();
    if (s.empty()) return;
    redo_.clear();
    undo_.push_back(std::move(s));
    while (undo_.size() > kSteps) undo_.pop_front();
}

const UndoStep* UndoHistory::undo()
{
    if (undo_.empty() || depth_ > 0) return nullptr;
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    return &redo_.back();
}

const UndoStep* UndoHistory::redo()
{
    if (redo_.empty() || depth_ > 0) return nullptr;
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    return &undo_.back();
}

// ========================================================================================================= sub-tabs

void SubTabs::add(const juce::String& name, std::function<std::unique_ptr<juce::Component>()> make, juce::Colour colour)
{
    Page p;
    p.name = name;
    p.colour = colour;
    p.make = std::move(make);
    p.button = std::make_unique<juce::TextButton>(name);
    p.button->setClickingTogglesState(false);
    p.button->setWantsKeyboardFocus(false);
    const int index = static_cast<int>(pages_.size());
    p.button->onClick = [this, index] { show(index); };
    addAndMakeVisible(*p.button);
    pages_.push_back(std::move(p));
    if (current_ < 0) show(0);
}

void SubTabs::show(int index)
{
    if (index < 0 || index >= count()) return;
    if (current_ >= 0 && pages_[static_cast<size_t>(current_)].comp != nullptr) pages_[static_cast<size_t>(current_)].comp->setVisible(false);
    current_ = index;
    Page& p = pages_[static_cast<size_t>(index)];
    if (p.comp == nullptr) {
        p.comp = p.make();
        addAndMakeVisible(*p.comp);
    }
    p.comp->setVisible(true);
    for (size_t i = 0; i < pages_.size(); ++i) {
        const juce::Colour c = pages_[i].colour.isTransparent() ? skin_.accent : pages_[i].colour;
        pages_[i].button->setColour(juce::TextButton::buttonOnColourId, c.withAlpha(0.42f));
        pages_[i].button->setToggleState(static_cast<int>(i) == index, juce::dontSendNotification);
    }
    resized();
    if (onChange) onChange(index);
}

juce::Component* SubTabs::page() const { return current_ >= 0 ? pages_[static_cast<size_t>(current_)].comp.get() : nullptr; }

void SubTabs::resized()
{
    auto r = getLocalBounds();
    auto row = r.removeFromTop(36).reduced(10, 5);
    for (auto& p : pages_) {
        const int w = juce::GlyphArrangement::getStringWidthInt(font(13.5f), p.name) + 34;
        p.button->setBounds(row.removeFromLeft(std::max(84, w)).reduced(2, 0));
    }
    if (auto* c = page()) c->setBounds(r);
}

void SubTabs::paint(juce::Graphics& g)
{
    g.setColour(skin_.edge);
    g.fillRect(10, 35, getWidth() - 20, 1);
}

// ======================================================================================================== sections

bool isModulationGroup(const juce::String& title)
{
    const juce::String t = title.toLowerCase();
    return t.contains("lfo") || t.startsWith("mod ") || t.contains("matrix") || t.startsWith("modulation") || t == "trance gate";
}

SectionPlan planSections(const juce::StringArray& titles, int available, const std::function<int(const std::vector<int>&)>& height,
                         int tolerance)
{
    SectionPlan plan;
    std::vector<int> all, sound, mod;
    for (int i = 0; i < titles.size(); ++i) {
        all.push_back(i);
        (isModulationGroup(titles[i]) ? mod : sound).push_back(i);
    }
    // FAMILY_NO_SECTIONS=1: every page whole (the manual's full-page pictures, which show all of a page at once).
    static const bool whole = juce::SystemStats::getEnvironmentVariable("FAMILY_NO_SECTIONS", "").isNotEmpty();
    if (whole || all.empty() || available <= 0 || height(all) <= available + tolerance) {
        plan.groups.push_back(all);
        plan.names.add({});
        return plan;
    }
    // A section's name: the first group's title without its number ("LFO 1" -> "LFO", "Matrix 1-2" -> "Matrix").
    auto plain = [&](int group) {
        juce::String t = titles[group].trim();
        while (t.isNotEmpty() && (juce::CharacterFunctions::isDigit(t.getLastCharacter()) || t.getLastCharacter() == '-'))
            t = t.dropLastCharacters(1).trimEnd();
        return t.isEmpty() ? titles[group] : t;
    };
    const bool both = !sound.empty() && !mod.empty();
    // A synth's page begins with its presets: its first section is its sound, whatever group follows them.
    const bool presets = !sound.empty() && titles[sound.front()].trim().endsWithIgnoreCase("preset");
    auto cut = [&](const std::vector<int>& kind, const juce::String& firstName) {
        std::vector<int> section;
        bool first = true;
        auto close = [&] {
            if (section.empty()) return;
            const bool named = first && (both || (presets && &kind == &sound));
            juce::String name = named ? firstName : plain(section.front());
            for (int n = 2; plan.names.contains(name); ++n) name = plain(section.front()) + " " + juce::String(n);
            plan.groups.push_back(section);
            plan.names.add(name);
            section.clear();
            first = false;
        };
        for (int g : kind) {
            std::vector<int> more = section;
            more.push_back(g);
            if (!section.empty() && height(more) > available + tolerance) close();
            section.push_back(g);
        }
        close();
    };
    cut(sound, "Sound");
    cut(mod, "Modulation");
    return plan;
}

SectionSwitch::SectionSwitch(const Skin& skin, const juce::StringArray& names) : skin_(skin)
{
    setNames(names);
}

void SectionSwitch::setNames(const juce::StringArray& names)
{
    juce::StringArray now;
    for (auto* b : buttons_) now.add(b->getButtonText());
    if (now == names) return;
    buttons_.clear();
    for (int i = 0; i < names.size(); ++i) {
        auto* b = buttons_.add(new juce::TextButton(names[i]));
        b->setClickingTogglesState(false);
        b->setWantsKeyboardFocus(false);
        b->setColour(juce::TextButton::buttonOnColourId, skin_.accent.withAlpha(0.42f));
        b->onClick = [this, i] {
            if (i == current_) return;
            setCurrent(i);
            if (onChange) onChange(i);
        };
        addAndMakeVisible(b);
    }
    setCurrent(current_);
    resized();
}

void SectionSwitch::setCurrent(int index)
{
    current_ = juce::jlimit(0, std::max(0, buttons_.size() - 1), index);
    for (int i = 0; i < buttons_.size(); ++i) buttons_[i]->setToggleState(i == current_, juce::dontSendNotification);
}

int SectionSwitch::bestWidth() const
{
    int w = 0;
    for (auto* b : buttons_) w += std::max(84, juce::GlyphArrangement::getStringWidthInt(font(13.5f), b->getButtonText()) + 34);
    return w;
}

void SectionSwitch::resized()
{
    auto r = getLocalBounds();
    for (auto* b : buttons_)
        b->setBounds(r.removeFromLeft(std::max(84, juce::GlyphArrangement::getStringWidthInt(font(13.5f), b->getButtonText()) + 34)).reduced(2, 0));
}

int pageOverflow(juce::Component& c)
{
    int most = 0;
    if (auto* v = dynamic_cast<juce::Viewport*>(&c))
        if (auto* inner = v->getViewedComponent()) most = inner->getHeight() - v->getMaximumVisibleHeight();
    for (auto* child : c.getChildren())
        if (child->isVisible()) most = std::max(most, pageOverflow(*child));
    return std::max(0, most);
}

namespace {
/** @brief The visible section switches under @p c (a page split into Sound and Modulation has one). */
void findSwitches(juce::Component& c, std::vector<SectionSwitch*>& out)
{
    if (auto* s = dynamic_cast<SectionSwitch*>(&c)) { if (s->isVisible()) out.push_back(s); return; }
    for (auto* child : c.getChildren())
        if (child->isVisible()) findSwitches(*child, out);
}

} // namespace

juce::String pageLines(juce::Component& page, const juce::String& name)
{
    juce::String out;
    std::vector<SectionSwitch*> switches;
    findSwitches(page, switches);
    if (switches.empty()) { out << name << "\t" << pageOverflow(page) << "\n"; return out; }
    SectionSwitch* s = switches.front();
    const int was = s->current();
    for (int k = 0; k < s->count(); ++k) {
        s->setCurrent(k);
        if (s->onChange) s->onChange(k);
        out << name << " [" << s->name(k) << "]\t" << pageOverflow(page) << "\n";
    }
    s->setCurrent(was);
    if (s->onChange) s->onChange(was);
    return out;
}

juce::String pageReport(juce::TabbedComponent& tabs)
{
    juce::String out;
    const int front = tabs.getCurrentTabIndex();
    const juce::StringArray names = tabs.getTabNames();
    for (int i = 0; i < tabs.getNumTabs(); ++i) {
        tabs.setCurrentTabIndex(i, false);
        juce::Component* page = tabs.getCurrentContentComponent();
        if (page == nullptr) continue;
        if (auto* st = dynamic_cast<SubTabs*>(page)) {
            const int was = st->current();
            for (int j = 0; j < st->count(); ++j) {
                st->show(j);
                out << pageLines(*st, names[i] + ": " + st->name(j));
            }
            st->show(was);
        } else {
            out << pageLines(*page, names[i]);
        }
    }
    tabs.setCurrentTabIndex(front, false);
    return out;
}

// ========================================================================================================= controls

void ControlActions::showMenu(juce::Component& target, int id) const
{
    juce::PopupMenu m;
    if (describe) m.addSectionHeader(describe(id));
    const int cc = controllerFor ? controllerFor(id) : -1;
    if (learn) m.addItem(cc >= 0 ? "MIDI learn (now CC " + juce::String(cc) + ") ..." : juce::String("MIDI learn ..."), [f = learn, id] { f(id); });
    if (forget && cc >= 0) m.addItem("Forget CC " + juce::String(cc), [f = forget, id] { f(id); });
    if (reset) m.addItem("Default value", [f = reset, id] { f(id); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target));
}

Knob::Knob(SliderStyle style, TextEntryBoxPosition box, const ControlActions* actions, int id)
    : juce::Slider(style, box), actions_(actions), id_(id)
{
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && actions_ != nullptr) { actions_->showMenu(*this, id_); return; }
    juce::Slider::mouseDown(e);
}

void Choice::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && actions_ != nullptr) { actions_->showMenu(*this, id_); return; }
    juce::ComboBox::mouseDown(e);
}

void Switch::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && actions_ != nullptr) { actions_->showMenu(*this, id_); return; }
    juce::ToggleButton::mouseDown(e);
}

void Switch::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && actions_ != nullptr) return;   // the menu, not a click
    juce::ToggleButton::mouseUp(e);
}

LiveRings::LiveRings(std::function<float(int)> playedNormalised) : played_(std::move(playedNormalised)) { startTimerHz(15); }

void LiveRings::add(juce::Slider& slider, int id) { items_.emplace_back(&slider, id); }

void LiveRings::timerCallback()
{
    for (auto& [s, id] : items_) {
        juce::Slider* sl = s.getComponent();
        if (sl == nullptr || !sl->isShowing()) continue;
        const float v = played_(id);
        auto& props = sl->getProperties();
        const juce::var was = props["live"];
        if (std::isnan(v)) {
            if (!was.isVoid()) { props.remove("live"); sl->repaint(); }
            continue;
        }
        if (was.isVoid() || std::abs(static_cast<float>(static_cast<double>(was)) - v) > 0.002f) {
            props.set("live", v);
            sl->repaint();
        }
    }
}

// ========================================================================================================== console

namespace {
constexpr float kTopDb = 6.0f;   ///< the top of the strips' meter scale, dB
constexpr float kFloorDb = -60.0f;   ///< the floor of the strips' meter scale, dB
/** @brief A linear amplitude in dB, -100 for silence. */
float toDb(float linear) { return linear > 1.0e-5f ? 20.0f * std::log10(linear) : -100.0f; }
} // namespace

ChannelStrip::ChannelStrip(const Skin& skin, const juce::String& name, juce::Colour colour, Control fader, std::vector<Control> knobs,
                           const ControlActions* actions, LiveRings* live)
    : skin_(skin), name_(name), colour_(colour)
{
    if (fader.param != nullptr) {
        fader_ = std::make_unique<Knob>(juce::Slider::LinearVertical, juce::Slider::NoTextBox, actions, fader.id);
        fader_->setColour(juce::Slider::trackColourId, colour_.withAlpha(0.8f));
        fader_->setColour(juce::Slider::thumbColourId, skin_.ink);
        fader_->setColour(juce::Slider::backgroundColourId, skin_.bg);
        fader_->setPopupDisplayEnabled(true, true, nullptr);
        fader_->setTooltip(fader.label);
        addAndMakeVisible(*fader_);
        faderLink_ = std::make_unique<juce::SliderParameterAttachment>(*fader.param, *fader_);
        fader_->setDoubleClickReturnValue(true, fader.param->convertFrom0to1(fader.param->getDefaultValue()));
        if (live != nullptr) live->add(*fader_, fader.id);
    }
    for (const Control& c : knobs) {
        if (c.param == nullptr) continue;
        auto k = std::make_unique<Knob>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox, actions, c.id);
        k->setColour(juce::Slider::rotarySliderFillColourId, colour_);
        k->setPopupDisplayEnabled(true, true, nullptr);
        k->setTooltip(c.label);
        addAndMakeVisible(*k);
        links_.push_back(std::make_unique<juce::SliderParameterAttachment>(*c.param, *k));
        k->setDoubleClickReturnValue(true, c.param->convertFrom0to1(c.param->getDefaultValue()));
        if (live != nullptr) live->add(*k, c.id);
        auto l = std::make_unique<juce::Label>(juce::String(), c.label);
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::textColourId, skin_.dim);
        l->setFont(font(10.0f));
        l->setMinimumHorizontalScale(0.7f);
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
        isSend_.push_back(c.send);
        knobs_.push_back(std::move(k));
        names_.push_back(std::move(l));
    }
}

void ChannelStrip::setSound(const juce::String& s)
{
    if (s == sound_) return;
    sound_ = s;
    setTooltip(s);
    repaint();
}

void ChannelStrip::setHeadButton(juce::Component* b)
{
    head_ = b;
    if (b != nullptr) addAndMakeVisible(b);
    resized();
}

void ChannelStrip::meter(float peak, float rms, double seconds)
{
    const float pDb = toDb(peak), rDb = toDb(rms);
    const float a = static_cast<float>(std::exp(-seconds / 0.3));
    rmsDb_ = rDb >= rmsDb_ ? rDb : juce::jmax(rDb, kFloorDb - 1.0f + (rmsDb_ - (kFloorDb - 1.0f)) * a);
    if (pDb >= holdDb_) { holdDb_ = pDb; holdAge_ = 0.0; }
    else {
        holdAge_ += seconds;
        if (holdAge_ > 1.5) holdDb_ = juce::jmax(pDb, holdDb_ - static_cast<float>(20.0 * seconds));
    }
    repaint(meterArea_.expanded(2).withBottom(getHeight()));
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced(3);
    r.removeFromTop(32);   // the name, and the sound under it
    if (head_ != nullptr) head_->setBounds(r.removeFromTop(22).reduced(4, 1));
    // Folded, only the pan above the fader; unfolded, the sends too. Every strip keeps the same room, so the faders and
    // the meters line up across the console.
    const int slots = sends_ ? 6 : 1;
    const int lh = 12, kh = juce::jlimit(20, 42, std::min(r.getWidth() - 18, (r.getHeight() - 150) / slots - lh));
    int used = 0;
    for (size_t i = 0; i < knobs_.size(); ++i) {
        const bool show = sends_ || !isSend_[i];
        knobs_[i]->setVisible(show && used < slots);
        names_[i]->setVisible(show && used < slots);
        if (!show || used >= slots) continue;
        auto row = r.removeFromTop(kh + lh);
        knobs_[i]->setBounds(row.removeFromTop(kh));
        names_[i]->setBounds(row);
        ++used;
    }
    r.removeFromTop(std::max(0, slots - used) * (kh + lh));
    r.removeFromTop(6);
    r.removeFromBottom(15);   // the peak readout
    if (fader_ != nullptr) fader_->setBounds(r.removeFromLeft(r.getWidth() / 2));
    else r.removeFromLeft(r.getWidth() / 4);
    meterArea_ = r.reduced(4, 6);
}

void ChannelStrip::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(skin_.group);
    g.fillRoundedRectangle(b, skin_.radius);
    g.setColour(colour_);
    g.fillRoundedRectangle(b.withHeight(3.0f), 1.5f);
    g.setColour(skin_.ink);
    g.setFont(font(12.5f, true));
    g.drawFittedText(name_, getLocalBounds().withHeight(22).reduced(2, 0), juce::Justification::centred, 1);
    if (sound_.isNotEmpty()) {
        g.setColour(skin_.dim);
        g.setFont(font(10.0f));
        g.drawFittedText(sound_, getLocalBounds().withTrimmedTop(18).withHeight(14).reduced(3, 0), juce::Justification::centred, 1, 0.8f);
    }
    // The meter: -60 .. +6 dB, the RMS bar, the held peak, 0 dBFS marked.
    const auto m = meterArea_.toFloat();
    g.setColour(skin_.bg);
    g.fillRect(m);
    auto yOf = [&](float db) {
        const float t = (juce::jlimit(kFloorDb, kTopDb, db) - kFloorDb) / (kTopDb - kFloorDb);
        return m.getBottom() - t * m.getHeight();
    };
    if (rmsDb_ > kFloorDb) {
        const float top = yOf(rmsDb_), y6 = yOf(-6.0f), y0 = yOf(0.0f);
        g.setColour(skin_.good.withAlpha(0.85f));
        g.fillRect(m.getX(), std::max(top, y6), m.getWidth(), m.getBottom() - std::max(top, y6));
        if (top < y6) {
            g.setColour(skin_.accent.withAlpha(0.85f));
            g.fillRect(m.getX(), std::max(top, y0), m.getWidth(), y6 - std::max(top, y0));
        }
        if (top < y0) {
            g.setColour(skin_.bad);
            g.fillRect(m.getX(), top, m.getWidth(), y0 - top);
        }
    }
    if (holdDb_ > kFloorDb) {
        g.setColour(holdDb_ > 0.0f ? skin_.bad : skin_.ink);
        g.fillRect(m.getX(), yOf(holdDb_) - 1.0f, m.getWidth(), 2.0f);
    }
    g.setColour(skin_.faint);
    g.fillRect(m.getX() - 3.0f, yOf(0.0f), 3.0f, 1.0f);
    g.setColour(holdDb_ > 0.0f ? skin_.bad : skin_.dim);
    g.setFont(font(10.0f));
    g.drawText(holdDb_ > kFloorDb ? juce::String(holdDb_, 1) : juce::String("-inf"), getLocalBounds().removeFromBottom(17), juce::Justification::centred);
}

Console::Console(const Skin& skin) : skin_(skin)
{
    fold_.setClickingTogglesState(true);
    fold_.setTooltip("Unfold every strip's sends, or fold them away");
    fold_.setWantsKeyboardFocus(false);
    fold_.onClick = [this] {
        sends_ = fold_.getToggleState();
        for (auto& s : strips_) s->showSends(sends_);
    };
    addAndMakeVisible(fold_);
    view_.setViewedComponent(&inner_, false);
    view_.setScrollBarsShown(false, true);
    addAndMakeVisible(view_);
}

ChannelStrip& Console::add(std::unique_ptr<ChannelStrip> strip)
{
    inner_.addAndMakeVisible(*strip);
    strip->showSends(sends_);
    strips_.push_back(std::move(strip));
    resized();
    return *strips_.back();
}

void Console::resized()
{
    auto r = getLocalBounds().reduced(6, 4);
    fold_.setBounds(r.removeFromTop(26).removeFromLeft(110));
    r.removeFromTop(6);
    view_.setBounds(r);
    if (strips_.empty()) return;
    const int n = size();
    const bool scroll = r.getWidth() / n < kMinStrip;
    const int w = scroll ? kMinStrip : r.getWidth() / n;
    const int h = r.getHeight() - (scroll ? view_.getScrollBarThickness() : 0);
    inner_.setSize(w * n, h);
    for (int i = 0; i < n; ++i) strips_[static_cast<size_t>(i)]->setBounds(i * w, 0, w - 3, h);
}

// ========================================================================================================== headset

Headset::Headset() { receiver_.addListener(this); }

Headset::~Headset()
{
    receiver_.removeListener(this);
    receiver_.disconnect();
}

void Headset::listen(int port)
{
    if (port == port_ && (port == 0 || error_.isEmpty())) return;
    receiver_.disconnect();
    port_ = port;
    error_ = {};
    if (port <= 0) return;
    if (!receiver_.connect(port)) error_ = "port " + juce::String(port) + " is taken";
}

bool Headset::active() const { return juce::Time::getMillisecondCounterHiRes() - last_ < 3000.0; }

void Headset::oscMessageReceived(const juce::OSCMessage& m)
{
    if (m.getAddressPattern().toString() != "/hands" || m.size() < 6) return;
    auto f = [&](int i) { return m[i].isFloat32() ? m[i].getFloat32() : m[i].isInt32() ? static_cast<float>(m[i].getInt32()) : 0.0f; };
    Hands h;
    for (int i = 0; i < 2; ++i) {
        h.height[i] = juce::jlimit(0.0f, 1.0f, f(i));
        h.pinch[i] = f(2 + i) > 0.5f;
        h.tracked[i] = f(4 + i) > 0.5f;
    }
    inject(h);
}

void Headset::inject(const Hands& h)
{
    hands_ = h;
    ++messages_;
    last_ = juce::Time::getMillisecondCounterHiRes();
    source_ = "the headset";
}

HeadsetEvents Headset::poll()
{
    // The grammar of the Quest apps (their main.cpp, the same rules and the same numbers): both hands closed together
    // fire the next one at once and spoil both pinches; a single short pinch acts when it opens again, so it never also
    // counts as part of a pinch of both; the right one held 0.6 s fires its second action and is spoiled too; a hand
    // moves its control only while it does not pinch; both controls glide (0.15 s) and are centred, so a hand at mid
    // height plays what was composed: the filter with a dead zone of 0.1 round the middle, the throw from the middle up.
    HeadsetEvents e;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastPoll_ > 0.0 ? std::min(0.2, (now - lastPoll_) / 1000.0) : 0.0;
    lastPoll_ = now;
    if (!active()) {
        was_[0] = was_[1] = both_ = false;
        spoiled_[0] = spoiled_[1] = true;
        if (holding_) { holding_ = false; e.holdEnded = true; }
        return e;
    }
    const Hands h = hands_;
    const bool closed[2] = { h.tracked[0] && h.pinch[0], h.tracked[1] && h.pinch[1] };
    if (closed[0] && closed[1]) {
        spoiled_[0] = spoiled_[1] = true;
        if (!both_) { e.next = true; both_ = true; }
    }
    for (int i = 0; i < 2; ++i) {
        if (closed[i] && !was_[i] && !closed[1 - i]) { spoiled_[i] = false; since_[i] = now; }
        if (i == 1 && closed[1] && !spoiled_[1] && now - since_[1] >= 600.0) {
            spoiled_[1] = true;
            e.hold = true;
            holding_ = true;
        }
        if (i == 1 && !closed[1] && holding_) { holding_ = false; e.holdEnded = true; }
        if (!closed[i] && was_[i] && !spoiled_[i]) {
            if (i == 0) e.playStop = true;
            else e.action = true;
        }
        was_[i] = closed[i];
    }
    if (!closed[0] && !closed[1]) both_ = false;
    const float a = dt > 0.0 ? static_cast<float>(1.0 - std::exp(-dt / 0.15)) : 1.0f;
    // The left hand: below the middle a low pass, above it a high pass, a dead zone round the middle.
    float ft = 0.0f;
    if (h.tracked[0] && !h.pinch[0]) {
        const float d = (h.height[0] - 0.5f) * 2.0f;
        ft = std::abs(d) < 0.1f ? 0.0f : juce::jlimit(-1.0f, 1.0f, (d - std::copysign(0.1f, d)) / 0.9f);
    } else if (h.tracked[0]) {
        ft = filter_;   // pinching: held where it was
    }
    // The right hand: the throw from the middle up.
    float tt = 0.0f;
    if (h.tracked[1] && !h.pinch[1]) tt = juce::jlimit(0.0f, 1.0f, 2.0f * (h.height[1] - 0.5f));
    else if (h.tracked[1]) tt = throw_;
    const float nf = filter_ + a * (ft - filter_), nt = throw_ + a * (tt - throw_);
    if (std::abs(nf - filter_) > 1.0e-4f || (nf == 0.0f) != (filter_ == 0.0f)) e.filterMoved = true;
    if (std::abs(nt - throw_) > 1.0e-4f || (nt == 0.0f) != (throw_ == 0.0f)) e.throwMoved = true;
    filter_ = std::abs(nf) < 1.0e-3f ? 0.0f : nf;
    throw_ = nt < 1.0e-3f ? 0.0f : nt;
    e.filter = filter_;
    e.throwAmount = throw_;
    return e;
}

juce::String Headset::statusText() const
{
    if (port_ <= 0) return "the headset: not listening (Off)";
    if (error_.isNotEmpty()) return "the headset: " + error_;
    if (active()) return "the headset sends its hands (port " + juce::String(port_) + ")";
    if (everSeen()) return "the headset is quiet (port " + juce::String(port_) + ")";
    return "listening for a headset on port " + juce::String(port_);
}

std::vector<std::pair<juce::String, juce::String>> headsetGrammarShort(const juce::String& action, const juce::String& hold,
                                                                       const juce::String& left, const juce::String& right)
{
    std::vector<std::pair<juce::String, juce::String>> t = { { "left pinch", "play / stop" }, { "both pinched", "the next one" },
                                                              { "right pinch", action } };
    if (hold.isNotEmpty()) t.emplace_back("right held", hold);
    t.emplace_back("left hand", left);
    t.emplace_back("right hand", right);
    return t;
}

void drawHeadsetBox(juce::Graphics& g, juce::Rectangle<int> area, const Skin& skin, const Headset& headset,
                    const juce::String& action, const juce::String& hold, const juce::String& left, const juce::String& right,
                    bool framed)
{
    auto h = area.reduced(framed ? 6 : 0, 0);
    if (framed) {   // a box of its own; inside a page's group (Phosphene's) the group is the box
        g.setColour(skin.group);
        g.fillRoundedRectangle(h.toFloat(), skin.radius);
        g.setColour(skin.edge);
        g.drawRoundedRectangle(h.toFloat().reduced(0.5f), skin.radius, 1.0f);
        h = h.reduced(12, 8);
        g.setColour(skin.family(Family::Motion));
        g.setFont(font(11.5f, true));
        g.drawText("HEADSET", h.removeFromTop(16), juce::Justification::centredLeft);
    }
    g.setColour(skin.dim);
    g.setFont(font(11.5f));
    g.drawFittedText(headset.statusText(), h.removeFromTop(18), juce::Justification::centredLeft, 1, 0.8f);
    h.removeFromTop(4);
    // The hands: a slot each, the dot on its height, lit while it pinches.
    const Hands hands = headset.hands();
    const bool live = headset.active();
    auto bars = h.removeFromLeft(96);
    for (int i = 0; i < 2; ++i) {
        auto col = bars.removeFromLeft(48).reduced(6, 0);
        g.setColour(skin.dim);
        g.drawText(i == 0 ? "left" : "right", col.removeFromBottom(16), juce::Justification::centred);
        const auto track = col.withSizeKeepingCentre(10, col.getHeight()).toFloat();
        g.setColour(skin.faint);
        g.fillRoundedRectangle(track, 5.0f);
        if (live && hands.tracked[i]) {
            const float y = track.getBottom() - hands.height[i] * track.getHeight();
            g.setColour(hands.pinch[i] ? skin.onset : skin.accent);
            g.fillEllipse(track.getCentreX() - 7.0f, y - 7.0f, 14.0f, 14.0f);
        }
    }
    // The grammar.
    h.removeFromLeft(8);
    const auto lines = headsetGrammarShort(action, hold, left, right);
    const int lh = juce::jlimit(12, 16, h.getHeight() / std::max(1, static_cast<int>(lines.size())));
    g.setFont(font(std::min(12.0f, static_cast<float>(lh) - 1.0f)));
    for (const auto& [gesture, effect] : lines) {
        auto row = h.removeFromTop(lh);
        g.setColour(skin.dim);
        g.drawText(gesture, row.removeFromLeft(80), juce::Justification::centredLeft);
        g.setColour(skin.ink);
        g.drawFittedText(effect, row, juce::Justification::centredLeft, 1, 0.75f);
    }
}

juce::String headsetGrammar(const juce::String& action, const juce::String& hold)
{
    juce::String t;
    t << "left pinch\tplay / stop\n"
      << "both hands pinched\tthe next one, from a new seed\n"
      << "right pinch\t" << action << "\n";
    if (hold.isNotEmpty()) t << "right pinch held\t" << hold << "\n";
    t << "left hand up or down\tthe master filter: low pass below the middle, high pass above\n"
      << "right hand up\tthe echo throw\n\n"
      << "A hand moves its control only while it does not pinch; both glide and are centred, so a hand at mid height "
         "plays what was composed. The Quest app sends its hands to this computer in bridge mode: bridge_host = this "
         "computer's address in the app's config file (audio=0 leaves the headset silent).";
    return t;
}

// ---------------------------------------------------------------------------------------------------------------------
// The keyboard's options (02.10.2026).

uint16_t scaleMask(const juce::String& name)
{
    static const std::pair<const char*, std::array<int, 8>> kScales[] = {
        { "aeolian", { 0, 2, 3, 5, 7, 8, 10, -1 } },          { "minor", { 0, 2, 3, 5, 7, 8, 10, -1 } },
        { "dorian", { 0, 2, 3, 5, 7, 9, 10, -1 } },           { "phrygian", { 0, 1, 3, 5, 7, 8, 10, -1 } },
        { "harmonic minor", { 0, 2, 3, 5, 7, 8, 11, -1 } },   { "minor pentatonic", { 0, 3, 5, 7, 10, -1, -1, -1 } },
        { "ionian", { 0, 2, 4, 5, 7, 9, 11, -1 } },           { "major", { 0, 2, 4, 5, 7, 9, 11, -1 } },
        { "mixolydian", { 0, 2, 4, 5, 7, 9, 10, -1 } },       { "lydian", { 0, 2, 4, 6, 7, 9, 11, -1 } },
        { "locrian", { 0, 1, 3, 5, 6, 8, 10, -1 } },          { "phrygian dominant", { 0, 1, 4, 5, 7, 8, 10, -1 } },
        { "double harmonic", { 0, 1, 4, 5, 7, 8, 11, -1 } },  { "hexachord", { 0, 2, 3, 5, 7, 10, -1, -1 } },
    };
    const juce::String n = name.trim().toLowerCase();
    for (const auto& [key, steps] : kScales)
        if (n == key) {
            uint16_t m = 0;
            for (int s : steps) if (s >= 0) m = static_cast<uint16_t>(m | (1u << s));
            return m;
        }
    return 0x0FFF;
}

int snapToScale(int pitch, int root, uint16_t mask)
{
    if ((mask & 0x0FFF) == 0x0FFF || (mask & 0x0FFF) == 0) return pitch;
    auto in = [&](int p) { return ((mask >> (((p - root) % 12 + 12) % 12)) & 1u) != 0; };
    for (int d = 0; d < 12; ++d) {
        if (pitch - d >= 0 && in(pitch - d)) return pitch - d;   // a tie goes down
        if (pitch + d <= 127 && in(pitch + d)) return pitch + d;
    }
    return pitch;
}

int shapeVelocity(int velocity, int curve)
{
    const int v = std::clamp(velocity, 1, 127);
    if (curve == 3) return 100;
    if (curve != 1 && curve != 2) return v;
    const double x = static_cast<double>(v) / 127.0;
    return std::clamp(static_cast<int>(std::lround(127.0 * std::pow(x, curve == 1 ? 0.6 : 1.7))), 1, 127);
}

void KeyMemory::clear()
{
    for (auto& row : target_) for (auto& t : row) t = -2;
    for (auto& row : pitch_) for (auto& p : row) p = 0;
}

void KeyMemory::press(int channel, int key, int target, int pitch)
{
    if (channel < 0 || channel > 15 || key < 0 || key > 127) return;
    target_[channel][key] = static_cast<int16_t>(target);
    pitch_[channel][key] = static_cast<int16_t>(pitch);
}

bool KeyMemory::release(int channel, int key, int& target, int& pitch)
{
    if (channel < 0 || channel > 15 || key < 0 || key > 127 || target_[channel][key] == -2) return false;
    target = target_[channel][key];
    pitch = pitch_[channel][key];
    target_[channel][key] = -2;
    return true;
}

} // namespace frame
