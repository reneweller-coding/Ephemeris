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
    ableton::Link link{ 120.0 };   ///< the session
    Impl() { link.enableStartStopSync(true); }
    /** @brief The Link clock's time @p latencySeconds from now. */
    std::chrono::microseconds at(double latencySeconds) const
    {
        return link.clock().micros() + std::chrono::microseconds(static_cast<long long>(latencySeconds * 1.0e6));
    }
};
#else
/** @brief Nothing: Link was not compiled in. */
struct LinkClock::Impl {};
#endif

LinkClock::LinkClock() : impl_(std::make_unique<Impl>()) {}

LinkClock::~LinkClock()
{
#if FAMILY_HAS_LINK
    impl_->link.enable(false);
#endif
}

void LinkClock::setEnabled(bool on)
{
#if FAMILY_HAS_LINK
    if (on != impl_->link.isEnabled()) impl_->link.enable(on);
    enabled_.store(on, std::memory_order_relaxed);
#else
    (void) on;
#endif
}

int LinkClock::peers() const
{
#if FAMILY_HAS_LINK
    return enabled() ? static_cast<int>(impl_->link.numPeers()) : 0;
#else
    return 0;
#endif
}

LinkClock::Now LinkClock::capture(double latencySeconds, double quantum) const
{
    Now n;
#if FAMILY_HAS_LINK
    if (!enabled()) return n;
    const auto state = impl_->link.captureAudioSessionState();
    const auto t = impl_->at(latencySeconds);
    n.active = true;
    n.peers = static_cast<int>(impl_->link.numPeers());
    n.bpm = state.tempo();
    n.beat = state.beatAtTime(t, quantum);
    n.playing = state.isPlaying();
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
    auto state = impl_->link.captureAudioSessionState();
    if (std::abs(state.tempo() - bpm) < 1.0e-3) return;
    state.setTempo(bpm, impl_->at(latencySeconds));
    impl_->link.commitAudioSessionState(state);
#else
    (void) bpm;
    (void) latencySeconds;
#endif
}

void LinkClock::setPlaying(bool playing, double latencySeconds, double quantum)
{
#if FAMILY_HAS_LINK
    if (!enabled()) return;
    auto state = impl_->link.captureAudioSessionState();
    const auto t = impl_->at(latencySeconds);
    if (playing) state.setIsPlayingAndRequestBeatAtTime(true, t, 0.0, quantum);
    else state.setIsPlaying(false, t);
    impl_->link.commitAudioSessionState(state);
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
    if (now - lastReport_ < 1.0) return;
    lastReport_ = now;
    const Now n = capture(0.0, 4.0);
    if (std::FILE* f = std::fopen(path, "a")) {
        std::fprintf(f, "enabled %d peers %d bpm %.3f playing %d beat %.3f\n", n.active ? 1 : 0, n.peers, n.bpm, n.playing ? 1 : 0, n.beat);
        std::fclose(f);
    }
}

} // namespace frame
