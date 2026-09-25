/**
 * @file EditorOrrery.cpp
 * @brief The rack page and its orrery (EditorOrrery.h).
 */
#include "EditorOrrery.h"
#include "EditorTheme.h"
#include "PluginEditor.h"
#include "eph/Dsp.h"
#include <cmath>

using namespace eph;

namespace {

const juce::Colour kBack = ephui::colour::bg, kInk = ephui::colour::ink, kDim = ephui::colour::dim, kFaint = ephui::colour::faint, kSun = ephui::colour::amber;

/** @brief A colour per row, warm inside, cool outside. */
juce::Colour rowColour(int r)
{
    static const juce::uint32 c[8] = { 0xffe0a458, 0xffd9825b, 0xffc9a45c, 0xff9fbf6f, 0xff6fb8ae, 0xff6f8fb8, 0xffa58ac2, 0xffc47a9c };
    return juce::Colour(c[r & 7]);
}

const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** @brief A step length in beats as a note value. */
juce::String noteValue(double beats)
{
    struct V { double beats; const char* name; };
    static const V values[] = { { 4.0, "1 bar" }, { 1.0, "1/4" }, { 0.5, "1/8" }, { 1.0 / 3.0, "1/8T" },
                                { 0.25, "1/16" }, { 1.0 / 6.0, "1/16T" }, { 0.125, "1/32" } };
    for (const V& v : values) if (std::fabs(beats - v.beats) < 1e-6) return v.name;
    return juce::String(beats, 3) + " beats";
}

} // namespace

// ==================================================================== OrreryView

OrreryView::OrreryView(EphemerisProcessor& p) : proc_(p)
{
    startTimerHz(30);
}

OrreryView::~OrreryView() { stopTimer(); }

void OrreryView::timerCallback()
{
    if (proc_.scoreVersion() != version_) {
        version_ = proc_.scoreVersion();
        proc_.copyScore(score_);
    }
    repaint();
}

std::vector<OrreryView::RowState> OrreryView::rowsAt(double beat) const
{
    std::vector<RowState> rows(kRows);
    for (const RowShape& s : score_.rowShapes) {
        if (s.from > beat || s.row < 0 || s.row >= kRows) continue;
        RowState& r = rows[static_cast<size_t>(s.row)];
        r.known = true;
        r.length = s.length;
        r.divBeats = s.divBeats;
        r.transposer = s.transposer;
    }
    // Start and stop as Rack::run applies them: a row index below zero means every row.
    for (const RackEvent& e : score_.rack) {
        if (e.beat > beat) break;
        if (e.op != RackOp::Start && e.op != RackOp::Stop) continue;
        const int lo = e.row < 0 ? 0 : e.row, hi = e.row < 0 ? kRows - 1 : e.row;
        for (int i = lo; i <= hi && i < kRows; ++i) {
            RowState& r = rows[static_cast<size_t>(i)];
            r.running = e.op == RackOp::Start;
            if (r.running) r.start = e.beat;
        }
    }
    for (RowState& r : rows) {
        const double period = r.length * r.divBeats;
        if (r.known && r.running && period > 0.0) {
            const double t = (beat - r.start) / period;
            r.cycle = t - std::floor(t);
        }
    }
    return rows;
}

double OrreryView::nextConjunction(const std::vector<RowState>& rows, double beat)
{
    // Candidates are the returns of the longest running cycle; every other running row has to be on its
    // first step there too. Rows start on bars and steps are simple fractions of a beat, so a small
    // tolerance is exact enough.
    const RowState* longest = nullptr;
    int running = 0;
    for (const RowState& r : rows)
        if (r.known && r.running && !r.transposer) {
            ++running;
            if (longest == nullptr || r.length * r.divBeats > longest->length * longest->divBeats) longest = &r;
        }
    if (longest == nullptr || running < 2) return -1.0;
    const double period = longest->length * longest->divBeats;
    const double first = longest->start + std::ceil((beat - longest->start) / period + 1e-9) * period;
    for (int k = 0; k < 4000; ++k) {
        const double t = first + k * period;
        bool all = true;
        for (const RowState& r : rows) {
            if (!r.known || !r.running || r.transposer) continue;
            const double p = r.length * r.divBeats;
            const double q = (t - r.start) / p;
            if (std::fabs(q - std::round(q)) > 1e-6) { all = false; break; }
        }
        if (all) return t;
    }
    return -1.0;
}

