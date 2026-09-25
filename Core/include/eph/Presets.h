/**
 * @file Presets.h
 * @brief Factory sound presets for every synth (25.09.2026, after Phosphene's SoundPresets and the names of
 *        Noctuary's presets): 1024 per synth -- the modular voice of the rows, the lead, the drone, the tape
 *        keys, the string machine, the drums and the atmosphere.
 *
 * **How they are made.** Each synth has sixteen groups -- "Ladder Bass", "Glass Arp", "Cathedral Choir", "Slow
 * Swell" ... -- and each group sixty-four presets on an eight by eight grid. The rows of the grid are an
 * adjective that runs from dark to bright ("Umbral", "Midnight", ... "Radiant"), the columns a noun of the
 * group's own ("Keel", "Anchor", "Engine" ...), so every name is two words and unique within its synth, and
 * the name says where on the grid the sound lies. A group gives each knob it cares about a range and an axis:
 * the adjective's (mostly the brightness: cutoff, tone), the noun's (mostly the shape: decay, resonance,
 * drive), or a seeded draw; the rest keep their defaults. The same index is always the same sound.
 *
 * **What they leave alone.** A preset is the sound, not the mix: level, pan, the sends, the low cut, the
 * distance, the drone's auto pan, the tape keys' spread stay where the mix and the composer put them; so do
 * the atmosphere's amounts (wind, sweeps, bleeps, grains), which the composer moves along the form.
 *
 * **Tuning.** Nothing a preset sets moves the pitch away from the rest: there is no transposition among the
 * knobs, the detune stays between 0 and 3 cents in the foundation (the drone, the bass groups: the pure
 * intervals of the dark-ambient addon) and at most 12 elsewhere, the drift at most 7, the lead's vibrato at
 * most 40 cents, the tape's wow, flutter and motor load inside the range of their defaults.
 */
#pragma once
#include "eph/Params.h"
#include <string>
#include <utility>
#include <vector>

namespace eph {

/** @brief One preset: its group (a submenu), its name, and the values it sets (knob index in its module, value). */
struct SoundPreset {
    std::string group;
    std::string name;
    std::vector<std::pair<int, float>> values;
};

/**
 * @brief The factory presets of a synth: Module::Voice, Lead, Drone, Tape, Strings, Drums or Atmos; empty for
 *        any other module. Built once, thread-safe.
 */
const std::vector<SoundPreset>& factoryPresets(Module module);

/** @brief Whether a preset leaves knob @p k of @p module alone (the mix, the composer's amounts). */
bool presetLeaves(Module module, int k);

/**
 * @brief The values a preset gives every knob of its module it does not leave: its own, the defaults for the
 *        rest -- the whole sound, so a preset sounds the same whatever was loaded before.
 */
std::vector<std::pair<int, float>> presetKnobs(Module module, const SoundPreset& preset);

/** @brief Applies @p preset to instance @p instance of @p module in @p params (the offline tools and the tests). */
void applyPreset(ParamStore& params, Module module, int instance, const SoundPreset& preset);

} // namespace eph
