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
    : m_proc(proc), m_live([this](int id) { return m_proc.playedNormalised(id); }), m_console(ephui::skin())
{
    m_actions.m_learn = [this](int id) { m_proc.learn(id); };
    m_actions.m_controllerFor = [this](int id) { return m_proc.controllerFor(id); };
    m_actions.m_forget = [this](int id) { m_proc.forget(id); };
    m_actions.m_reset = [this](int id) { m_proc.resetToDefault(id); };
    m_actions.m_describe = [this](int id) { return juce::String(m_proc.store().desc(id).m_name) + "  (" + m_proc.store().key(id) + ")"; };
    const ParamStore& p = proc.store();
    using MT = ModuleE;
    // The strips in eph::Engine channel order; the drone's table is laid out like the lead's (Params.h).
    for (int c = 0; c < Engine::Channels; ++c) {
        MT m = MT::ROW;
        int inst = 0, level = 0, pan = -1, echoSend = 0, reverbSend = 0;
        juce::Colour colour(0xffc9a45c);
        if (c < Rows) {
            inst = c; level = row::LEVEL; pan = row::PAN; echoSend = row::ECHO_SEND; reverbSend = row::REVERB_SEND;
        } else if (c == Rows || c == Rows + 1) {
            m = c == Rows ? MT::LEAD : MT::DRONE;
            level = lead::LEVEL; pan = lead::PAN; echoSend = lead::ECHO_SEND; reverbSend = lead::REVERB_SEND;
            colour = juce::Colour(c == Rows ? 0xffd9825b : 0xff6f8fb8);
        } else if (c == Rows + 2) {
            m = MT::TAPE; level = tape::LEVEL; pan = tape::PAN; echoSend = tape::ECHO_SEND; reverbSend = tape::REVERB_SEND;
            colour = juce::Colour(0xffb58ac2);
        } else if (c == Rows + 3) {
            m = MT::STRINGS; level = strings::LEVEL; pan = strings::PAN; echoSend = strings::ECHO_SEND; reverbSend = strings::REVERB_SEND;
            colour = juce::Colour(0xff8fbf7f);
        } else if (c == Rows + 4) {
            m = MT::POLY; level = poly::LEVEL; pan = poly::PAN; echoSend = poly::ECHO_SEND; reverbSend = poly::REVERB_SEND;
            colour = juce::Colour(0xff7fa8c9);
        } else if (c == Rows + 5) {
            m = MT::DRUMS; level = drums::LEVEL; echoSend = drums::ECHO_SEND; reverbSend = drums::REVERB_SEND;
            colour = juce::Colour(0xffc76b6b);
        } else {
            m = MT::ATMOS; level = atmos::LEVEL; echoSend = atmos::ECHO_SEND; reverbSend = atmos::REVERB_SEND;
            colour = juce::Colour(0xff6fb8ae);
        }
        std::vector<std::pair<int, const char*>> knobs;
        if (pan >= 0) knobs.emplace_back(p.id(m, inst, pan), "Pan");
        knobs.emplace_back(p.id(m, inst, echoSend), "Echo");
        if (c < Rows) knobs.emplace_back(p.id(m, inst, row::ECHO2_SEND), "Echo 2");
        // The blend room (the addon's serial far space), where the source has a send into it.
        int blendSend = -1;
        if (c < Rows) blendSend = row::BLEND_SEND;
        else if (m == MT::LEAD || m == MT::DRONE) blendSend = lead::BLEND_SEND;
        else if (m == MT::TAPE) blendSend = tape::BLEND_SEND;
        else if (m == MT::STRINGS) blendSend = strings::BLEND_SEND;
        else if (m == MT::POLY) blendSend = poly::BLEND_SEND;
        else if (m == MT::DRUMS) blendSend = drums::BLEND_SEND;
        // Send A, the early reflections, where the source has one; then the blend room, the hall, send D.
        int earlySend = -1, shimmerSend = -1;
        if (c < Rows) earlySend = row::EARLY_SEND;
        else if (m == MT::LEAD || m == MT::DRONE) { earlySend = lead::EARLY_SEND; shimmerSend = lead::SHIMMER_SEND; }
        else if (m == MT::TAPE) { earlySend = tape::EARLY_SEND; shimmerSend = tape::SHIMMER_SEND; }
        else if (m == MT::STRINGS) { earlySend = strings::EARLY_SEND; shimmerSend = strings::SHIMMER_SEND; }
        else if (m == MT::POLY) { earlySend = poly::EARLY_SEND; shimmerSend = poly::SHIMMER_SEND; }
        else if (m == MT::DRUMS) earlySend = drums::EARLY_SEND;
        else if (m == MT::ATMOS) shimmerSend = atmos::SHIMMER_SEND;
        if (earlySend >= 0) knobs.emplace_back(p.id(m, inst, earlySend), "Early");
        if (blendSend >= 0) knobs.emplace_back(p.id(m, inst, blendSend), "Blend");
        knobs.emplace_back(p.id(m, inst, reverbSend), "Hall");
        if (shimmerSend >= 0) knobs.emplace_back(p.id(m, inst, shimmerSend), "Shimmer");
        m_synths.push_back({ c < Rows ? MT::VOICE : m, c < Rows ? c : 0 });
        colour = ephui::channelColour(c);   // (26.09.2026: the families' palette, EditorTheme.h)
        auto control = [&](int id, const juce::String& label, bool send) {
            frame::ChannelStrip::Control k;
            k.m_id = id;
            k.m_param = m_proc.parameter(id);
            k.m_label = label;
            k.m_send = send;
            return k;
        };
        std::vector<frame::ChannelStrip::Control> controls;
        for (const auto& [id, label] : knobs) controls.push_back(control(id, label, juce::String(label) != "Pan"));
        m_console.add(std::make_unique<frame::ChannelStrip>(ephui::skin(), Engine::channelName(c), colour,
                                                           control(p.id(m, inst, level), juce::String(Engine::channelName(c)) + " level", false),
                                                           controls, &m_actions, &m_live));
    }
    addAndMakeVisible(m_console);
    m_lastPoll = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz(30);
}

MixerConsole::~MixerConsole() { stopTimer(); }

void MixerConsole::timerCallback()
{
    float peak[Engine::Channels], rms[Engine::Channels];
    m_proc.takeChannelMeters(peak, rms);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit(0.001, 0.5, now - m_lastPoll);
    m_lastPoll = now;
    // The strips follow the readings whether the page is on screen or not, so a page that comes into view --
    // or into a screenshot -- shows the level of now.
    for (int i = 0; i < m_console.size() && i < Engine::Channels; ++i) m_console.strip(i).meter(peak[i], rms[i], dt);
    // The composer's presets of the moment, about once a second.
    if (++m_tick % 16 != 1) return;
    for (int i = 0; i < m_console.size() && i < static_cast<int>(m_synths.size()); ++i) {
        const auto [m, inst] = m_synths[static_cast<size_t>(i)];
        const int index = m_proc.composedPreset(m, inst);
        const std::vector<SoundPreset>& list = factoryPresets(m);
        m_console.strip(i).setSound(index >= 0 && index < static_cast<int>(list.size()) ? juce::String(list[static_cast<size_t>(index)].m_name) : juce::String());
    }
}

void MixerConsole::resized() { m_console.setBounds(getLocalBounds()); }