void OrreryView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(8.0f);
    g.setColour(kBack);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 6.0f);
    const double beat = proc_.positionBeats();
    const std::vector<RowState> rows = rowsAt(beat);
    const float legend = 60.0f;
    const auto disc = area.withTrimmedBottom(legend);
    const juce::Point<float> c = disc.getCentre();
    const float outer = 0.5f * juce::jmin(disc.getWidth(), disc.getHeight()) - 6.0f;
    const float sunR = outer * 0.16f;

    // The orbits of the rows that play notes, the bass innermost.
    std::vector<int> order;
    for (int r = 0; r < kRows; ++r) if (rows[static_cast<size_t>(r)].known && !rows[static_cast<size_t>(r)].transposer) order.push_back(r);
    const int n = juce::jmax(1, static_cast<int>(order.size()));
    int onFirst = 0, runningRows = 0;
    for (int k = 0; k < static_cast<int>(order.size()); ++k) {
        const RowState& r = rows[static_cast<size_t>(order[static_cast<size_t>(k)])];
        const float radius = sunR + (outer - sunR) * (static_cast<float>(k) + 1.0f) / static_cast<float>(n);
        const juce::Colour col = rowColour(order[static_cast<size_t>(k)]);
        g.setColour(r.running ? col.withAlpha(0.35f) : kFaint);
        g.drawEllipse(c.x - radius, c.y - radius, 2.0f * radius, 2.0f * radius, r.running ? 1.4f : 1.0f);
        // A tick per step, the first one longer: the top of every orbit.
        for (int s = 0; s < r.length && r.length <= 32; ++s) {
            const float a = juce::MathConstants<float>::twoPi * static_cast<float>(s) / static_cast<float>(r.length) - juce::MathConstants<float>::halfPi;
            const float len = s == 0 ? 6.0f : 2.5f;
            g.setColour(r.running ? col.withAlpha(s == 0 ? 0.8f : 0.35f) : kFaint);
            g.drawLine(c.x + (radius - len) * std::cos(a), c.y + (radius - len) * std::sin(a),
                       c.x + (radius + len) * std::cos(a), c.y + (radius + len) * std::sin(a), s == 0 ? 1.6f : 1.0f);
        }
        if (!r.running) continue;
        ++runningRows;
        const int step = static_cast<int>(r.cycle * r.length);
        if (step == 0) ++onFirst;
        const float a = juce::MathConstants<float>::twoPi * static_cast<float>(r.cycle) - juce::MathConstants<float>::halfPi;
        const juce::Point<float> pl(c.x + radius * std::cos(a), c.y + radius * std::sin(a));
        g.setColour(col.withAlpha(0.25f));
        g.fillEllipse(pl.x - 9.0f, pl.y - 9.0f, 18.0f, 18.0f);
        g.setColour(col);
        g.fillEllipse(pl.x - 5.0f, pl.y - 5.0f, 10.0f, 10.0f);
    }

    // A conjunction: running rows together on their first step, a line of light from the sun upwards.
    if (runningRows >= 2 && onFirst >= 2) {
        const float strength = static_cast<float>(onFirst) / static_cast<float>(runningRows);
        g.setGradientFill(juce::ColourGradient(kSun.withAlpha(0.2f + 0.7f * strength), c.x, c.y - sunR,
                                               kSun.withAlpha(0.0f), c.x, c.y - outer - 6.0f, false));
        g.fillRect(juce::Rectangle<float>(c.x - 2.0f - 2.0f * strength, c.y - outer - 6.0f, 4.0f + 4.0f * strength, outer + 6.0f - sunR));
    }

    // The sun: the root the transposer puts the rows on.
    const RowState* tr = nullptr;
    for (const RowState& r : rows) if (r.known && r.transposer) tr = &r;
    const bool moving = tr != nullptr && tr->running;
    g.setColour(kSun.withAlpha(moving ? 0.35f : 0.15f));
    g.fillEllipse(c.x - sunR * 1.35f, c.y - sunR * 1.35f, sunR * 2.7f, sunR * 2.7f);
    g.setColour(kSun.withAlpha(moving ? 1.0f : 0.6f));
    g.fillEllipse(c.x - sunR, c.y - sunR, 2.0f * sunR, 2.0f * sunR);
    g.setColour(kBack);
    g.setFont(juce::Font(juce::FontOptions(sunR * 0.9f, juce::Font::bold)));
    const int root = pitchClass(score_.keyRoot + score_.rootAt(beat));
    g.drawText(kNoteNames[root], juce::Rectangle<float>(c.x - sunR, c.y - sunR, 2.0f * sunR, 2.0f * sunR), juce::Justification::centred);

    // The legend: each row's cycle, and when the running rows meet again.
    auto text = area.withTop(area.getBottom() - legend);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    juce::String line;
    for (int r : order) {
        const RowState& s = rows[static_cast<size_t>(r)];
        line << "Row " << (r + 1) << ": " << s.length << " x " << noteValue(s.divBeats) << "    ";
    }
    g.setColour(kDim);
    g.drawFittedText(line.trim(), text.removeFromTop(34.0f).toNearestInt(), juce::Justification::centred, 2);
    const double next = nextConjunction(rows, beat);
    const double bpm = score_.tempo.bpmAt(beat);
    juce::String when = "no conjunction ahead while these rows play";
    if (next > 0.0) {
        const double bars = (next - beat) / 4.0;
        const double seconds = (next - beat) * 60.0 / juce::jmax(1.0, bpm);
        when = "all running rows meet again in " + juce::String(bars, 1) + " bars (" + juce::String(juce::roundToInt(seconds)) + " s)";
    }
    g.setColour(onFirst >= 2 ? kSun : kInk);
    g.drawFittedText(when, text.toNearestInt(), juce::Justification::centred, 1);
}

// ==================================================================== RackPage

RackPage::RackPage(EphemerisProcessor& p, std::unique_ptr<ParamPage> params) : orrery_(p), params_(std::move(params))
{
    addAndMakeVisible(orrery_);
    view_.setViewedComponent(params_.get(), false);
    view_.setScrollBarsShown(true, false);
    addAndMakeVisible(view_);
}

RackPage::~RackPage() = default;

void RackPage::resized()
{
    auto r = getLocalBounds();
    const int side = juce::jmin(r.getHeight(), r.getWidth() / 2);
    orrery_.setBounds(r.removeFromLeft(side));
    ScrollingPage::fit(*params_, view_, r);
}
