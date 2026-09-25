/**
 * @file EditorMixer.cpp
 * @brief The mixer page (EditorMixer.h).
 */
#include "EditorMixer.h"
#include <cmath>

using namespace eph;

namespace {

constexpr float kTopDb = 6.0f;       ///< the meter's top
constexpr float kFloorDb = -60.0f;   ///< and its floor
constexpr double kHoldSeconds = 1.5;
constexpr double kFallDbPerSecond = 20.0;
constexpr double kRmsRelease = 0.3;  ///< seconds for the RMS bar to fall by 1/e of its distance

// The panel's colours (PluginEditor.cpp) and the meter's (Phosphene's).
const juce::Colour kGroup(0xff252932), kMeterBack(0xff111318), kEdge(0xff343945);
const juce::Colour kInk(0xffd8d4c8), kDim(0xff8a8f99), kFaint(0xff5d6273);
const juce::Colour kGreen(0xff67e8a0), kWarm(0xffff9a4d), kRed(0xffff6b6b);

float toDb(float linear) { return linear > 1.0e-5f ? 20.0f * std::log10(linear) : -100.0f; }

juce::Font font(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

} // namespace

// ==================================================================== MixerStrip

MixerStrip::MixerStrip(EphemerisProcessor& proc, const juce::String& name, juce::Colour colour, int level,
                       const std::vector<std::pair<int, const char*>>& knobs)
    : name_(name), colour_(colour)
{
    const ParamStore& p = proc.store();
    fader_ = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    fader_->setColour(juce::Slider::trackColourId, colour_.withAlpha(0.8f));
    fader_->setColour(juce::Slider::thumbColourId, kInk);
    fader_->setColour(juce::Slider::backgroundColourId, kMeterBack);
    fader_->setPopupDisplayEnabled(true, true, nullptr);
    fader_->setTooltip(juce::String(p.desc(level).name));
    addAndMakeVisible(*fader_);
    if (StoreParameter* sp = proc.parameter(level)) faderLink_ = std::make_unique<juce::SliderParameterAttachment>(*sp, *fader_);
    for (const auto& [id, label] : knobs) {
        StoreParameter* sp = proc.parameter(id);
        if (sp == nullptr) continue;
        auto k = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        k->setColour(juce::Slider::rotarySliderFillColourId, colour_);
        k->setPopupDisplayEnabled(true, true, nullptr);
        k->setTooltip(juce::String(p.desc(id).name));
        addAndMakeVisible(*k);
        knobLinks_.push_back(std::make_unique<juce::SliderParameterAttachment>(*sp, *k));
        auto l = std::make_unique<juce::Label>(juce::String(), label);
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::textColourId, kDim);
        l->setFont(font(10.0f));
        l->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*l);
        knobs_.push_back(std::move(k));
        knobNames_.push_back(std::move(l));
    }
}

void MixerStrip::meter(float peak, float rms, double seconds)
{
    const float pDb = toDb(peak), rDb = toDb(rms);
    // The RMS bar rises at once and falls with kRmsRelease; the peak line holds, then falls.
    const float a = static_cast<float>(std::exp(-seconds / kRmsRelease));
    rmsDb_ = rDb >= rmsDb_ ? rDb : juce::jmax(rDb, kFloorDb - 1.0f + (rmsDb_ - (kFloorDb - 1.0f)) * a);
    if (pDb >= holdDb_) { holdDb_ = pDb; holdAge_ = 0.0; }
    else {
        holdAge_ += seconds;
        if (holdAge_ > kHoldSeconds) holdDb_ = juce::jmax(pDb, holdDb_ - static_cast<float>(kFallDbPerSecond * seconds));
    }
    repaint(meterArea_.expanded(2).withBottom(getHeight()));
}

void MixerStrip::resized()
{
    auto r = getLocalBounds().reduced(3);
    r.removeFromTop(20);   // the name
    // The knobs one under the other: fourteen strips leave a strip too narrow for two knobs of a size a hand
    // can grab. The name sits under each knob.
    const int kh = juce::jlimit(28, 44, r.getWidth() - 18), lh = 12;
    for (size_t i = 0; i < knobs_.size(); ++i) {
        auto row = r.removeFromTop(kh + lh);
        knobs_[i]->setBounds(row.removeFromTop(kh));
        knobNames_[i]->setBounds(row);
    }
    // Room for the rows' five knobs on every strip, so the faders and meters line up across the console.
    r.removeFromTop(static_cast<int>(5 - std::min<size_t>(5, knobs_.size())) * (kh + lh));
    r.removeFromTop(6);
    r.removeFromBottom(15);   // the peak readout
    const int half = r.getWidth() / 2;
    fader_->setBounds(r.removeFromLeft(half));
    meterArea_ = r.reduced(4, 6);
}

