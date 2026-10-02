/**
 * @file NoteTap.h
 * @brief The notes the engine played in one process() call, for the plugin's MIDI out (02.10.2026; after Totality's).
 *
 * The engine writes every composer note it actually plays into the tap -- after the keyboard's Replace and the composer
 * switch, transposed as it is heard, so MIDI out is what sounds -- with its absolute sample; the plugin turns them into
 * MIDI events on the channels of the MIDI export (Midi.h, midiChannelOf). Every note of Ephemeris has its own off, so
 * offSample stays -1. A played key is not written: it came in as MIDI already. Fixed capacity, no allocation: the audio
 * thread fills it and the same thread empties it.
 */
#pragma once
#include <cstdint>

namespace eph {

/** @brief A fixed-capacity list of played notes; clear() before a process() call, read after it. */
struct NoteTap {
    /** @brief One note-on or note-off as the engine played it. */
    struct Note {
        int64_t sample;      ///< when, on the engine's sample counter
        int64_t offSample;   ///< a note-on without an off of its own: when its off is due; else -1
        uint8_t part;        ///< the Part
        uint8_t pitch;       ///< MIDI note number
        uint8_t velocity;    ///< 1..127 for an on, 0 for an off
    };
    static constexpr int kCapacity = 2048;   ///< more notes than any block of 4096 samples plays
    Note notes[kCapacity];                   ///< the notes, in the order they were played
    int count = 0;                           ///< how many of notes[] are valid
    /** @brief Forgets the notes of the last call. */
    void clear() { count = 0; }
    /** @brief Adds a note (dropped when full); @p offSample as in Note. */
    void add(int64_t sample, int part, int pitch, float velocity, bool on, int64_t offSample)
    {
        if (count >= kCapacity || pitch < 0 || pitch > 127) return;
        const int v = on ? (velocity <= 0.0f ? 1 : velocity >= 1.0f ? 127 : 1 + static_cast<int>(velocity * 126.0f + 0.5f)) : 0;
        notes[count++] = Note{ sample, on ? offSample : -1, static_cast<uint8_t>(part), static_cast<uint8_t>(pitch), static_cast<uint8_t>(v) };
    }
};

} // namespace eph
