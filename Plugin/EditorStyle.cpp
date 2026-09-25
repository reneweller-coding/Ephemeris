/**
 * @file EditorStyle.cpp
 * @brief The style page (EditorStyle.h).
 */
#include "EditorStyle.h"
#include "eph/compose/Style.h"
#include <functional>

using namespace eph;

namespace {
const juce::Colour kBack(0xff1e2129), kInk(0xffd8d4c8), kDim(0xff8a8f99), kFaint(0xff2b2f38), kAccent(0xffc9a45c);

juce::String percent(float x) { return juce::String(juce::roundToInt(100.0f * x)) + " %"; }
juce::String range(double a, double b, int decimals = 0)
{
    return a == b ? juce::String(a, decimals) : juce::String(a, decimals) + " - " + juce::String(b, decimals);
}
juce::String division(RowDivision d)
{
    switch (d) {
    case RowDivision::Quarter: return "1/4";
    case RowDivision::Eighth: return "1/8";
    case RowDivision::EighthT: return "1/8T";
    case RowDivision::Sixteenth: return "1/16";
    case RowDivision::SixteenthT: return "1/16T";
    case RowDivision::ThirtySecond: return "1/32";
    case RowDivision::Bar1: return "1 bar";
    case RowDivision::Bars2: return "2 bars";
    case RowDivision::Bars4: return "4 bars";
    default: return "?";
    }
}

/** @brief One line of the table: its label and how a profile says it. */
struct Line {
    const char* label;
    std::function<juce::String(const StyleProfile&)> value;
};

const std::vector<Line>& lines()
{
    static const std::vector<Line> l = {
        { "Tempo (BPM)",            [](const StyleProfile& p) { return range(p.bpmLow, p.bpmHigh); } },
        { "Piece in a concert (min)", [](const StyleProfile& p) { return range(p.minutesLow, p.minutesHigh); } },
        { "Sequence phases",        [](const StyleProfile& p) { return range(p.phasesLow, p.phasesHigh); } },
        { "Atmosphere before / coda after", [](const StyleProfile& p) { return percent(p.introShare) + " / " + percent(p.codaShare); } },
        { "New tempo / new key in a phase", [](const StyleProfile& p) { return percent(p.newTempoChance) + " / " + percent(p.newKeyChance); } },
        { "Rows at the peak",       [](const StyleProfile& p) { return juce::String(p.peakRows); } },
        { "Counter row lengths",    [](const StyleProfile& p) { juce::String s; for (int n : p.counterLengths) s << n << " "; return s.trim(); } },
        { "Mutation per cycle",     [](const StyleProfile& p) { return percent(p.mutation); } },
        { "Transposer",             [](const StyleProfile& p) { return juce::String(p.transposerLength) + " x " + division(p.transposerDivision); } },
        { "Tape keys",              [](const StyleProfile& p) { return percent(p.tapeChance); } },
        { "String machine",         [](const StyleProfile& p) { return percent(p.stringsChance); } },
        { "Lead section",           [](const StyleProfile& p) { return percent(p.leadChance); } },
        { "Bleeps",                 [](const StyleProfile& p) { return percent(p.bleepChance); } },
        { "Drums",                  [](const StyleProfile& p) { return percent(p.drumsChance); } },
        { "Tape set",               [](const StyleProfile& p) { return juce::String(kTapeSetNames[static_cast<int>(p.tape)]); } },
        { "Lead density",           [](const StyleProfile& p) { return juce::String(p.leadIntensity, 2); } },
        { "Hands: a move / a rest", [](const StyleProfile& p) { return juce::String(p.hands.medianSeconds, 0) + " s / " + juce::String(p.hands.restSeconds, 0) + " s"; } },
        { "Darkness",               [](const StyleProfile& p) { return juce::String(p.darkness, 2); } },
        { "Hall",                   [](const StyleProfile& p) { return juce::String(p.hallSeconds, 1) + " s"; } },
        { "Level",                  [](const StyleProfile& p) { return juce::String(p.levelDb, 1) + " dB"; } },
    };
    return l;
}
} // namespace

StylePage::StylePage(EphemerisProcessor& p) : proc_(p)
{
    startTimerHz(4);
}

StylePage::~StylePage() { stopTimer(); }

void StylePage::timerCallback()
{
    const ParamStore& s = proc_.store();
    const int style = s.getInt(s.id(Module::Compose, 0, compose::Style));
    if (style != style_) { style_ = style; repaint(); }
}

void StylePage::paint(juce::Graphics& g)
{
    g.fillAll(kBack);
    const int styles = static_cast<int>(Style::Count);
    auto area = getLocalBounds().toFloat().reduced(16.0f, 10.0f);
    const float labelW = 240.0f, colW = (area.getWidth() - labelW) / static_cast<float>(styles);
    const auto& rows = lines();
    const float rowH = std::min(22.0f, (area.getHeight() - 60.0f) / static_cast<float>(rows.size() + 1));
    // The chosen style's column.
    if (style_ >= 0 && style_ < styles) {
        g.setColour(kAccent.withAlpha(0.12f));
        g.fillRoundedRectangle(area.getX() + labelW + colW * static_cast<float>(style_), area.getY(), colW, rowH * static_cast<float>(rows.size() + 1) + 4.0f, 4.0f);
    }
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    for (int s = 0; s < styles; ++s) {
        g.setColour(s == style_ ? kAccent : kInk);
        g.drawText(styleProfile(static_cast<Style>(s)).name, juce::Rectangle<float>(area.getX() + labelW + colW * static_cast<float>(s), area.getY(), colW, rowH),
                   juce::Justification::centred);
    }
    g.setFont(juce::Font(juce::FontOptions(12.5f)));
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = area.getY() + rowH * static_cast<float>(i + 1);
        if (i % 2 == 0) { g.setColour(kFaint.withAlpha(0.5f)); g.fillRect(area.getX(), y, area.getWidth(), rowH); }
        g.setColour(kDim);
        g.drawText(rows[i].label, juce::Rectangle<float>(area.getX() + 4.0f, y, labelW - 8.0f, rowH), juce::Justification::centredLeft);
        for (int s = 0; s < styles; ++s) {
            g.setColour(s == style_ ? kInk : kInk.withAlpha(0.75f));
            g.drawText(rows[i].value(styleProfile(static_cast<Style>(s))),
                       juce::Rectangle<float>(area.getX() + labelW + colW * static_cast<float>(s), y, colW, rowH), juce::Justification::centred);
        }
    }
    g.setColour(kDim);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    const float y = area.getY() + rowH * static_cast<float>(rows.size() + 1) + 14.0f;
    g.drawFittedText("A style is a set of ranges and chances the composer draws from: two pieces of one style share these, "
                     "not their notes. The tempo comes from the style while compose.style_tempo is on. The tempi and the "
                     "levels were set after measuring reference recordings (Tools/analyze_ref.py).",
                     juce::Rectangle<float>(area.getX(), y, area.getWidth(), 40.0f).toNearestInt(), juce::Justification::topLeft, 3);
}
