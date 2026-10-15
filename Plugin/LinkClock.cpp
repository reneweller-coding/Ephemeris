/**
 * @file LinkClock.cpp
 * @brief Ableton Link for the standalone (02.10.2026): LinkClock.h. Without the library (FAMILY_HAS_LINK unset) every
 *        call is a quiet no-op, so a build without it still runs.
 */
#include "LinkClock.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#if FAMILY_HAS_LINK
#include <ableton/Link.hpp>
#endif

namespace frame {

#if FAMILY_HAS_LINK
/** @brief The Link object (120 BPM until the session or the app says otherwise), start/stop sync on. */
struct LinkClock::Impl {
    ableton::Link m_link{ 120.0 };   ///< the session
    Impl() { m_link.enableStartStopSync(true); }
    /** @brief The Link clock's time @p latencySeconds from now. */
    std::chrono::microseconds at(double latencySeconds) const
    {
        return m_link.clock().micros() + std::chrono::microseconds(static_cast<long long>(latencySeconds * 1.0e6));
    }
};
#else
/** @brief Nothing: Link was not compiled in. */
struct LinkClock::Impl {};
#endif

LinkClock::LinkClock() : m_impl(std::make_unique<Impl>()) {}

LinkClock::~LinkClock()
{
#if FAMILY_HAS_LINK
    m_impl->m_link.enable(false);
#endif
}

void LinkClock::setEnabled(bool on)
{
#if FAMILY_HAS_LINK
    if (on != m_impl->m_link.isEnabled()) m_impl->m_link.enable(on);
    m_enabled.store(on, std::memory_order_relaxed);
#else
    (void) on;
#endif
}

int LinkClock::peers() const
{
#if FAMILY_HAS_LINK
    return enabled() ? static_cast<int>(m_impl->m_link.numPeers()) : 0;
#else
    return 0;
#endif
}

LinkClock::Now LinkClock::capture(double latencySeconds, double quantum) const
{
    Now n;
#if FAMILY_HAS_LINK
    if (!enabled()) return n;
    const auto state = m_impl->m_link.captureAudioSessionState();
    const auto t = m_impl->at(latencySeconds);
    n.m_active = true;
    n.m_peers = static_cast<int>(m_impl->m_link.numPeers());
    n.m_bpm = state.tempo();
    n.m_beat = state.beatAtTime(t, quantum);
    n.m_playing = state.isPlaying();
#else
    (void) latencySeconds;
    (void) quantum;
#endif
    return n;
}

void LinkClock::proposeTempo(double bpm, double latencySeconds)
{
#if FAMILY_HAS_LINK
    if (!enabled() || !(bpm > 20.0 && bpm < 999.0)) return;
    auto state = m_impl->m_link.captureAudioSessionState();
    if (std::abs(state.tempo() - bpm) < 1.0e-3) return;
    state.setTempo(bpm, m_impl->at(latencySeconds));
    m_impl->m_link.commitAudioSessionState(state);
#else
    (void) bpm;
    (void) latencySeconds;
#endif
}

void LinkClock::setPlaying(bool playing, double latencySeconds, double quantum)
{
#if FAMILY_HAS_LINK
    if (!enabled()) return;
    auto state = m_impl->m_link.captureAudioSessionState();
    const auto t = m_impl->at(latencySeconds);
    if (playing) state.setIsPlayingAndRequestBeatAtTime(true, t, 0.0, quantum);
    else state.setIsPlaying(false, t);
    m_impl->m_link.commitAudioSessionState(state);
#else
    (void) playing;
    (void) latencySeconds;
    (void) quantum;
#endif
}

bool LinkClock::forcedOn()
{
    static const bool on = [] { const char* v = std::getenv("FAMILY_LINK"); return v != nullptr && v[0] == '1'; }();
    return on;
}

void LinkClock::tick()
{
    const char* path = std::getenv("FAMILY_LINK_REPORT");
    if (path == nullptr || path[0] == 0) return;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - m_lastReport < 1.0) return;
    m_lastReport = now;
    const Now n = capture(0.0, 4.0);
    if (std::FILE* f = std::fopen(path, "a")) {
        std::fprintf(f, "enabled %d peers %d bpm %.3f playing %d beat %.3f\n", n.m_active ? 1 : 0, n.m_peers, n.m_bpm, n.m_playing ? 1 : 0, n.m_beat);
        std::fclose(f);
    }
}

} // namespace frame
