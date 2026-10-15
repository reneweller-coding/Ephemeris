/**
 * @file LinkClock.h
 * @brief Ableton Link for the standalone (02.10.2026, the family's): tempo, beat phase and start/stop shared with the
 *        other apps on the network -- DJ software, Ableton Live, another generator of the family.
 *
 * Off by default (Settings > Ableton Link). On, the standalone joins the Link session. With other apps in it, it follows
 * the session as it follows a host's playhead in a DAW: the session's tempo, its bar phase (a quantum of four beats --
 * the bars line up, the position in the track stays), its start and stop. Alone in the session it leads: the session
 * takes the track's tempo. In a DAW the host's transport rules and Link is not used. Ableton Link is free software
 * (GPL-2.0-or-later, github.com/Ableton/link), compiled in through cmake/Family.cmake (family_link).
 *
 * For tests and diagnostics: FAMILY_LINK=1 switches it on without touching the settings, FAMILY_LINK_REPORT=\<file\>
 * appends a line a second with what the session says (tick()).
 * @author Rene Weller
 */
#pragma once
#include <atomic>
#include <memory>

namespace frame
{

/** @brief One app's place in a Link session; the audio thread reads and writes it, the message thread switches it. */
class LinkClock
{
public:
    /** @brief What the session says at the moment a block is heard. */
    struct Now
    {
        bool   m_active = false;    ///< Link is on
        int    m_peers = 0;         ///< the other apps in the session
        double m_bpm = 120.0;       ///< the session's tempo
        double m_beat = 0.0;        ///< the session's beat at that moment (its bar phase: beat modulo the quantum)
        bool   m_playing = false;   ///< the session's transport (start/stop sync)
    };

    LinkClock();
    ~LinkClock();
    /**
     * @brief Joins or leaves the session (message thread).
     * @param[in] on  true to join the session
     * @pre called on the message thread
     */
    void setEnabled( bool on );
    /**
     * @brief Whether it is in the session.
     * @return true while it is in the session
     */
    bool enabled() const { return m_enabled.load( std::memory_order_relaxed ); }
    /**
     * @brief How many other apps are in the session (any thread).
     * @return how many other apps are in the session
     */
    int peers() const;
    /**
     * @brief The session at the moment a block starting now is heard, @p latencySeconds from now (audio thread).
     * @param[in] latencySeconds  the output latency in seconds
     * @param[in] quantum         the bar's length in beats
     * @return the session at that moment
     * @pre called on the audio thread
     */
    Now capture( double latencySeconds, double quantum ) const;
    /**
     * @brief Proposes @p bpm to the session -- the app alone in it leads (audio thread).
     * @param[in] bpm             the tempo
     * @param[in] latencySeconds  the output latency in seconds
     * @pre called on the audio thread
     */
    void proposeTempo( double bpm, double latencySeconds );
    /**
     * @brief Starts or stops the session's transport; a start lines beat 0 up with the next bar (audio thread).
     * @param[in] playing         true to start, false to stop
     * @param[in] latencySeconds  the output latency in seconds
     * @param[in] quantum         the bar's length in beats
     * @pre called on the audio thread
     */
    void setPlaying( bool playing, double latencySeconds, double quantum );
    /**
     * @brief Whether FAMILY_LINK=1 asks for Link whatever the settings say (tests).
     * @return true when FAMILY_LINK=1 is set
     */
    static bool forcedOn();
    /**
     * @brief Message thread, on the owner's timer: with FAMILY_LINK_REPORT set, a line a second on the session.
     * @pre called on the message thread
     */
    void tick();

private:
    struct Impl;                    ///< the Link object itself (LinkClock.cpp): the header stays free of asio
    std::unique_ptr<Impl> m_impl;   ///< made on construction, the session joined only when enabled
    std::atomic<bool>     m_enabled{ false };   ///< setEnabled's last word
    double                m_lastReport = 0.0;   ///< when tick() last wrote, seconds on the steady clock
};

}   // namespace frame
