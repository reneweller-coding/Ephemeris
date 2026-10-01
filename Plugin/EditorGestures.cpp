/**
 * @file EditorGestures.cpp
 * @brief The gestures page (EditorGestures.h).
 */
#include "EditorGestures.h"
#include "EditorTheme.h"
#include <algorithm>
#include <cmath>
#include <map>

using namespace eph;

namespace {
const juce::Colour kBack = ephui::colour::bg;   ///< the window: midnight
const juce::Colour kInk = ephui::colour::ink;   ///< text: parchment
const juce::Colour kDim = ephui::colour::dim;   ///< names, secondary text
const juce::Colour kFaint = ephui::colour::edge;   ///< hairlines
const juce::Colour kPlayhead = ephui::colour::amber;   ///< the sun: the accent
const juce::Colour kHand[2] = { ephui::familyColour(ephui::Family::Source), ephui::familyColour(ephui::Family::Motion) };   ///< left hand warm, right hand cool
constexpr float kNameWidth = 150.0f;   ///< the lanes' names' column, px
constexpr float kHandsHeight = 22.0f;   ///< the hands' strip at the top, px
}

GestureView::GestureView(EphemerisProcessor& p) : proc_(p)
{
    startTimerHz(10);
}

GestureView::~GestureView() { stopTimer(); }

void GestureView::timerCallback()
{
    if (proc_.scoreVersion() != version_) {
        version_ = proc_.scoreVersion();
        proc_.copyScore(score_);
        rebuild();
    }
    repaint();
}

void GestureView::resized() { rebuild(); }

float GestureView::xOf(double beat) const
{
    const double len = std::max(1.0, score_.lengthBeats);
    return area_.getX() + static_cast<float>(beat / len) * area_.getWidth();
}

void GestureView::rebuild()
{
    area_ = getLocalBounds().toFloat().reduced(8.0f).withTrimmedLeft(kNameWidth).withTrimmedTop(kHandsHeight + 6.0f);
    std::map<int, Lane> byParam;
    for (const Gesture& g : score_.gestures) {
        if (g.param < 0) continue;
        Lane& l = byParam[g.param];
        if (l.gestures.empty()) { l.param = g.param; l.hand = g.hand & 1; }
        l.gestures.push_back(g);
    }
    lanes_.clear();
    const ParamStore& s = proc_.store();
    for (auto& [param, lane] : byParam) {
        if (param < s.count()) lane.name = juce::String(s.key(param));
        std::stable_sort(lane.gestures.begin(), lane.gestures.end(), [](const Gesture& a, const Gesture& b) { return a.beat < b.beat; });
        lanes_.push_back(std::move(lane));
    }
    // The curves at this size: the offset the engine plays at each pixel (the latest gesture that has started).
    curves_.assign(lanes_.size(), juce::Path());
    if (lanes_.empty() || area_.getWidth() < 10.0f) return;
    const float laneH = area_.getHeight() / static_cast<float>(lanes_.size());
    const int steps = static_cast<int>(area_.getWidth());
    for (size_t k = 0; k < lanes_.size(); ++k) {
        const Lane& l = lanes_[k];
        const float mid = area_.getY() + laneH * (static_cast<float>(k) + 0.5f), half = laneH * 0.42f;
        size_t cur = 0;
        bool started = false;
        for (int i = 0; i <= steps; ++i) {
            const double beat = score_.lengthBeats * i / steps;
            while (cur + 1 < l.gestures.size() && l.gestures[cur + 1].beat <= beat) ++cur;
            const float v = l.gestures[cur].beat <= beat ? gestureValue(l.gestures[cur], beat) : 0.0f;
            const float x = area_.getX() + static_cast<float>(i), y = mid - half * std::clamp(v, -1.0f, 1.0f);
            if (!started) { curves_[k].startNewSubPath(x, y); started = true; }
            else curves_[k].lineTo(x, y);
        }
    }
}

void GestureView::paint(juce::Graphics& g)
{
    g.fillAll(kBack);
    if (lanes_.empty()) {
        g.setColour(kDim);
        g.drawText("No gestures in this piece.", getLocalBounds(), juce::Justification::centred);
        return;
    }
    // The sections behind everything, as faint bands with their names on top.
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    for (size_t i = 0; i < score_.markers.size(); ++i) {
        const double b0 = score_.markers[i].beat;
        const double b1 = i + 1 < score_.markers.size() ? score_.markers[i + 1].beat : score_.lengthBeats;
        const float x0 = xOf(b0), x1 = xOf(b1);
        if (i % 2 == 1) { g.setColour(kFaint.withAlpha(0.35f)); g.fillRect(x0, area_.getY(), x1 - x0, area_.getHeight()); }
        g.setColour(kFaint.brighter(0.6f));
        g.drawVerticalLine(juce::roundToInt(x0), area_.getY(), area_.getBottom());
    }
    // The hands: where each is busy.
    for (int h = 0; h < 2; ++h) {
        const float y = 8.0f + static_cast<float>(h) * (kHandsHeight * 0.5f);
        g.setColour(kDim);
        g.drawText(h == 0 ? "player, left" : "player, right", juce::Rectangle<float>(8.0f, y - 2.0f, kNameWidth - 12.0f, kHandsHeight * 0.5f),
                   juce::Justification::centredRight);
        g.setColour(kHand[h].withAlpha(0.8f));
        for (const Gesture& ge : score_.gestures)
            if ((ge.hand & 1) == h && ge.length > 0.0)
                g.fillRect(juce::Rectangle<float>(xOf(ge.beat), y, std::max(1.0f, xOf(ge.beat + ge.length) - xOf(ge.beat)), kHandsHeight * 0.5f - 3.0f));
    }
    // The lanes.
    const float laneH = area_.getHeight() / static_cast<float>(lanes_.size());
    for (size_t k = 0; k < lanes_.size(); ++k) {
        const float top = area_.getY() + laneH * static_cast<float>(k), mid = top + laneH * 0.5f;
        g.setColour(kFaint);
        g.drawHorizontalLine(juce::roundToInt(mid), area_.getX(), area_.getRight());
        g.setColour(kInk);
        g.setFont(juce::Font(juce::FontOptions(std::min(12.0f, laneH * 0.7f))));
        g.drawText(lanes_[k].name, juce::Rectangle<float>(8.0f, top, kNameWidth - 12.0f, laneH), juce::Justification::centredRight);
        g.setColour(kHand[lanes_[k].hand].withAlpha(0.9f));
        g.strokePath(curves_[k], juce::PathStrokeType(1.5f));
    }
    // The playhead.
    const float x = xOf(proc_.positionBeats());
    g.setColour(kPlayhead);
    g.drawLine(x, 4.0f, x, area_.getBottom(), 1.5f);
}

void GestureView::mouseDown(const juce::MouseEvent& e)
{
    if (area_.getWidth() <= 0.0f || e.position.x < area_.getX()) return;
    proc_.seekTo(std::clamp((e.position.x - area_.getX()) / area_.getWidth(), 0.0f, 1.0f) * score_.lengthBeats);
}
