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
 * **Gestures as controllers** (Phase 4) when the parameters are given: what a hand does to a knob is
 * written as the knob's value, 0..127, every thirty-second note while it moves and once for a step --
 * cutoff as CC 74, resonance CC 71, filter decay CC 75 on the channel of the row, the lead or the drone;
 * the knobs of the whole instrument (echo, hall, tapes, atmosphere, master) on a track "controls",
 * channel 16, CC 12 to 25 and 7 (the table in Midi.cpp).
 *
 * Not yet: a reader.
 */
#pragma once
#include "eph/Score.h"
#include "eph/Params.h"
#include <cstdint>
#include <vector>

namespace eph {

constexpr int kMidiPpq = 960;   ///< ticks per quarter note in exported files

/** @brief MIDI channel (0-based) a part is written to. */
int midiChannelOf(Part part);

/** @brief Encodes a score as a Standard MIDI File (format 1). Notes need not be sorted. */
std::vector<uint8_t> encodeMidi(const Score& score, const char* title = "Ephemeris", const ParamStore* params = nullptr);

/** @brief Writes encodeMidi() to a file; false if it cannot be written. */
bool writeMidiFile(const Score& score, const char* path, const char* title = "Ephemeris", const ParamStore* params = nullptr);

} // namespace eph
