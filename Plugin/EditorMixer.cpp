/**
 * @file EditorMixer.cpp
 * @brief The mixer page (EditorMixer.h).
 */
#include "EditorMixer.h"
#include "EditorTheme.h"
#include "eph/Presets.h"
#include <cmath>

using namespace eph;

// ==================================================================== MixerConsole

MixerConsole::MixerConsole(EphemerisProcessor& proc)
    : proc_(proc), live_([this](int id) { return proc_.playedNormalised(id); }), console_(ephui::skin())
{
    actions_.learn = [this](int id) { proc_.learn(id); };
    actions_.controllerFor = [this](int id) { return proc_.controllerFor(id); };
    actions_.forget = [this](int id) { proc_.forget(id); };
    actions_.reset = [this](int id) { proc_.resetToDefault(id); };
    actions_.describe = [this](int id) { return juce::String(proc_.store().desc(id).name) + "  (" + proc_.store().key(id) + ")"; };
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
            m = M::Poly; level = poly::Level; pan = poly::Pan; echoSend = poly::EchoSend; reverbSend = poly::ReverbSend;
            colour = juce::Colour(0xff7fa8c9);
        } else if (c == kRows + 5) {
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
        else if (m == M::Poly) blendSend = poly::BlendSend;
        else if (m == M::Drums) blendSend = drums::BlendSend;
        // Send A, the early reflections, where the source has one; then the blend room, the hall, send D.
        int earlySend = -1, shimmerSend = -1;
        if (c < kRows) earlySend = row::EarlySend;
        else if (m == M::Lead || m == M::Drone) { earlySend = lead::EarlySend; shimmerSend = lead::ShimmerSend; }
        else if (m == M::Tape) { earlySend = tape::EarlySend; shimmerSend = tape::ShimmerSend; }
        else if (m == M::Strings) { earlySend = strings::EarlySend; shimmerSend = strings::ShimmerSend; }
        else if (m == M::Poly) { earlySend = poly::EarlySend; shimmerSend = poly::ShimmerSend; }
        else if (m == M::Drums) earlySend = drums::EarlySend;
        else if (m == M::Atmos) shimmerSend = atmos::ShimmerSend;
        if (earlySend >= 0) knobs.emplace_back(p.id(m, inst, earlySend), "Early");
        if (blendSend >= 0) knobs.emplace_back(p.id(m, inst, blendSend), "Blend");
        knobs.emplace_back(p.id(m, inst, reverbSend), "Hall");
        if (shimmerSend >= 0) knobs.emplace_back(p.id(m, inst, shimmerSend), "Shimmer");
        synths_.push_back({ c < kRows ? M::Voice : m, c < kRows ? c : 0 });
        colour = ephui::channelColour(c);   // (26.09.2026: the families' palette, EditorTheme.h)
        auto control = [&](int id, const juce::String& label, bool send) {
            frame::ChannelStrip::Control k;
            k.id = id;
            k.param = proc_.parameter(id);
            k.label = label;
            k.send = send;
            return k;
        };
        std::vector<frame::ChannelStrip::Control> controls;
        for (const auto& [id, label] : knobs) controls.push_back(control(id, label, juce::String(label) != "Pan"));
        console_.add(std::make_unique<frame::ChannelStrip>(ephui::skin(), Engine::channelName(c), colour,
                                                           control(p.id(m, inst, level), juce::String(Engine::channelName(c)) + " level", false),
                                                           controls, &actions_, &live_));
    }
    addAndMakeVisible(console_);
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
    for (int i = 0; i < console_.size() && i < Engine::kChannels; ++i) console_.strip(i).meter(peak[i], rms[i], dt);
    // The composer's presets of the moment, about once a second.
    if (++tick_ % 16 != 1) return;
    for (int i = 0; i < console_.size() && i < static_cast<int>(synths_.size()); ++i) {
        const auto [m, inst] = synths_[static_cast<size_t>(i)];
        const int index = proc_.composedPreset(m, inst);
        const std::vector<SoundPreset>& list = factoryPresets(m);
        console_.strip(i).setSound(index >= 0 && index < static_cast<int>(list.size()) ? juce::String(list[static_cast<size_t>(index)].name) : juce::String());
    }
}

void MixerConsole::resized() { console_.setBounds(getLocalBounds()); }
