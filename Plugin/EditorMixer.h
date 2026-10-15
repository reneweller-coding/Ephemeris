/**
 * @file EditorMixer.h
 * @brief The mixer page: a channel strip per source of the engine, with a meter, a fader and the sends.
 *
 * The user (25.09.2026): "Bitte für den Mixer auch die Meter-Anzeige wie in Phosphene verwenden." A strip holds,
 * top to bottom, what a console strip holds: the source's pan (where it has one), its sends to the tape echo and
 * the hall, and the fader beside the meter. Every control is the same host parameter the source's own page
 * shows -- two views of one value, no copy.
 *
 * The meter reads what the source puts into the mix (eph::Engine::takeMeters: after fader and pan, before the
 * rooms and the master), as an RMS bar with a 300 ms release and a peak line that holds for a second and a
 * half and then falls at 20 dB a second: the body of the level and its peaks, as a console shows them.
 *
 * Since 01.10.2026 the strips and the console are the frame's (Frame.h: ChannelStrip, Console), the same in every
 * generator; the controls have the frame's right-click menu and the live ring of the moves.
 * @note After Phosphene `Plugin/EditorMixer.h` (24.09.2026), without the mutes Ephemeris does not have.
 * @author Rene Weller
 */
#pragma once
#include "PluginProcessor.h"
#include "Frame.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>
#include <vector>

/** @brief The console: one frame::ChannelStrip per engine channel, in the mixing order, fed by a 30 Hz timer. */
class MixerConsole final : public juce::Component, private juce::Timer {
public:
    /**
     * @brief Builds a strip per channel and starts the meter timer.
     * @param[in,out] proc  the processor
     */
    explicit MixerConsole(EphemerisProcessor& proc);
    ~MixerConsole() override;   ///< stops the timer
    void resized() override;    ///< the console fills the page

private:
    /** @brief Takes the processor's readings and hands them to the strips. */
    void timerCallback() override;
    EphemerisProcessor& m_proc;                          ///< the processor whose meters it reads
    frame::ControlActions m_actions;                     ///< the strips' right-click menu
    frame::LiveRings m_live;                             ///< where the faders and sends play
    frame::Console m_console;                            ///< the strips
    std::vector<std::pair<eph::ModuleE, int>> m_synths;   ///< per strip: the synth whose presets it plays (Count: none)
    int m_tick = 0;                                      ///< timer ticks, to look at the presets now and then
    double m_lastPoll = 0.0;                             ///< when the meters were last read, seconds
};
