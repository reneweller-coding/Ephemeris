/**
 * @file Cue.h
 * @brief Score cues for a visualiser (PLAN 8.3): what the score knows, sent over OSC at the moment it is heard.
 *
 * Kaleidoscope guesses beat and section from the audio it is fed. Ephemeris does not have to guess: it wrote
 * the score. So it says so, over UDP as OSC 1.0:
 * @code
 *   /eph/beat i f          beat number, tempo in BPM                      on every beat
 *   /eph/phase s i         section ("ENTRY", "BUILD", "PEAK", ...), phase at every section boundary
 *   /eph/key s             the root the rows play on ("D", "F#")          when the transposer moves it
 *   /eph/conjunction i f   rows meeting on their first step, share of the running rows    at a conjunction
 * @endcode
 *
 * **Where a cue comes from** (after Phosphene's Cue.h, which argues it at length): not from the composer,
 * which knows every boundary long before it is heard, but from the play position. The engine turns its
 * score into a list of marks when it loads it (Engine::cueMarks); the audio thread gives CueTap::scan() the
 * beat range each block covers, and the tap stamps every mark in it with the instant the listener hears it
 * -- now, plus the output's latency, plus the limiter's lookahead, plus the mark's place in the block.
 *
 * **The audio thread sends nothing.** scan() does arithmetic and one wait-free push per cue into a CueRing;
 * the CueSender's own thread pops, waits for the due instant and calls sendto(). No socket, no allocation and
 * no lock touch the audio thread. **Failure is silence**: a host nobody listens on, a socket that cannot be
 * opened, a full ring -- all counted (CueSender::dropped) and otherwise ignored.
 */
#pragma once
#include "eph/Score.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace eph {

/** @brief What a mark or a cue says. */
enum class CueKind : uint8_t { Beat, Phase, Key, Conjunction };

/** @brief A mark in the score: a beat and what happens there (built at load, read by the audio thread). */
struct CueMark {
    double beat = 0.0;          ///< where
    CueKind kind = CueKind::Phase;   ///< what
    int32_t a = 0;              ///< Phase: the phase; Conjunction: rows meeting
    float b = 0.0f;             ///< Conjunction: their share of the running rows
    char text[16] = {};         ///< Phase: the section's name; Key: the root's name
};

/** @brief A cue with its instant: what the audio thread hands to the sender. */
struct Cue {
    int64_t dueNanos = 0;       ///< when the listener hears it (steady clock)
    CueKind kind = CueKind::Beat;   ///< what
    int32_t a = 0;              ///< Beat: the beat number; else as CueMark
    float b = 0.0f;             ///< Beat: the tempo; else as CueMark
    char text[16] = {};         ///< as CueMark
};

/**
 * @brief The marks of a score: its sections (the markers, in English), the roots the transposer moves to,
 *        and the conjunctions of its rows (every step where two or more running rows are on their first
 *        step together). In beat order.
 */
std::vector<CueMark> cueMarksOf(const Score& score);

/** @brief Single-producer single-consumer ring of cues: the audio thread pushes, the sender pops. Wait-free. */
class CueRing {
public:
    /** @brief Adds a cue; false when full (the cue is dropped). */
    bool push(const Cue& c)
    {
        const uint32_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= kSize) return false;
        slots_[w % kSize] = c;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    /** @brief Takes the oldest cue; false when empty. */
    bool pop(Cue& c)
    {
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        c = slots_[r % kSize];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kSize = 512;   ///< slots in the ring
    std::array<Cue, kSize> slots_{};   ///< the ring
    std::atomic<uint32_t> write_{ 0 };   ///< the next slot to write (audio thread)
    std::atomic<uint32_t> read_{ 0 };   ///< the next slot to read (sender thread)
};

/** @brief Turns the beat range of a block into cues (audio thread: arithmetic and pushes only). */
class CueTap {
public:
    /**
     * @brief Emits the beats and the marks in [@p from, @p to).
     * @param marks      Engine::cueMarks() of the score that plays
     * @param from       beat at the block's first sample
     * @param to         beat just past its last sample
     * @param bpm        the tempo, for the beat message
     * @param nowNanos   the steady clock now
     * @param leadNanos  how much later the block's first sample is heard (output latency, lookahead)
     * @param blockNanos the block's duration
     * @param ring       where the cues go
     * @return the cues that did not fit
     */
    int scan(const std::vector<CueMark>& marks, double from, double to, float bpm, int64_t nowNanos,
             int64_t leadNanos, int64_t blockNanos, CueRing& ring);
    /** @brief Forgets the position in the marks (after a jump or a new score). */
    void reset() { cursor_ = 0; }

private:
    size_t cursor_ = 0;   ///< the first mark not yet sent
};

/**
 * @brief Encodes one OSC 1.0 message: the address, the type tags, the arguments, each padded to four bytes,
 *        numbers big-endian. @p tags are the argument types without the comma ("if", "s", "si").
 * @return the bytes
 */
std::vector<uint8_t> oscMessage(const char* address, const char* tags, const int32_t* ints, const float* floats, const char* const* strings);

/** @brief The bytes of a cue as its OSC message (CueKind decides the address and the arguments). */
std::vector<uint8_t> oscOf(const Cue& c);

/** @brief The UDP sender: a thread that pops cues, waits for their instant and sends them. */
class CueSender {
public:
    CueSender() = default;
    CueSender(const CueSender&) = delete;
    CueSender& operator=(const CueSender&) = delete;
    ~CueSender() { stop(); }
    /** @brief Opens the socket to @p host (an IPv4 address or a name) and @p port and starts the thread. */
    bool start(const std::string& host, int port);
    /** @brief Stops the thread and closes the socket. */
    void stop();
    /** @brief Whether the thread runs. */
    bool running() const { return running_.load(std::memory_order_acquire); }
    /** @brief The ring the audio thread fills. */
    CueRing& ring() { return ring_; }
    /** @brief Cues that could not be sent. */
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    /** @brief The steady clock in nanoseconds, the time base of the cues. */
    static int64_t nowNanos();

private:
    /** @brief The thread: takes the cues off the ring and sends each as an OSC message when its time comes. */
    void loop();
    CueRing ring_;   ///< the ring the audio thread fills
    std::thread thread_;   ///< the sender thread
    std::atomic<bool> running_{ false };   ///< the thread runs
    std::atomic<bool> stop_{ false };   ///< the thread is asked to end
    std::atomic<uint64_t> dropped_{ 0 };   ///< cues that could not be sent
    intptr_t socket_ = -1;   ///< the UDP socket, -1 closed
    uint8_t address_[16] = {};   ///< a sockaddr_in, kept opaque so this header needs no socket headers
};

} // namespace eph