void MixerStrip::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(kGroup);
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(colour_);
    g.fillRoundedRectangle(b.withHeight(3.0f), 1.5f);
    g.setColour(kInk);
    g.setFont(font(12.5f, true));
    g.drawFittedText(name_, getLocalBounds().withHeight(22).reduced(2, 0), juce::Justification::centred, 1);

    // The meter: a dB scale from kFloorDb to kTopDb, the RMS bar, the held peak, 0 dBFS marked.
    const auto m = meterArea_.toFloat();
    g.setColour(kMeterBack);
    g.fillRect(m);
    auto yOf = [&](float db) {
        const float t = (juce::jlimit(kFloorDb, kTopDb, db) - kFloorDb) / (kTopDb - kFloorDb);
        return m.getBottom() - t * m.getHeight();
    };
    const float top = yOf(rmsDb_);
    if (rmsDb_ > kFloorDb) {
        const float y6 = yOf(-6.0f), y0 = yOf(0.0f);
        g.setColour(kGreen.withAlpha(0.85f));
        g.fillRect(juce::Rectangle<float>(m.getX(), juce::jmax(top, y6), m.getWidth(), m.getBottom() - juce::jmax(top, y6)));
        if (top < y6) {
            g.setColour(kWarm.withAlpha(0.9f));
            g.fillRect(juce::Rectangle<float>(m.getX(), juce::jmax(top, y0), m.getWidth(), y6 - juce::jmax(top, y0)));
        }
        if (top < y0) {
            g.setColour(kRed);
            g.fillRect(juce::Rectangle<float>(m.getX(), top, m.getWidth(), y0 - top));
        }
    }
    if (holdDb_ > kFloorDb) {
        g.setColour(holdDb_ > 0.0f ? kRed : kInk);
        g.fillRect(m.getX(), yOf(holdDb_) - 1.0f, m.getWidth(), 2.0f);
    }
    g.setColour(kFaint);
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f }) g.fillRect(m.getRight() + 1.0f, yOf(db), 3.0f, 1.0f);
    g.setColour(kEdge);
    g.drawRect(m, 1.0f);
    // The held peak in figures under the meter.
    g.setColour(holdDb_ > 0.0f ? kRed : kDim);
    g.setFont(font(10.5f));
    const juce::String readout = holdDb_ > -99.0f ? juce::String(holdDb_, 1) : juce::String("-inf");
    g.drawFittedText(readout, getLocalBounds().removeFromBottom(17).reduced(2, 1), juce::Justification::centred, 1);
}

// ==================================================================== MixerConsole

MixerConsole::MixerConsole(EphemerisProcessor& proc) : proc_(proc)
{
    const ParamStore& p = proc.store();
    using M = Module;
    // The strips in eph::Engine channel order; the drone's table is laid out like the lead's (Params.h).
    for (int c = 0; c < Engine::kChannels; ++c) {
        M m = M::Row;
        int inst = 0, level = 0, pan = -1, echoSend = 0, reverbSend = 0;
        juce::Colour colour(0xffc9a45c);
        if (c < kRows) {
            inst = c; level = row::Level; pan = row::Pan; echoSend = row::EchoSend; reverbSend = row::ReverbSend;
        } else if (c == kRows || c == kRows + 1) {
            m = c == kRows ? M::Lead : M::Drone;
            level = lead::Level; pan = lead::Pan; echoSend = lead::EchoSend; reverbSend = lead::ReverbSend;
            colour = juce::Colour(c == kRows ? 0xffd9825b : 0xff6f8fb8);
        } else if (c == kRows + 2) {
            m = M::Tape; level = tape::Level; pan = tape::Pan; echoSend = tape::EchoSend; reverbSend = tape::ReverbSend;
            colour = juce::Colour(0xffb58ac2);
        } else if (c == kRows + 3) {
            m = M::Strings; level = strings::Level; pan = strings::Pan; echoSend = strings::EchoSend; reverbSend = strings::ReverbSend;
            colour = juce::Colour(0xff8fbf7f);
        } else if (c == kRows + 4) {
            m = M::Drums; level = drums::Level; echoSend = drums::EchoSend; reverbSend = drums::ReverbSend;
            colour = juce::Colour(0xffc76b6b);
        } else {
            m = M::Atmos; level = atmos::Level; echoSend = atmos::EchoSend; reverbSend = atmos::ReverbSend;
            colour = juce::Colour(0xff6fb8ae);
        }
        std::vector<std::pair<int, const char*>> knobs;
        if (pan >= 0) knobs.emplace_back(p.id(m, inst, pan), "Pan");
        knobs.emplace_back(p.id(m, inst, echoSend), "Echo");
        if (c < kRows) knobs.emplace_back(p.id(m, inst, row::Echo2Send), "Echo 2");
        // The blend room (the addon's serial far space), where the source has a send into it.
        int blendSend = -1;
        if (c < kRows) blendSend = row::BlendSend;
        else if (m == M::Lead || m == M::Drone) blendSend = lead::BlendSend;
        else if (m == M::Tape) blendSend = tape::BlendSend;
        else if (m == M::Strings) blendSend = strings::BlendSend;
        else if (m == M::Drums) blendSend = drums::BlendSend;
        if (blendSend >= 0) knobs.emplace_back(p.id(m, inst, blendSend), "Blend");
        knobs.emplace_back(p.id(m, inst, reverbSend), "Hall");
        auto strip = std::make_unique<MixerStrip>(proc, Engine::channelName(c), colour, p.id(m, inst, level), knobs);
        addAndMakeVisible(*strip);
        strips_.push_back(std::move(strip));
    }
    lastPoll_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz(30);
}

MixerConsole::~MixerConsole() { stopTimer(); }

void MixerConsole::timerCallback()
{
    float peak[Engine::kChannels], rms[Engine::kChannels];
    proc_.takeChannelMeters(peak, rms);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit(0.001, 0.5, now - lastPoll_);
    lastPoll_ = now;
    // The strips follow the readings whether the page is on screen or not, so a page that comes into view --
    // or into a screenshot -- shows the level of now.
    for (size_t i = 0; i < strips_.size(); ++i) strips_[i]->meter(peak[i], rms[i], dt);
}

void MixerConsole::resized()
{
    const int n = static_cast<int>(strips_.size());
    if (n == 0) return;
    const auto area = getLocalBounds().reduced(6);
    const int gap = 4, w = (area.getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i) strips_[static_cast<size_t>(i)]->setBounds(area.getX() + i * (w + gap), area.getY(), w, area.getHeight());
}
