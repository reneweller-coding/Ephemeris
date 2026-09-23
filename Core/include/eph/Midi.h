/**
 * @file Midi.h
 * @brief Standard MIDI File export of a score.
 *
 * Format 1, 960 ticks per quarter note, so sixteenths (240), sixteenth triplets (160) and the
 * thirty-seconds of a ratchet (120) are exact. Track 0 is the conductor track: name, 4/4, key
 * signature, the tempo map and the markers. Every part with notes gets its own track, channel
 * `part mod 16` except the drums on channel 10 (index 9).
 *
 * Tempo ramps are written as one tempo event per beat whose value makes that beat last exactly as long
 * as it does in the ramp (60 / (secondsAt(b + 1) - secondsAt(b))), so the file stays in time with the
 * audio render at every beat, however long the ramp. The same rule as Phosphene's exporter; the code
 * is new because the parts are.
 *
 * Not yet: gestures as controller curves (PLAN 7, Phase 4) and a reader.
 */
#pragma once
#include "eph/Score.h"
#include <cstdint>
#include <vector>

namespace eph {

constexpr int kMidiPpq = 960;   ///< ticks per quarter note in exported files

/** @brief MIDI channel (0-based) a part is written to. */
int midiChannelOf(Part part);

/** @brief Encodes a score as a Standard MIDI File (format 1). Notes need not be sorted. */
std::vector<uint8_t> encodeMidi(const Score& score, const char* title = "Ephemeris");

/** @brief Writes encodeMidi() to a file; false if it cannot be written. */
bool writeMidiFile(const Score& score, const char* path, const char* title = "Ephemeris");

} // namespace eph
