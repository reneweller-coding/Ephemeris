/**
 * @file Presets.cpp
 * @brief The factory presets: sixteen groups per synth, sixty-four presets per group (Presets.h).
 * @author Rene Weller
 */
#include "eph/Presets.h"
#include "eph/synth/Wavetable.h"
#include "eph/synth/Filters.h"
#include "eph/synth/Modulation.h"
#include "eph/Dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace eph {

namespace {

/** @brief A knob's range in a group and the axis that moves it: 'A' the adjective's, 'B' the noun's, 'R' a draw. */
struct Axis {
    int m_k;   ///< the knob (its index in the module)
    float m_lo;   ///< the low end of its range in the group
    float m_hi;   ///< the high end
    char m_axis;   ///< what moves it: 'A' the adjective, 'B' the noun, 'R' a draw
};

/** @brief A group: its name, which of the synth's four adjective rows it uses, its nouns and its knobs. */
struct Group {
    const char* m_name;   ///< the group's name
    int m_adjectives;   ///< which of the synth's four adjective rows it uses
    const char* m_nouns[8];   ///< its eight nouns
    std::vector<Axis> m_axes;   ///< its knobs
};

/** @brief A synth: four rows of adjectives, each from dark to bright, and its sixteen groups. */
struct Synth {
    const char* m_adjectives[4][8];   ///< four rows of eight adjectives, each from dark to bright
    std::vector<Group> m_groups;   ///< its sixteen groups
};

// --- The modular voice of the rows --------------------------------------------------------------------------
namespace vo = voice;
/**
 * @brief The modular voice of the rows: its names and groups.
 * @return the voice's names and groups
 */
const Synth& voiceSynth()
{
    static const Synth s{
        { { "Umbral", "Midnight", "Smoky", "Amber", "Copper", "Golden", "Silver", "Radiant" },
          { "Buried", "Nocturnal", "Dusky", "Velvet", "Burnished", "Lucid", "Crystal", "Solar" },
          { "Obsidian", "Slate", "Iron", "Bronze", "Brass", "Chrome", "Neon", "Stellar" },
          { "Abyssal", "Shadowed", "Misty", "Hazy", "Pale", "Clear", "Bright", "Blazing" } },
        {
            { "Ladder Bass", 0, { "Keel", "Anchor", "Engine", "Undertow", "Foundry", "Bedrock", "Axle", "Piston" },
              { { vo::WAVE, 0.0f, 0.15f, 'R' }, { vo::DETUNE, 0.0f, 3.0f, 'R' }, { vo::DRIFT, 0.5f, 2.0f, 'R' }, { vo::DRIVE, 4.0f, 12.0f, 'B' },
                { vo::CUTOFF, 160.0f, 700.0f, 'A' }, { vo::RESONANCE, 0.1f, 0.35f, 'B' }, { vo::ENV_AMOUNT, 1.5f, 3.5f, 'A' },
                { vo::DECAY, 120.0f, 450.0f, 'B' }, { vo::KEY_TRACK, 0.3f, 0.6f, 'R' }, { vo::ACCENT, 0.4f, 0.7f, 'R' },
                { vo::AMP_DECAY, 60.0f, 250.0f, 'B' }, { vo::GLIDE, 20.0f, 90.0f, 'R' } } },
            { "Deep Ostinato", 1, { "Current", "Tide", "Pendulum", "Heartbeat", "Loop", "Circuit", "Treadmill", "Rotor" },
              { { vo::WAVE, 0.0f, 0.35f, 'B' }, { vo::DETUNE, 0.0f, 2.0f, 'R' }, { vo::DRIFT, 0.5f, 2.0f, 'R' }, { vo::DRIVE, 2.0f, 8.0f, 'R' },
                { vo::CUTOFF, 110.0f, 380.0f, 'A' }, { vo::RESONANCE, 0.05f, 0.25f, 'R' }, { vo::ENV_AMOUNT, 0.8f, 2.2f, 'A' },
                { vo::DECAY, 200.0f, 700.0f, 'B' }, { vo::AMP_DECAY, 150.0f, 500.0f, 'B' }, { vo::GLIDE, 30.0f, 120.0f, 'R' } } },
            { "Pluck Sequence", 2, { "Harp", "Quill", "Pizzicato", "Droplet", "Needle", "Spark", "Plectrum", "Pebble" },
              { { vo::WAVE, 0.0f, 0.5f, 'R' }, { vo::DETUNE, 3.0f, 8.0f, 'R' }, { vo::DRIFT, 1.0f, 3.0f, 'R' }, { vo::DRIVE, 3.0f, 9.0f, 'R' },
                { vo::CUTOFF, 300.0f, 1500.0f, 'A' }, { vo::RESONANCE, 0.2f, 0.5f, 'B' }, { vo::ENV_AMOUNT, 2.5f, 5.0f, 'A' },
                { vo::DECAY, 40.0f, 180.0f, 'B' }, { vo::KEY_TRACK, 0.5f, 0.9f, 'R' }, { vo::AMP_DECAY, 30.0f, 150.0f, 'B' }, { vo::GLIDE, 5.0f, 30.0f, 'R' } } },
            { "Resonant Sweep", 3, { "Arc", "Comet", "Crescent", "Horizon", "Sweep", "Aurora", "Nebula", "Parabola" },
              { { vo::WAVE, 0.0f, 0.3f, 'R' }, { vo::DETUNE, 3.0f, 8.0f, 'R' }, { vo::DRIVE, 4.0f, 10.0f, 'R' }, { vo::CUTOFF, 250.0f, 1200.0f, 'A' },
                { vo::RESONANCE, 0.45f, 0.75f, 'B' }, { vo::ENV_AMOUNT, 2.0f, 4.5f, 'A' }, { vo::DECAY, 150.0f, 600.0f, 'B' },
                { vo::AMP_DECAY, 80.0f, 300.0f, 'R' } } },
            { "Hollow Pulse", 0, { "Chamber", "Vessel", "Cavern", "Pipe", "Shell", "Hollow", "Grotto", "Flute" },
              { { vo::WAVE, 0.7f, 1.0f, 'R' }, { vo::PULSE_WIDTH, 0.12f, 0.45f, 'A' }, { vo::DETUNE, 4.0f, 10.0f, 'R' },
                { vo::CUTOFF, 400.0f, 2500.0f, 'B' }, { vo::RESONANCE, 0.15f, 0.4f, 'R' }, { vo::ENV_AMOUNT, 1.5f, 3.5f, 'R' },
                { vo::DECAY, 100.0f, 400.0f, 'R' }, { vo::AMP_DECAY, 60.0f, 250.0f, 'R' } } },
            { "Glass Arp", 1, { "Prism", "Lattice", "Mirror", "Icicle", "Facet", "Lens", "Quartz", "Filament" },
              { { vo::WAVE, 0.4f, 0.9f, 'R' }, { vo::PULSE_WIDTH, 0.2f, 0.45f, 'R' }, { vo::DETUNE, 3.0f, 7.0f, 'R' },
                { vo::CUTOFF, 1500.0f, 6000.0f, 'A' }, { vo::RESONANCE, 0.1f, 0.3f, 'B' }, { vo::ENV_AMOUNT, 1.0f, 3.0f, 'R' },
                { vo::DECAY, 60.0f, 250.0f, 'B' }, { vo::KEY_TRACK, 0.6f, 1.0f, 'R' }, { vo::AMP_DECAY, 40.0f, 200.0f, 'R' } } },
            { "Tape Sequence", 2, { "Reel", "Spool", "Cassette", "Capstan", "Loopback", "Splice", "Leader", "Tapehead" },
              { { vo::WAVE, 0.0f, 0.5f, 'R' }, { vo::DETUNE, 6.0f, 12.0f, 'A' }, { vo::DRIFT, 3.0f, 6.0f, 'B' }, { vo::DRIVE, 6.0f, 14.0f, 'R' },
                { vo::CUTOFF, 500.0f, 2500.0f, 'A' }, { vo::RESONANCE, 0.15f, 0.35f, 'R' }, { vo::ENV_AMOUNT, 1.5f, 3.0f, 'R' },
                { vo::DECAY, 120.0f, 400.0f, 'B' }, { vo::AMP_DECAY, 60.0f, 250.0f, 'R' } } },
            { "Soft Pad Voice", 3, { "Haze", "Veil", "Mist", "Cloudbank", "Breath", "Down", "Fleece", "Murmur" },
              { { vo::WAVE, 0.0f, 0.4f, 'R' }, { vo::DETUNE, 5.0f, 10.0f, 'R' }, { vo::DRIVE, 0.0f, 4.0f, 'R' }, { vo::CUTOFF, 150.0f, 500.0f, 'A' },
                { vo::RESONANCE, 0.0f, 0.15f, 'R' }, { vo::ENV_AMOUNT, 0.5f, 1.5f, 'B' }, { vo::DECAY, 300.0f, 1200.0f, 'R' },
                { vo::AMP_DECAY, 200.0f, 800.0f, 'B' }, { vo::GLIDE, 40.0f, 150.0f, 'R' } } },
            { "Squelch Arp", 0, { "Circuitry", "Squall", "Gremlin", "Relay", "Sprocket", "Gear", "Zapper", "Coil" },
              { { vo::WAVE, 0.0f, 0.4f, 'R' }, { vo::DETUNE, 2.0f, 6.0f, 'R' }, { vo::DRIVE, 8.0f, 16.0f, 'B' }, { vo::CUTOFF, 200.0f, 900.0f, 'A' },
                { vo::RESONANCE, 0.55f, 0.8f, 'B' }, { vo::ENV_AMOUNT, 3.0f, 5.5f, 'A' }, { vo::DECAY, 80.0f, 250.0f, 'R' },
                { vo::AMP_DECAY, 40.0f, 180.0f, 'R' } } },
            { "Staccato Pulse", 1, { "Morse", "Ticker", "Metronome", "Telegraph", "Beacon", "Signal", "Blip", "Semaphore" },
              { { vo::WAVE, 0.3f, 1.0f, 'R' }, { vo::PULSE_WIDTH, 0.2f, 0.5f, 'R' }, { vo::DETUNE, 3.0f, 7.0f, 'R' },
                { vo::CUTOFF, 500.0f, 2500.0f, 'A' }, { vo::RESONANCE, 0.1f, 0.35f, 'R' }, { vo::ENV_AMOUNT, 1.5f, 3.5f, 'R' },
                { vo::DECAY, 30.0f, 120.0f, 'B' }, { vo::AMP_DECAY, 15.0f, 60.0f, 'B' } } },
            { "Legato Glide", 2, { "Glider", "Slipstream", "Wake", "Contrail", "Glissade", "Ribbon", "Sailplane", "Updraft" },
              { { vo::WAVE, 0.0f, 0.6f, 'R' }, { vo::DETUNE, 3.0f, 8.0f, 'R' }, { vo::CUTOFF, 300.0f, 1500.0f, 'A' },
                { vo::RESONANCE, 0.15f, 0.4f, 'R' }, { vo::ENV_AMOUNT, 1.0f, 3.0f, 'R' }, { vo::DECAY, 300.0f, 900.0f, 'B' },
                { vo::AMP_DECAY, 300.0f, 900.0f, 'B' }, { vo::GLIDE, 120.0f, 400.0f, 'A' } } },
            { "Warm Unison", 3, { "Hearth", "Ember", "Lantern", "Candle", "Kiln", "Fireside", "Furnace", "Brazier" },
              { { vo::WAVE, 0.0f, 0.3f, 'R' }, { vo::DETUNE, 8.0f, 12.0f, 'A' }, { vo::DRIFT, 2.0f, 4.0f, 'R' }, { vo::DRIVE, 3.0f, 8.0f, 'R' },
                { vo::CUTOFF, 600.0f, 2000.0f, 'B' }, { vo::RESONANCE, 0.1f, 0.3f, 'R' }, { vo::ENV_AMOUNT, 1.0f, 2.5f, 'R' },
                { vo::DECAY, 150.0f, 500.0f, 'R' }, { vo::AMP_DECAY, 80.0f, 300.0f, 'R' } } },
            { "Bright Stab", 0, { "Flare", "Flash", "Strobe", "Blade", "Shard", "Spike", "Laser", "Glint" },
              { { vo::WAVE, 0.2f, 0.8f, 'R' }, { vo::DETUNE, 4.0f, 9.0f, 'R' }, { vo::CUTOFF, 2000.0f, 8000.0f, 'A' },
                { vo::RESONANCE, 0.1f, 0.3f, 'R' }, { vo::ENV_AMOUNT, 1.0f, 2.0f, 'R' }, { vo::DECAY, 50.0f, 200.0f, 'B' },
                { vo::AMP_DECAY, 80.0f, 250.0f, 'B' } } },
            { "Dark Throb", 1, { "Tremor", "Quake", "Magma", "Rift", "Chasm", "Growl", "Rumble", "Maelstrom" },
              { { vo::WAVE, 0.0f, 0.2f, 'R' }, { vo::DETUNE, 0.0f, 4.0f, 'R' }, { vo::DRIVE, 8.0f, 16.0f, 'R' }, { vo::CUTOFF, 80.0f, 260.0f, 'A' },
                { vo::RESONANCE, 0.3f, 0.55f, 'B' }, { vo::ENV_AMOUNT, 2.0f, 4.0f, 'R' }, { vo::DECAY, 250.0f, 800.0f, 'B' },
                { vo::AMP_DECAY, 100.0f, 400.0f, 'R' } } },
            { "Accent Ratchet", 2, { "Ratchet", "Rattle", "Stutter", "Clockwork", "Escapement", "Gallop", "Trigger", "Hammer" },
              { { vo::WAVE, 0.0f, 0.7f, 'R' }, { vo::DETUNE, 2.0f, 6.0f, 'R' }, { vo::ACCENT, 0.6f, 1.0f, 'B' }, { vo::CUTOFF, 400.0f, 2000.0f, 'A' },
                { vo::RESONANCE, 0.2f, 0.45f, 'R' }, { vo::ENV_AMOUNT, 2.0f, 4.0f, 'R' }, { vo::DECAY, 40.0f, 150.0f, 'B' },
                { vo::AMP_DECAY, 25.0f, 90.0f, 'R' } } },
            { "Cosmic Drip", 3, { "Pulsar", "Quasar", "Orbit", "Satellite", "Meteor", "Planetoid", "Starfall", "Zenith" },
              { { vo::WAVE, 0.2f, 0.7f, 'R' }, { vo::DETUNE, 4.0f, 9.0f, 'R' }, { vo::RESONANCE, 0.35f, 0.6f, 'B' },
                { vo::CUTOFF, 800.0f, 3000.0f, 'A' }, { vo::ENV_AMOUNT, 1.5f, 4.0f, 'R' }, { vo::DECAY, 100.0f, 400.0f, 'B' },
                { vo::KEY_TRACK, 0.8f, 1.0f, 'R' }, { vo::GLIDE, 40.0f, 150.0f, 'R' }, { vo::AMP_DECAY, 60.0f, 240.0f, 'R' } } },
        } };
    return s;
}

// --- The lead ---------------------------------------------------------------------------------------------------
namespace le = lead;
/**
 * @brief The lead's names and groups.
 * @return the lead's names and groups
 */
const Synth& leadSynth()
{
    static const Synth s{
        { { "Distant", "Lonely", "Dreaming", "Wandering", "Soaring", "Singing", "Shining", "Blazing" },
          { "Hushed", "Silken", "Mellow", "Tender", "Vivid", "Keen", "Piercing", "Brilliant" },
          { "Moonlit", "Starlit", "Twilit", "Misty", "Clear", "Sunlit", "Gleaming", "Dazzling" },
          { "Hollow", "Grainy", "Rustic", "Smooth", "Polished", "Glossy", "Glassy", "Sparkling" } },
        {
            { "Solo Saw", 0, { "Voyager", "Wanderer", "Pilgrim", "Navigator", "Nomad", "Seeker", "Traveller", "Rover" },
              { { le::WAVE, 0.0f, 0.2f, 'R' }, { le::DETUNE, 2.0f, 6.0f, 'R' }, { le::DRIVE, 6.0f, 12.0f, 'R' }, { le::CUTOFF, 1000.0f, 4000.0f, 'A' },
                { le::RESONANCE, 0.15f, 0.35f, 'B' }, { le::ENV_AMOUNT, 1.0f, 3.0f, 'R' }, { le::DECAY, 300.0f, 900.0f, 'B' },
                { le::AMP_DECAY, 200.0f, 600.0f, 'R' }, { le::GLIDE, 60.0f, 150.0f, 'R' }, { le::VIBRATO, 12.0f, 24.0f, 'R' }, { le::VIBRATO_RATE, 4.6f, 5.8f, 'R' } } },
            { "Singing Pulse", 1, { "Aria", "Cantor", "Hymn", "Canticle", "Chant", "Refrain", "Ballad", "Carol" },
              { { le::WAVE, 0.7f, 1.0f, 'R' }, { le::PULSE_WIDTH, 0.2f, 0.45f, 'A' }, { le::DETUNE, 2.0f, 5.0f, 'R' }, { le::CUTOFF, 1200.0f, 4000.0f, 'B' },
                { le::RESONANCE, 0.1f, 0.3f, 'R' }, { le::VIBRATO, 15.0f, 30.0f, 'R' }, { le::GLIDE, 80.0f, 200.0f, 'R' }, { le::VIBRATO_RATE, 4.8f, 6.0f, 'R' } } },
            { "Flute Lead", 2, { "Reed", "Piccolo", "Ocarina", "Fife", "Recorder", "Panpipe", "Pipe", "Kaval" },
              { { le::WAVE, 0.6f, 0.9f, 'R' }, { le::PULSE_WIDTH, 0.4f, 0.5f, 'R' }, { le::DETUNE, 1.0f, 3.0f, 'R' }, { le::DRIVE, 0.0f, 4.0f, 'R' },
                { le::CUTOFF, 900.0f, 2500.0f, 'A' }, { le::RESONANCE, 0.02f, 0.15f, 'R' }, { le::ENV_AMOUNT, 0.3f, 1.2f, 'B' },
                { le::AMP_DECAY, 150.0f, 400.0f, 'R' }, { le::GLIDE, 20.0f, 60.0f, 'R' }, { le::VIBRATO, 8.0f, 20.0f, 'B' }, { le::VIBRATO_RATE, 4.8f, 5.8f, 'R' } } },
            { "Portamento", 3, { "Slide", "Swoop", "Glide", "Swan", "Heron", "Falcon", "Kestrel", "Albatross" },
              { { le::WAVE, 0.0f, 0.4f, 'R' }, { le::DETUNE, 2.0f, 6.0f, 'R' }, { le::CUTOFF, 800.0f, 3000.0f, 'A' }, { le::RESONANCE, 0.2f, 0.4f, 'R' },
                { le::GLIDE, 180.0f, 450.0f, 'B' }, { le::VIBRATO, 10.0f, 25.0f, 'R' }, { le::AMP_DECAY, 250.0f, 700.0f, 'R' } } },
            { "Glass Whistle", 0, { "Icewind", "Frost", "Glacier", "Snowfield", "Hailstone", "Rime", "Firn", "Floe" },
              { { le::WAVE, 0.5f, 0.9f, 'R' }, { le::DETUNE, 1.0f, 4.0f, 'R' }, { le::CUTOFF, 3000.0f, 8000.0f, 'A' }, { le::RESONANCE, 0.05f, 0.2f, 'R' },
                { le::ENV_AMOUNT, 0.5f, 1.5f, 'B' }, { le::VIBRATO, 5.0f, 15.0f, 'R' }, { le::GLIDE, 30.0f, 100.0f, 'R' } } },
            { "Theremin", 1, { "Ghost", "Phantom", "Specter", "Wraith", "Apparition", "Spirit", "Shade", "Revenant" },
              { { le::WAVE, 0.85f, 1.0f, 'R' }, { le::PULSE_WIDTH, 0.45f, 0.5f, 'R' }, { le::DETUNE, 0.0f, 2.0f, 'R' }, { le::CUTOFF, 700.0f, 2000.0f, 'A' },
                { le::RESONANCE, 0.0f, 0.1f, 'R' }, { le::GLIDE, 250.0f, 500.0f, 'B' }, { le::VIBRATO, 25.0f, 40.0f, 'B' }, { le::VIBRATO_RATE, 5.0f, 6.5f, 'R' },
                { le::AMP_DECAY, 300.0f, 800.0f, 'R' } } },
            { "Screaming Filter", 2, { "Tempest", "Cyclone", "Hurricane", "Typhoon", "Gale", "Storm", "Whirlwind", "Tornado" },
              { { le::WAVE, 0.0f, 0.3f, 'R' }, { le::DETUNE, 2.0f, 6.0f, 'R' }, { le::DRIVE, 10.0f, 18.0f, 'R' }, { le::CUTOFF, 1500.0f, 5000.0f, 'A' },
                { le::RESONANCE, 0.5f, 0.72f, 'B' }, { le::ENV_AMOUNT, 1.5f, 3.0f, 'R' }, { le::VIBRATO, 10.0f, 25.0f, 'R' } } },
            { "Soft Horn", 3, { "Horn", "Bugle", "Cornet", "Clarion", "Herald", "Fanfare", "Summons", "Call" },
              { { le::WAVE, 0.2f, 0.5f, 'R' }, { le::DETUNE, 2.0f, 5.0f, 'R' }, { le::CUTOFF, 600.0f, 1800.0f, 'A' }, { le::RESONANCE, 0.05f, 0.2f, 'R' },
                { le::ENV_AMOUNT, 1.5f, 3.0f, 'B' }, { le::DECAY, 150.0f, 400.0f, 'R' }, { le::AMP_DECAY, 200.0f, 500.0f, 'R' }, { le::VIBRATO, 6.0f, 16.0f, 'R' } } },
            { "Ethereal Sine", 0, { "Halo", "Aura", "Nimbus", "Corona", "Glow", "Radiance", "Luster", "Gleam" },
              { { le::WAVE, 0.9f, 1.0f, 'R' }, { le::PULSE_WIDTH, 0.48f, 0.5f, 'R' }, { le::DETUNE, 0.0f, 2.0f, 'R' }, { le::DRIVE, 0.0f, 2.0f, 'R' },
                { le::CUTOFF, 500.0f, 1300.0f, 'A' }, { le::RESONANCE, 0.0f, 0.05f, 'R' }, { le::VIBRATO, 5.0f, 15.0f, 'B' }, { le::GLIDE, 60.0f, 200.0f, 'R' } } },
            { "Brass Lead", 1, { "Trumpet", "Trombone", "Brasswind", "Bellow", "Proclamation", "Decree", "Anthem", "Salute" },
              { { le::WAVE, 0.0f, 0.3f, 'R' }, { le::DETUNE, 3.0f, 7.0f, 'R' }, { le::DRIVE, 8.0f, 14.0f, 'R' }, { le::CUTOFF, 800.0f, 2500.0f, 'A' },
                { le::RESONANCE, 0.1f, 0.3f, 'R' }, { le::ENV_AMOUNT, 2.0f, 4.0f, 'B' }, { le::DECAY, 100.0f, 300.0f, 'R' }, { le::VIBRATO, 8.0f, 18.0f, 'R' } } },
            { "Hollow Oboe", 2, { "Oboe", "Chalumeau", "Shawm", "Bassoon", "Duduk", "Cor", "Crumhorn", "Musette" },
              { { le::WAVE, 0.8f, 1.0f, 'R' }, { le::PULSE_WIDTH, 0.08f, 0.25f, 'A' }, { le::DETUNE, 1.0f, 4.0f, 'R' }, { le::CUTOFF, 1000.0f, 3000.0f, 'B' },
                { le::RESONANCE, 0.15f, 0.35f, 'R' }, { le::VIBRATO, 8.0f, 18.0f, 'R' } } },
            { "Twin Oscillator", 3, { "Gemini", "Twin", "Binary", "Duet", "Doublet", "Tandem", "Castor", "Pollux" },
              { { le::WAVE, 0.0f, 0.5f, 'R' }, { le::DETUNE, 5.0f, 10.0f, 'A' }, { le::CUTOFF, 1200.0f, 3500.0f, 'B' }, { le::RESONANCE, 0.1f, 0.3f, 'R' },
                { le::VIBRATO, 8.0f, 20.0f, 'R' }, { le::DRIFT, 1.0f, 3.0f, 'R' } } },
            { "Cosmic Siren", 0, { "Siren", "Signal", "Beacon", "Lighthouse", "Transmitter", "Emitter", "Broadcast", "Callsign" },
              { { le::WAVE, 0.3f, 0.8f, 'R' }, { le::DETUNE, 2.0f, 6.0f, 'R' }, { le::CUTOFF, 2000.0f, 6000.0f, 'A' }, { le::RESONANCE, 0.3f, 0.55f, 'R' },
                { le::GLIDE, 200.0f, 450.0f, 'B' }, { le::VIBRATO, 20.0f, 35.0f, 'R' } } },
            { "Warm Mono", 1, { "Solace", "Comfort", "Refuge", "Harbor", "Haven", "Shelter", "Sanctuary", "Retreat" },
              { { le::WAVE, 0.0f, 0.3f, 'R' }, { le::DETUNE, 2.0f, 5.0f, 'R' }, { le::DRIVE, 4.0f, 9.0f, 'R' }, { le::CUTOFF, 500.0f, 1500.0f, 'A' },
                { le::RESONANCE, 0.1f, 0.25f, 'R' }, { le::ENV_AMOUNT, 1.0f, 2.5f, 'B' }, { le::VIBRATO, 10.0f, 20.0f, 'R' } } },
            { "Bell Lead", 2, { "Bell", "Chime", "Carillon", "Tolling", "Peal", "Knell", "Campanile", "Belfry" },
              { { le::WAVE, 0.4f, 0.8f, 'R' }, { le::DETUNE, 2.0f, 5.0f, 'R' }, { le::CUTOFF, 2000.0f, 6000.0f, 'A' }, { le::RESONANCE, 0.2f, 0.4f, 'R' },
                { le::ENV_AMOUNT, 2.0f, 4.0f, 'R' }, { le::DECAY, 80.0f, 250.0f, 'B' }, { le::AMP_DECAY, 600.0f, 1500.0f, 'B' }, { le::VIBRATO, 0.0f, 8.0f, 'R' } } },
            { "Dusty Solo", 3, { "Relic", "Artifact", "Heirloom", "Memento", "Keepsake", "Souvenir", "Fossil", "Remnant" },
              { { le::WAVE, 0.0f, 0.5f, 'R' }, { le::DRIFT, 2.5f, 4.0f, 'A' }, { le::DETUNE, 3.0f, 8.0f, 'R' }, { le::CUTOFF, 700.0f, 2500.0f, 'B' },
                { le::DRIVE, 6.0f, 12.0f, 'R' }, { le::VIBRATO, 12.0f, 25.0f, 'R' } } },
        } };
    return s;
}

// --- The drone (the lead's table): the foundation, in pure intervals -----------------------------------------------
/**
 * @brief The drone's names and groups: the foundation, in pure intervals.
 * @return the drone's names and groups
 */
const Synth& droneSynth()
{
    // Every group: detune 0 .. 1 cent (the addon's pure foundation), no accent, a long release.
    auto base = [](std::vector<Axis> a) {
        a.push_back({ le::DETUNE, 0.0f, 1.0f, 'R' });
        a.push_back({ le::ACCENT, 0.0f, 0.0f, 'R' });
        a.push_back({ le::AMP_DECAY, 1200.0f, 2000.0f, 'R' });
        return a;
    };
    static const Synth s{
        { { "Sunken", "Buried", "Earthen", "Granite", "Ancient", "Hallowed", "Gilded", "Sunlit" },
          { "Nameless", "Silent", "Endless", "Patient", "Steady", "Glowing", "Burning", "Shining" },
          { "Black", "Ashen", "Grey", "Umber", "Ochre", "Russet", "Amber", "Ivory" },
          { "Polar", "Arctic", "Tundra", "Boreal", "Temperate", "Tropic", "Equatorial", "Solar" } },
        {
            { "Dark Bordun", 0, { "Bordun", "Ground", "Root", "Basis", "Plinth", "Pillar", "Pedestal", "Footing" },
              base({ { le::WAVE, 0.0f, 0.2f, 'R' }, { le::CUTOFF, 120.0f, 380.0f, 'A' }, { le::RESONANCE, 0.1f, 0.35f, 'B' }, { le::DRIVE, 2.0f, 6.0f, 'R' }, { le::DRIFT, 2.0f, 5.0f, 'R' } }) },
            { "Organ Pedal", 1, { "Organ", "Diapason", "Bourdon", "Principal", "Register", "Manual", "Console", "Pipework" },
              base({ { le::WAVE, 0.8f, 1.0f, 'R' }, { le::PULSE_WIDTH, 0.3f, 0.5f, 'A' }, { le::CUTOFF, 300.0f, 900.0f, 'B' }, { le::RESONANCE, 0.05f, 0.2f, 'R' }, { le::DRIVE, 0.0f, 3.0f, 'R' } }) },
            { "Pulse Hum", 2, { "Hum", "Drone", "Buzz", "Thrum", "Purr", "Whir", "Throb", "Murmur" },
              base({ { le::WAVE, 0.6f, 0.9f, 'R' }, { le::PULSE_WIDTH, 0.1f, 0.3f, 'A' }, { le::CUTOFF, 200.0f, 600.0f, 'B' }, { le::RESONANCE, 0.2f, 0.4f, 'R' } }) },
            { "Resonant Earth", 3, { "Canyon", "Gorge", "Ravine", "Basin", "Valley", "Crater", "Caldera", "Fjord" },
              base({ { le::WAVE, 0.0f, 0.3f, 'R' }, { le::CUTOFF, 150.0f, 450.0f, 'A' }, { le::RESONANCE, 0.45f, 0.7f, 'B' }, { le::DRIVE, 3.0f, 7.0f, 'R' } }) },
            { "Glowing Root", 0, { "Emberbed", "Coalface", "Cinder", "Hearthstone", "Glow", "Smoulder", "Brand", "Coal" },
              base({ { le::WAVE, 0.0f, 0.3f, 'R' }, { le::CUTOFF, 400.0f, 1200.0f, 'A' }, { le::RESONANCE, 0.15f, 0.35f, 'R' }, { le::DRIVE, 4.0f, 10.0f, 'B' } }) },
            { "Hollow Pipe", 1, { "Tube", "Conduit", "Culvert", "Tunnel", "Shaft", "Well", "Cistern", "Aqueduct" },
              base({ { le::WAVE, 0.9f, 1.0f, 'R' }, { le::PULSE_WIDTH, 0.08f, 0.2f, 'A' }, { le::CUTOFF, 300.0f, 800.0f, 'B' }, { le::RESONANCE, 0.1f, 0.3f, 'R' } }) },
            { "Warm Floor", 2, { "Loam", "Soil", "Humus", "Clay", "Peat", "Marl", "Silt", "Terra" },
              base({ { le::WAVE, 0.1f, 0.4f, 'R' }, { le::CUTOFF, 250.0f, 700.0f, 'A' }, { le::RESONANCE, 0.05f, 0.2f, 'R' }, { le::DRIVE, 5.0f, 12.0f, 'B' } }) },
            { "Filtered Void", 3, { "Void", "Vacuum", "Nadir", "Absence", "Stillness", "Vastness", "Nullspace", "Deepfield" },
              base({ { le::WAVE, 0.0f, 0.2f, 'R' }, { le::CUTOFF, 80.0f, 200.0f, 'A' }, { le::RESONANCE, 0.2f, 0.5f, 'B' }, { le::DRIFT, 3.0f, 6.0f, 'R' } }) },
            { "Singing Ground", 0, { "Hymnal", "Psalm", "Plainsong", "Vespers", "Matins", "Compline", "Litany", "Requiem" },
              base({ { le::WAVE, 0.2f, 0.5f, 'R' }, { le::CUTOFF, 500.0f, 1500.0f, 'A' }, { le::RESONANCE, 0.3f, 0.5f, 'B' }, { le::VIBRATO, 1.0f, 3.0f, 'R' }, { le::VIBRATO_RATE, 1.5f, 3.0f, 'R' } }) },
            { "Iron Drone", 1, { "Anvil", "Forge", "Smelter", "Crucible", "Girder", "Rivet", "Bulkhead", "Hull" },
              base({ { le::WAVE, 0.0f, 0.3f, 'R' }, { le::DRIVE, 12.0f, 20.0f, 'B' }, { le::CUTOFF, 200.0f, 700.0f, 'A' }, { le::RESONANCE, 0.2f, 0.4f, 'R' } }) },
            { "Soft Monolith", 2, { "Monolith", "Obelisk", "Menhir", "Dolmen", "Cairn", "Stele", "Megalith", "Henge" },
              base({ { le::WAVE, 0.5f, 0.8f, 'R' }, { le::CUTOFF, 150.0f, 400.0f, 'A' }, { le::RESONANCE, 0.0f, 0.15f, 'B' }, { le::DRIVE, 0.0f, 3.0f, 'R' } }) },
            { "Bright Axis", 3, { "Axis", "Meridian", "Equator", "Latitude", "Longitude", "Zenith", "Solstice", "Equinox" },
              base({ { le::WAVE, 0.2f, 0.6f, 'R' }, { le::CUTOFF, 1000.0f, 3000.0f, 'A' }, { le::RESONANCE, 0.1f, 0.3f, 'B' } }) },
            { "Deep Current", 0, { "Gulfstream", "Riptide", "Upwelling", "Thermocline", "Seafloor", "Trench", "Seamount", "Benthos" },
              base({ { le::WAVE, 0.0f, 0.2f, 'R' }, { le::CUTOFF, 100.0f, 300.0f, 'A' }, { le::ENV_AMOUNT, 0.5f, 2.0f, 'B' }, { le::DECAY, 800.0f, 2000.0f, 'R' } }) },
            { "Breathing Root", 1, { "Lungs", "Bellows", "Heave", "Swell", "Respite", "Sigh", "Inhale", "Exhale" },
              base({ { le::WAVE, 0.0f, 0.4f, 'R' }, { le::DRIFT, 4.0f, 7.0f, 'B' }, { le::CUTOFF, 200.0f, 600.0f, 'A' }, { le::RESONANCE, 0.15f, 0.35f, 'R' } }) },
            { "Temple Hum", 2, { "Temple", "Shrine", "Sanctum", "Crypt", "Chapel", "Cloister", "Nave", "Apse" },
              base({ { le::WAVE, 0.7f, 1.0f, 'R' }, { le::PULSE_WIDTH, 0.35f, 0.5f, 'R' }, { le::CUTOFF, 250.0f, 700.0f, 'A' }, { le::RESONANCE, 0.25f, 0.45f, 'B' } }) },
            { "Night Floor", 3, { "Nightfall", "Dusk", "Gloaming", "Eventide", "Vigil", "Nocturne", "Nightwatch", "Moonset" },
              base({ { le::WAVE, 0.0f, 0.3f, 'R' }, { le::CUTOFF, 100.0f, 260.0f, 'A' }, { le::RESONANCE, 0.05f, 0.25f, 'R' }, { le::DRIVE, 1.0f, 5.0f, 'B' } }) },
        } };
    return s;
}

// --- The tape keys: choir, strings and flute tapes --------------------------------------------------------------------
/**
 * @brief The tape keys' names and groups: choir, strings and flute tapes.
 * @return the tape keys' names and groups
 */
const Synth& tapeSynth()
{
    // The tapes' wobble stays inside the range of the defaults, so the keyboard stays in tune with the rest.
    static const Synth s{
        { { "Faded", "Dusty", "Worn", "Mellow", "Warm", "Gentle", "Clear", "Bright" },
          { "Forgotten", "Distant", "Old", "Quiet", "Tender", "Sweet", "Airy", "Shimmering" },
          { "Brittle", "Crackled", "Weathered", "Aged", "Seasoned", "Mended", "Restored", "Pristine" },
          { "Sepia", "Umber", "Russet", "Rose", "Honey", "Cream", "Pearl", "Snow" } },
        {
            { "Cathedral Choir", 0, { "Cathedral", "Basilica", "Abbey", "Minster", "Chancel", "Transept", "Choirloft", "Vault" },
              { { tape::SET, 0.0f, 0.0f, 'R' }, { tape::VOWEL, 0.1f, 0.4f, 'B' }, { tape::TONE, 5000.0f, 9000.0f, 'A' }, { tape::AGE, 0.2f, 0.5f, 'R' },
                { tape::WOW, 3.0f, 6.0f, 'R' }, { tape::FLUTTER, 1.0f, 2.0f, 'R' }, { tape::SAG, 0.5f, 1.2f, 'R' } } },
            { "Dark Choir", 1, { "Lament", "Dirge", "Elegy", "Threnody", "Mourners", "Keening", "Wake", "Pall" },
              { { tape::SET, 0.0f, 0.0f, 'R' }, { tape::VOWEL, 0.0f, 0.3f, 'R' }, { tape::TONE, 2000.0f, 4500.0f, 'A' }, { tape::AGE, 0.4f, 0.7f, 'B' },
                { tape::WOW, 4.0f, 8.0f, 'R' } } },
            { "Worn Choir", 2, { "Archive", "Attic", "Scrapbook", "Photograph", "Letter", "Diary", "Almanac", "Ledger" },
              { { tape::SET, 0.0f, 0.0f, 'R' }, { tape::AGE, 0.7f, 1.0f, 'A' }, { tape::WOW, 7.0f, 12.0f, 'B' }, { tape::FLUTTER, 2.0f, 4.0f, 'R' },
                { tape::TONE, 3000.0f, 6000.0f, 'R' } } },
            { "Bright Choir", 3, { "Seraph", "Cherub", "Angels", "Heralds", "Hosanna", "Gloria", "Alleluia", "Sanctus" },
              { { tape::SET, 0.0f, 0.0f, 'R' }, { tape::TONE, 8000.0f, 14000.0f, 'A' }, { tape::AGE, 0.0f, 0.3f, 'R' }, { tape::VOWEL, 0.3f, 0.7f, 'B' },
                { tape::WOW, 2.0f, 4.0f, 'R' } } },
            { "Vowel Choir", 0, { "Vowels", "Syllables", "Phonemes", "Diphthong", "Whispers", "Utterance", "Murmurs", "Breaths" },
              { { tape::SET, 0.0f, 0.0f, 'R' }, { tape::VOWEL, 0.0f, 1.0f, 'A' }, { tape::TONE, 4000.0f, 8000.0f, 'B' }, { tape::AGE, 0.3f, 0.6f, 'R' } } },
            { "Ghost Choir", 1, { "Ghosts", "Phantoms", "Echoes", "Shadows", "Spirits", "Revenants", "Wraiths", "Specters" },
              { { tape::SET, 0.0f, 0.0f, 'R' }, { tape::TONE, 2500.0f, 5000.0f, 'A' }, { tape::AGE, 0.6f, 0.9f, 'R' }, { tape::WOW, 6.0f, 10.0f, 'B' },
                { tape::VOWEL, 0.5f, 0.9f, 'R' } } },
            { "Silk Strings", 2, { "Silk", "Satin", "Chiffon", "Gossamer", "Organza", "Lace", "Taffeta", "Voile" },
              { { tape::SET, 1.0f, 1.0f, 'R' }, { tape::TONE, 5000.0f, 10000.0f, 'A' }, { tape::AGE, 0.1f, 0.4f, 'B' }, { tape::WOW, 2.0f, 5.0f, 'R' } } },
            { "Worn Strings", 3, { "Gramophone", "Phonograph", "Wireless", "Parlour", "Salon", "Ballroom", "Tearoom", "Veranda" },
              { { tape::SET, 1.0f, 1.0f, 'R' }, { tape::AGE, 0.6f, 1.0f, 'A' }, { tape::WOW, 6.0f, 11.0f, 'B' }, { tape::FLUTTER, 1.5f, 3.5f, 'R' } } },
            { "Dark Strings", 0, { "Autumn", "November", "Winter", "Harvest", "Fallow", "Twilight", "Evensong", "Candlemas" },
              { { tape::SET, 1.0f, 1.0f, 'R' }, { tape::TONE, 1800.0f, 4000.0f, 'A' }, { tape::AGE, 0.3f, 0.6f, 'B' }, { tape::WOW, 3.0f, 7.0f, 'R' } } },
            { "Bright Strings", 1, { "Spring", "April", "Meadow", "Blossom", "Morning", "Daybreak", "Sunrise", "Summer" },
              { { tape::SET, 1.0f, 1.0f, 'R' }, { tape::TONE, 9000.0f, 15000.0f, 'A' }, { tape::AGE, 0.0f, 0.3f, 'R' }, { tape::WOW, 2.0f, 4.0f, 'B' } } },
            { "Warped Strings", 2, { "Warp", "Wobble", "Mirage", "Carousel", "Funhouse", "Seasick", "Swoon", "Vertigo" },
              { { tape::SET, 1.0f, 1.0f, 'R' }, { tape::WOW, 9.0f, 12.0f, 'A' }, { tape::SAG, 1.5f, 2.5f, 'B' }, { tape::FLUTTER, 2.0f, 4.0f, 'R' } } },
            { "Pure Flute", 3, { "Brook", "Stream", "Rill", "Creek", "Rivulet", "Fountain", "Cascade", "Wellspring" },
              { { tape::SET, 2.0f, 2.0f, 'R' }, { tape::TONE, 6000.0f, 12000.0f, 'A' }, { tape::AGE, 0.0f, 0.3f, 'B' }, { tape::WOW, 2.0f, 4.0f, 'R' } } },
            { "Breathy Flute", 0, { "Breeze", "Zephyr", "Draught", "Gust", "Puff", "Waft", "Airflow", "Sough" },
              { { tape::SET, 2.0f, 2.0f, 'R' }, { tape::TONE, 4000.0f, 8000.0f, 'A' }, { tape::AGE, 0.3f, 0.6f, 'B' }, { tape::VOWEL, 0.3f, 0.8f, 'R' } } },
            { "Worn Flute", 1, { "Shepherd", "Herder", "Hillside", "Pasture", "Flock", "Crook", "Fold", "Heath" },
              { { tape::SET, 2.0f, 2.0f, 'R' }, { tape::AGE, 0.6f, 1.0f, 'A' }, { tape::WOW, 6.0f, 11.0f, 'B' }, { tape::TONE, 3000.0f, 7000.0f, 'R' } } },
            { "Dark Flute", 2, { "Forest", "Thicket", "Grove", "Copse", "Bramble", "Undergrowth", "Wildwood", "Deadwood" },
              { { tape::SET, 2.0f, 2.0f, 'R' }, { tape::TONE, 1800.0f, 4000.0f, 'A' }, { tape::AGE, 0.2f, 0.6f, 'B' } } },
            { "Wobbly Flute", 3, { "Kite", "Balloon", "Pinwheel", "Whirligig", "Weathervane", "Windmill", "Mobile", "Yoyo" },
              { { tape::SET, 2.0f, 2.0f, 'R' }, { tape::WOW, 8.0f, 12.0f, 'A' }, { tape::FLUTTER, 2.5f, 4.0f, 'B' }, { tape::SAG, 1.0f, 2.5f, 'R' } } },
        } };
    return s;
}

// --- The string machine ---------------------------------------------------------------------------------------------------
namespace st = strings;
/**
 * @brief The string machine's names and groups.
 * @return the string machine's names and groups
 */
const Synth& stringsSynth()
{
    static const Synth s{
        { { "Velvet", "Dusky", "Hazy", "Soft", "Silken", "Lustrous", "Radiant", "Luminous" },
          { "Frozen", "Wintry", "Cool", "Calm", "Serene", "Balmy", "Sunny", "Golden" },
          { "Deep", "Dark", "Shaded", "Muted", "Glowing", "Bright", "Vivid", "Brilliant" },
          { "Slow", "Drifting", "Floating", "Hovering", "Rising", "Soaring", "Ascending", "Celestial" } },
        {
            { "Violin Section", 0, { "Bow", "Rosin", "Fiddle", "Scroll", "Fingerboard", "Tailpiece", "Soundpost", "Purfling" },
              { { st::REGISTRATION, 0.0f, 0.4f, 'R' }, { st::TONE, 3500.0f, 8000.0f, 'A' }, { st::ENSEMBLE, 0.6f, 0.9f, 'B' }, { st::ANIMATE, 0.1f, 0.3f, 'R' },
                { st::ATTACK, 0.2f, 0.8f, 'R' }, { st::RELEASE, 0.8f, 2.0f, 'R' } } },
            { "Viola Haze", 1, { "Serenade", "Sonatina", "Romance", "Pavane", "Siciliano", "Barcarolle", "Berceuse", "Reverie" },
              { { st::REGISTRATION, 0.8f, 1.3f, 'R' }, { st::TONE, 2500.0f, 6000.0f, 'A' }, { st::ENSEMBLE, 0.7f, 1.0f, 'B' }, { st::ANIMATE, 0.2f, 0.4f, 'R' } } },
            { "Cello Floor", 2, { "Mahogany", "Walnut", "Ebony", "Rosewood", "Maple", "Spruce", "Cedar", "Oak" },
              { { st::REGISTRATION, 1.8f, 2.3f, 'R' }, { st::FEET, 0.2f, 0.5f, 'R' }, { st::TONE, 1500.0f, 4000.0f, 'A' }, { st::ATTACK, 0.3f, 1.2f, 'B' } } },
            { "Bass Strings", 3, { "Cellar", "Vaults", "Catacomb", "Bedrock", "Foundations", "Cavern", "Grotto", "Undercroft" },
              { { st::REGISTRATION, 2.8f, 3.2f, 'R' }, { st::FEET, 0.1f, 0.4f, 'A' }, { st::TONE, 1200.0f, 3000.0f, 'B' } } },
            { "Full Ensemble", 0, { "Orchestra", "Symphony", "Ensemble", "Philharmonic", "Chorale", "Tutti", "Consort", "Assembly" },
              { { st::REGISTRATION, 3.8f, 4.3f, 'R' }, { st::TONE, 3000.0f, 7000.0f, 'A' }, { st::ENSEMBLE, 0.7f, 1.0f, 'B' } } },
            { "Hollow Strings", 1, { "Conch", "Gourd", "Husk", "Pod", "Calabash", "Nutshell", "Cocoon", "Seashell" },
              { { st::REGISTRATION, 4.8f, 5.3f, 'R' }, { st::TONE, 2500.0f, 6000.0f, 'A' }, { st::PHASER, 0.0f, 0.3f, 'B' } } },
            { "Brass Machine", 2, { "Fanfare", "Procession", "Parade", "Pageant", "Ceremony", "Coronation", "Triumph", "Jubilee" },
              { { st::REGISTRATION, 5.8f, 6.3f, 'R' }, { st::TONE, 3000.0f, 8000.0f, 'A' }, { st::ATTACK, 0.05f, 0.3f, 'B' }, { st::ENSEMBLE, 0.3f, 0.7f, 'R' } } },
            { "String Organ", 3, { "Harmonium", "Chapel", "Vestry", "Sacristy", "Parish", "Hymnbook", "Pew", "Steeple" },
              { { st::REGISTRATION, 6.7f, 7.0f, 'R' }, { st::TONE, 2500.0f, 6000.0f, 'A' }, { st::ENSEMBLE, 0.4f, 0.8f, 'B' } } },
            { "Animated Blend", 0, { "Kaleidoscope", "Chameleon", "Mosaic", "Tapestry", "Quilt", "Patchwork", "Collage", "Montage" },
              { { st::REGISTRATION, 0.0f, 7.0f, 'A' }, { st::ANIMATE, 0.6f, 1.0f, 'B' }, { st::ANIMATE_RATE, 0.02f, 0.12f, 'R' } } },
            { "Phased Strings", 1, { "Swirl", "Vortex", "Eddy", "Spiral", "Whirlpool", "Gyre", "Maelstrom", "Helix" },
              { { st::REGISTRATION, 0.0f, 4.0f, 'A' }, { st::PHASER, 0.4f, 0.9f, 'B' }, { st::ENSEMBLE, 0.6f, 0.9f, 'R' } } },
            { "Slow Swell", 2, { "Tidewater", "Swell", "Surge", "Floodtide", "Groundswell", "Upsurge", "Crest", "Billow" },
              { { st::ATTACK, 1.2f, 3.0f, 'A' }, { st::RELEASE, 2.0f, 5.0f, 'B' }, { st::REGISTRATION, 0.0f, 5.0f, 'R' } } },
            { "Short Bow", 3, { "Staccato", "Spiccato", "Pizzicato", "Detache", "Martele", "Sautille", "Ricochet", "Tremolo" },
              { { st::ATTACK, 0.01f, 0.08f, 'A' }, { st::RELEASE, 0.2f, 0.6f, 'B' }, { st::TONE, 3000.0f, 8000.0f, 'R' }, { st::REGISTRATION, 0.0f, 5.0f, 'R' } } },
            { "Wide Ensemble", 0, { "Panorama", "Vista", "Expanse", "Horizon", "Prairie", "Steppe", "Savanna", "Tundra" },
              { { st::ENSEMBLE_TYPE, 2.0f, 2.0f, 'R' }, { st::REGISTRATION, 0.0f, 5.0f, 'A' }, { st::ENSEMBLE, 0.7f, 1.0f, 'B' } } },
            { "Chorus Strings", 1, { "Chorus", "Unison", "Harmony", "Accord", "Concord", "Consonance", "Resonance", "Euphony" },
              { { st::ENSEMBLE_TYPE, 1.0f, 1.0f, 'R' }, { st::REGISTRATION, 0.0f, 5.0f, 'A' }, { st::ENSEMBLE, 0.6f, 1.0f, 'B' } } },
            { "Morphing Registers", 2, { "Metamorphosis", "Chrysalis", "Transfiguration", "Shapeshifter", "Mutation", "Evolution", "Transit", "Passage" },
              { { st::ANIMATE, 0.8f, 1.0f, 'A' }, { st::ANIMATE_RATE, 0.1f, 0.5f, 'B' }, { st::REGISTRATION, 1.0f, 6.0f, 'R' } } },
            { "Dark Solina", 3, { "Nightshade", "Belladonna", "Mandrake", "Hemlock", "Wolfsbane", "Foxglove", "Henbane", "Aconite" },
              { { st::TONE, 1000.0f, 2200.0f, 'A' }, { st::REGISTRATION, 0.0f, 4.0f, 'B' }, { st::ENSEMBLE, 0.8f, 1.0f, 'R' } } },
        } };
    return s;
}

// --- The drums --------------------------------------------------------------------------------------------------------------
namespace dr = drums;
/**
 * @brief The drums' names and groups.
 * @return the drums' names and groups
 */
const Synth& drumsSynth()
{
    static const Synth s{
        { { "Muffled", "Soft", "Warm", "Round", "Firm", "Punchy", "Snappy", "Crisp" },
          { "Distant", "Dusty", "Vintage", "Analog", "Tight", "Modern", "Polished", "Sharp" },
          { "Heavy", "Deep", "Low", "Dense", "Solid", "Lean", "Light", "Brisk" },
          { "Rusty", "Leaden", "Iron", "Steel", "Chrome", "Titanium", "Platinum", "Diamond" } },
        {
            { "Tight Kit", 0, { "Clasp", "Buckle", "Latch", "Clamp", "Bolt", "Lock", "Knot", "Stitch" },
              { { dr::KICK_HZ, 50.0f, 60.0f, 'A' }, { dr::DECAY, 0.3f, 0.6f, 'B' }, { dr::TONE, 0.4f, 0.7f, 'R' } } },
            { "Deep Kit", 1, { "Barrel", "Cask", "Keg", "Vat", "Cistern", "Silo", "Tank", "Hopper" },
              { { dr::KICK_HZ, 38.0f, 46.0f, 'A' }, { dr::DECAY, 0.9f, 1.6f, 'B' }, { dr::TONE, 0.2f, 0.5f, 'R' } } },
            { "Boom Kit", 2, { "Cannon", "Mortar", "Salvo", "Volley", "Broadside", "Thunderclap", "Detonation", "Blast" },
              { { dr::KICK_HZ, 40.0f, 50.0f, 'R' }, { dr::DECAY, 1.4f, 2.0f, 'A' }, { dr::TONE, 0.2f, 0.4f, 'B' } } },
            { "Dry Machine", 3, { "Robot", "Android", "Automaton", "Cyborg", "Mechanism", "Contraption", "Gadget", "Device" },
              { { dr::KICK_HZ, 48.0f, 58.0f, 'R' }, { dr::DECAY, 0.3f, 0.5f, 'A' }, { dr::TONE, 0.5f, 0.8f, 'B' } } },
            { "Click Kit", 0, { "Switch", "Toggle", "Button", "Lever", "Trigger", "Latchkey", "Keystroke", "Tick" },
              { { dr::KICK_HZ, 55.0f, 70.0f, 'A' }, { dr::DECAY, 0.3f, 0.5f, 'R' }, { dr::TONE, 0.7f, 1.0f, 'B' } } },
            { "Warm Analog", 1, { "Valve", "Tube", "Transistor", "Capacitor", "Resistor", "Diode", "Oscillator", "Rectifier" },
              { { dr::KICK_HZ, 44.0f, 52.0f, 'A' }, { dr::DECAY, 0.7f, 1.1f, 'B' }, { dr::TONE, 0.3f, 0.5f, 'R' } } },
            { "Bright Machine", 2, { "Photon", "Proton", "Electron", "Neutron", "Positron", "Muon", "Quark", "Gluon" },
              { { dr::KICK_HZ, 50.0f, 62.0f, 'R' }, { dr::DECAY, 0.5f, 0.9f, 'A' }, { dr::TONE, 0.7f, 0.95f, 'B' } } },
            { "Dark Machine", 3, { "Bunker", "Blockhouse", "Hangar", "Depot", "Warehouse", "Foundry", "Mill", "Refinery" },
              { { dr::KICK_HZ, 40.0f, 50.0f, 'R' }, { dr::DECAY, 0.6f, 1.2f, 'A' }, { dr::TONE, 0.05f, 0.3f, 'B' } } },
            { "Round Kit", 0, { "Marble", "Bead", "Pearl", "Orb", "Sphere", "Globe", "Ball", "Bubble" },
              { { dr::KICK_HZ, 45.0f, 55.0f, 'A' }, { dr::DECAY, 0.8f, 1.3f, 'R' }, { dr::TONE, 0.4f, 0.6f, 'B' } } },
            { "Punchy Kit", 1, { "Fist", "Knuckle", "Jab", "Hook", "Uppercut", "Punch", "Strike", "Blow" },
              { { dr::KICK_HZ, 52.0f, 64.0f, 'A' }, { dr::DECAY, 0.5f, 0.8f, 'B' }, { dr::TONE, 0.55f, 0.8f, 'R' } } },
            { "Sub Kit", 2, { "Aftershock", "Epicenter", "Fault", "Upheaval", "Subduction", "Seism", "Landslide", "Avalanche" },
              { { dr::KICK_HZ, 35.0f, 42.0f, 'A' }, { dr::DECAY, 1.0f, 1.8f, 'B' }, { dr::TONE, 0.1f, 0.3f, 'R' } } },
            { "Snappy Kit", 3, { "Whip", "Crack", "Snap", "Lash", "Flick", "Twig", "Branch", "Kindling" },
              { { dr::KICK_HZ, 55.0f, 68.0f, 'R' }, { dr::DECAY, 0.35f, 0.6f, 'A' }, { dr::TONE, 0.6f, 0.9f, 'B' } } },
            { "Soft Kit", 0, { "Pillow", "Cushion", "Felt", "Wool", "Velour", "Suede", "Moss", "Feather" },
              { { dr::KICK_HZ, 44.0f, 54.0f, 'R' }, { dr::DECAY, 0.6f, 1.0f, 'A' }, { dr::TONE, 0.1f, 0.35f, 'B' } } },
            { "Rock Kit", 1, { "Stadium", "Arena", "Garage", "Studio", "Stage", "Amphitheatre", "Coliseum", "Hall" },
              { { dr::KICK_HZ, 50.0f, 62.0f, 'A' }, { dr::DECAY, 0.8f, 1.2f, 'B' }, { dr::TONE, 0.5f, 0.75f, 'R' } } },
            { "Tribal Kit", 2, { "Ritual", "Rite", "Totem", "Bonfire", "Trance", "Dance", "Gathering", "Tribe" },
              { { dr::KICK_HZ, 40.0f, 55.0f, 'A' }, { dr::DECAY, 1.0f, 1.6f, 'B' }, { dr::TONE, 0.3f, 0.6f, 'R' } } },
            { "Metal Kit", 3, { "Scrap", "Chain", "Sheet", "Plate", "Pipe", "Rail", "Spring", "Grate" },
              { { dr::KICK_HZ, 58.0f, 80.0f, 'A' }, { dr::DECAY, 0.4f, 0.9f, 'B' }, { dr::TONE, 0.8f, 1.0f, 'R' } } },
        } };
    return s;
}

// --- The atmosphere: its colours; the amounts are the composer's ---------------------------------------------------------------
namespace at = atmos;
/**
 * @brief The atmosphere's names and groups: its colours (the amounts are the composer's).
 * @return the atmosphere's names and groups
 */
const Synth& atmosSynth()
{
    static const Synth s{
        { { "Silent", "Faint", "Quiet", "Soft", "Stirring", "Restless", "Rushing", "Howling" },
          { "Frozen", "Cold", "Cool", "Mild", "Warm", "Hot", "Searing", "Molten" },
          { "Dim", "Dusky", "Grey", "Pale", "Clear", "Bright", "Glaring", "Blinding" },
          { "Far", "Remote", "Outer", "Upper", "Lower", "Inner", "Near", "Close" } },
        {
            { "Polar Wind", 0, { "Icefield", "Permafrost", "Snowdrift", "Blizzard", "Whiteout", "Icecap", "Hoarfrost", "Frostline" },
              { { at::WIND_TONE, 150.0f, 450.0f, 'A' }, { at::SWEEP_LEVEL, -30.0f, -20.0f, 'B' }, { at::BLEEP_LEVEL, -35.0f, -28.0f, 'R' }, { at::GRAIN_DENSITY, 3.0f, 10.0f, 'R' } } },
            { "Desert Wind", 1, { "Dune", "Mesa", "Sirocco", "Mirage", "Oasis", "Sandstorm", "Erg", "Wadi" },
              { { at::WIND_TONE, 800.0f, 2000.0f, 'A' }, { at::SWEEP_LEVEL, -24.0f, -14.0f, 'B' }, { at::GRAIN_DENSITY, 6.0f, 16.0f, 'R' } } },
            { "Solar Wind", 2, { "Corona", "Prominence", "Sunspot", "Photosphere", "Chromosphere", "Heliosphere", "Sunwind", "Solarflare" },
              { { at::WIND_TONE, 1500.0f, 4000.0f, 'A' }, { at::BLEEP_LEVEL, -26.0f, -16.0f, 'B' }, { at::SWEEP_LEVEL, -24.0f, -16.0f, 'R' } } },
            { "Deep Space", 3, { "Nebula", "Cosmos", "Galaxy", "Quasar", "Pulsar", "Magnetar", "Blackhole", "Wormhole" },
              { { at::WIND_TONE, 150.0f, 400.0f, 'A' }, { at::SWEEP_LEVEL, -30.0f, -22.0f, 'R' }, { at::GRAIN_DENSITY, 2.0f, 6.0f, 'B' } } },
            { "Radio Static", 0, { "Static", "Shortwave", "Longwave", "Crackle", "Interference", "Frequency", "Wavelength", "Carrier" },
              { { at::WIND_TONE, 1000.0f, 3000.0f, 'R' }, { at::BLEEP_LEVEL, -20.0f, -12.0f, 'A' }, { at::GRAIN_DENSITY, 20.0f, 40.0f, 'B' } } },
            { "Night Sky", 1, { "Constellation", "Firmament", "Milkyway", "Zodiac", "Heavens", "Skyline", "Canopy", "Dome" },
              { { at::WIND_TONE, 300.0f, 800.0f, 'A' }, { at::GRAIN_DENSITY, 4.0f, 12.0f, 'B' }, { at::BLEEP_LEVEL, -30.0f, -22.0f, 'R' } } },
            { "Starfield", 2, { "Stardust", "Starlight", "Starburst", "Stargazer", "Starmap", "Starfall", "Starseed", "Starshine" },
              { { at::GRAIN_DENSITY, 15.0f, 40.0f, 'A' }, { at::WIND_TONE, 600.0f, 1500.0f, 'B' }, { at::BLEEP_LEVEL, -26.0f, -18.0f, 'R' } } },
            { "Ion Storm", 3, { "Ionosphere", "Plasma", "Lightning", "Thunderhead", "Squallline", "Stormfront", "Downburst", "Supercell" },
              { { at::SWEEP_LEVEL, -14.0f, -6.0f, 'A' }, { at::WIND_TONE, 800.0f, 2500.0f, 'B' } } },
            { "Satellite Chatter", 0, { "Telemetry", "Uplink", "Downlink", "Transponder", "Orbiter", "Probe", "Lander", "Relay" },
              { { at::BLEEP_LEVEL, -18.0f, -10.0f, 'A' }, { at::GRAIN_DENSITY, 8.0f, 20.0f, 'B' } } },
            { "Ocean Floor", 1, { "Seabed", "Reef", "Kelpforest", "Seagrass", "Lagoon", "Atoll", "Shoal", "Tidepool" },
              { { at::WIND_TONE, 150.0f, 350.0f, 'A' }, { at::SWEEP_LEVEL, -26.0f, -18.0f, 'B' }, { at::GRAIN_DENSITY, 1.0f, 4.0f, 'R' } } },
            { "Aurora", 2, { "Borealis", "Australis", "Northlights", "Skyfire", "Curtains", "Streamers", "Coronae", "Glowbands" },
              { { at::WIND_TONE, 500.0f, 1500.0f, 'A' }, { at::SWEEP_LEVEL, -20.0f, -10.0f, 'B' }, { at::GRAIN_DENSITY, 6.0f, 15.0f, 'R' } } },
            { "Dust Cloud", 3, { "Dust", "Pollen", "Spores", "Ash", "Soot", "Powder", "Haze", "Smoke" },
              { { at::GRAIN_DENSITY, 25.0f, 40.0f, 'A' }, { at::WIND_TONE, 400.0f, 1000.0f, 'B' } } },
            { "Cathedral Air", 0, { "Nave", "Cloister", "Crypt", "Belltower", "Rafters", "Vaulting", "Buttress", "Spire" },
              { { at::WIND_TONE, 250.0f, 600.0f, 'A' }, { at::BLEEP_LEVEL, -34.0f, -26.0f, 'R' }, { at::SWEEP_LEVEL, -28.0f, -18.0f, 'B' } } },
            { "Signal Drift", 1, { "Signal", "Transmission", "Broadcast", "Channel", "Band", "Waveband", "Numbers", "Station" },
              { { at::BLEEP_LEVEL, -22.0f, -12.0f, 'A' }, { at::WIND_TONE, 400.0f, 1200.0f, 'B' }, { at::GRAIN_DENSITY, 4.0f, 10.0f, 'R' } } },
            { "Magnetic Field", 2, { "Magnet", "Lodestone", "Compass", "Fluxline", "Dipole", "Magnetosphere", "Northpole", "Fieldline" },
              { { at::SWEEP_LEVEL, -18.0f, -8.0f, 'A' }, { at::GRAIN_DENSITY, 10.0f, 25.0f, 'B' } } },
            { "Ether", 3, { "Ether", "Aether", "Quintessence", "Pneuma", "Anima", "Essence", "Vapor", "Mist" },
              { { at::WIND_TONE, 200.0f, 600.0f, 'A' }, { at::GRAIN_DENSITY, 1.0f, 6.0f, 'B' }, { at::SWEEP_LEVEL, -30.0f, -20.0f, 'R' }, { at::BLEEP_LEVEL, -36.0f, -28.0f, 'R' } } },
        } };
    return s;
}

// --- The pad synth: analog pads on the PWM table, the Oberheim's brass, the PPG's and Waldorf's planes -----------------
namespace po = poly;
/**
 * @brief The index of a wavetable by name (Wavetable.h), as a knob value.
 * @param[in] name  the wavetable's name
 * @return its index as a knob value
 */
float tableOf(const char* name)
{
    for (int i = 0; i < WavetableCount; ++i) if (std::string(WavetableNames[i]) == name) return static_cast<float>(i);
    return 1.0f;
}
/**
 * @brief The pad synth's names and groups.
 * @return the pad synth's names and groups
 */
const Synth& polySynth()
{
    // Every group: a slow envelope and the ensemble unless it says otherwise; the detune within 14 cents.
    auto base = [](const char* first, const char* last, std::vector<Axis> a) {
        a.insert(a.begin(), { po::TABLE, tableOf(first), tableOf(last), 'R' });
        auto has = [&](int k) { for (const Axis& x : a) if (x.m_k == k) return true; return false; };
        if (!has(po::ATTACK)) a.push_back({ po::ATTACK, 0.8f, 2.5f, 'R' });
        if (!has(po::RELEASE)) a.push_back({ po::RELEASE, 2.5f, 6.0f, 'R' });
        if (!has(po::CHORUS)) a.push_back({ po::CHORUS, 0.3f, 0.7f, 'R' });
        if (!has(po::DETUNE)) a.push_back({ po::DETUNE, 4.0f, 12.0f, 'R' });
        if (!has(po::SCAN)) a.push_back({ po::SCAN, 0.2f, 0.6f, 'R' });
        if (!has(po::SCAN_RATE)) a.push_back({ po::SCAN_RATE, 0.02f, 0.1f, 'R' });
        return a;
    };
    static const Synth s{
        { { "Umbral", "Dusky", "Velvet", "Hazy", "Amber", "Golden", "Silver", "Radiant" },
          { "Nocturnal", "Shadowed", "Misty", "Soft", "Warm", "Luminous", "Crystal", "Solar" },
          { "Deep", "Sombre", "Muted", "Mellow", "Clear", "Bright", "Gleaming", "Blazing" },
          { "Abyssal", "Distant", "Veiled", "Drifting", "Floating", "Shining", "Starlit", "Celestial" } },
        {
            { "Analog Pad", 0, { "Horizon", "Plateau", "Tundra", "Steppe", "Prairie", "Savanna", "Mesa", "Delta" },
              base("PWM", "PWM", { { po::POSITION, 0.05f, 0.5f, 'B' }, { po::SCAN, 0.3f, 0.7f, 'R' }, { po::CUTOFF, 700.0f, 4000.0f, 'A' },
                                   { po::RESONANCE, 0.05f, 0.25f, 'R' }, { po::ENV_AMOUNT, 0.3f, 1.2f, 'R' }, { po::SPREAD, 0.4f, 0.8f, 'R' } }) },
            { "Juno Strings", 1, { "Ribbon", "Silk", "Satin", "Gossamer", "Chiffon", "Velour", "Tulle", "Organza" },
              base("PWM", "PWM", { { po::POSITION, 0.2f, 0.6f, 'R' }, { po::SCAN, 0.4f, 0.8f, 'R' }, { po::SCAN_RATE, 0.3f, 0.6f, 'R' },
                                   { po::CUTOFF, 1500.0f, 7000.0f, 'A' }, { po::ATTACK, 0.2f, 1.0f, 'B' }, { po::RELEASE, 1.5f, 4.0f, 'R' },
                                   { po::CHORUS, 0.7f, 1.0f, 'R' }, { po::DETUNE, 4.0f, 10.0f, 'R' } }) },
            { "Oberheim Brass", 2, { "Fanfare", "Herald", "Clarion", "Bugle", "Signal", "Summons", "Tattoo", "Reveille" },
              base("Classic", "Classic", { { po::POSITION, 0.45f, 0.55f, 'R' }, { po::SCAN, 0.0f, 0.15f, 'R' }, { po::CUTOFF, 400.0f, 2500.0f, 'A' },
                                           { po::RESONANCE, 0.1f, 0.3f, 'R' }, { po::ENV_AMOUNT, 1.5f, 3.0f, 'B' }, { po::ATTACK, 0.15f, 0.8f, 'R' },
                                           { po::RELEASE, 1.0f, 3.0f, 'R' }, { po::CHORUS, 0.2f, 0.5f, 'R' }, { po::DETUNE, 5.0f, 12.0f, 'R' } }) },
            { "Sync Sweep", 3, { "Comet", "Meteor", "Bolide", "Streak", "Tracer", "Flare", "Arc", "Trail" },
              base("Sync", "Sync", { { po::POSITION, 0.0f, 0.4f, 'B' }, { po::SCAN, 0.3f, 0.8f, 'R' }, { po::CUTOFF, 1500.0f, 8000.0f, 'A' },
                                     { po::RESONANCE, 0.1f, 0.3f, 'R' }, { po::DETUNE, 3.0f, 8.0f, 'R' } }) },
            { "Formant Pad", 0, { "Throat", "Larynx", "Palate", "Vowel", "Chant", "Mantra", "Intonation", "Cantor" },
              base("Formant", "Formant", { { po::POSITION, 0.1f, 0.7f, 'B' }, { po::SCAN, 0.4f, 0.9f, 'R' }, { po::CUTOFF, 1500.0f, 6000.0f, 'A' },
                                           { po::RESONANCE, 0.05f, 0.2f, 'R' } }) },
            { "Vocal Pad", 1, { "Chorale", "Anthem", "Canticle", "Motet", "Hymn", "Madrigal", "Oratorio", "Cantata" },
              base("Alto", "Basso", { { po::POSITION, 0.2f, 0.8f, 'B' }, { po::CUTOFF, 1200.0f, 5000.0f, 'A' }, { po::ATTACK, 1.0f, 3.0f, 'R' },
                                      { po::CHORUS, 0.4f, 0.7f, 'R' } }) },
            { "Glass Pad", 2, { "Prism", "Lens", "Crystal", "Quartz", "Facet", "Mirror", "Pane", "Icicle" },
              base("Glass", "Glass", { { po::POSITION, 0.3f, 1.0f, 'B' }, { po::CUTOFF, 3000.0f, 10000.0f, 'A' }, { po::ATTACK, 0.5f, 2.0f, 'R' },
                                       { po::RELEASE, 3.0f, 7.0f, 'R' } }) },
            { "Single Cycle Pad", 3, { "Relic", "Cameo", "Locket", "Token", "Charm", "Amulet", "Talisman", "Keepsake" },
              base("FM Synth", "Theremin", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 1000.0f, 5000.0f, 'A' }, { po::RESONANCE, 0.05f, 0.2f, 'R' } }) },
            { "Bowed Pad", 0, { "Bow", "Rosin", "Gut", "Bridge", "Scroll", "Fingerboard", "Soundpost", "Purfling" },
              base("Bowed 1", "Bowed 3", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 800.0f, 4000.0f, 'A' }, { po::ATTACK, 1.0f, 3.0f, 'R' },
                                           { po::RELEASE, 3.0f, 7.0f, 'R' } }) },
            { "Tube Pad", 1, { "Valve", "Filament", "Cathode", "Anode", "Glow", "Triode", "Pentode", "Heater" },
              base("Tube 1", "Tube 3", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 600.0f, 3500.0f, 'A' }, { po::RESONANCE, 0.05f, 0.25f, 'R' } }) },
            { "PPG Choir", 2, { "Wave", "Table", "Palette", "Spectrum", "Index", "Slot", "Bank", "Matrix" },
              base("PPG 00", "PPG 25", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 1500.0f, 7000.0f, 'A' }, { po::RESONANCE, 0.1f, 0.35f, 'R' },
                                         { po::CHORUS, 0.2f, 0.5f, 'R' }, { po::ATTACK, 0.5f, 2.0f, 'R' }, { po::RELEASE, 2.0f, 5.0f, 'R' } }) },
            { "PPG Upper", 3, { "Cirrus", "Stratus", "Nimbus", "Cumulus", "Altostratus", "Contrail", "Halo", "Corona" },
              base("PPG 22", "PPG Upper", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 3000.0f, 10000.0f, 'A' }, { po::CHORUS, 0.3f, 0.6f, 'R' } }) },
            { "Overtone Pad", 0, { "Partial", "Harmonic", "Series", "Octave", "Twelfth", "Fifteenth", "Seventeenth", "Nineteenth" },
              base("Overtones 1", "Overtones 3", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 1500.0f, 8000.0f, 'A' } }) },
            { "Morph Pad", 1, { "Chimera", "Hybrid", "Mutation", "Metamorph", "Shapeshift", "Transit", "Alloy", "Amalgam" },
              base("Consonant 1", "Morph 3", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::SCAN, 0.4f, 0.9f, 'R' }, { po::CUTOFF, 1000.0f, 6000.0f, 'A' } }) },
            { "Sampled Air", 2, { "Breeze", "Draught", "Gust", "Zephyr", "Current", "Updraft", "Thermal", "Jetstream" },
              base("Sampled 1", "Sampled 3", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 1000.0f, 6000.0f, 'A' }, { po::ATTACK, 1.5f, 4.0f, 'R' } }) },
            { "Dark Drone Pad", 3, { "Undercroft", "Catacomb", "Ossuary", "Vault", "Barrow", "Tomb", "Sepulchre", "Mausoleum" },
              base("Drone Bank", "Vox Synth", { { po::POSITION, 0.0f, 1.0f, 'B' }, { po::CUTOFF, 300.0f, 1500.0f, 'A' }, { po::ATTACK, 2.0f, 6.0f, 'R' },
                                               { po::RELEASE, 4.0f, 9.0f, 'R' }, { po::RESONANCE, 0.1f, 0.3f, 'R' } }) },
        } };
    return s;
}

/**
 * @brief The names and groups of module @p m, null for a module without presets.
 * @param[in] m  the module
 * @return its names and groups, null for a module without presets
 */
const Synth* synthOf(ModuleE m)
{
    switch (m) {
    case ModuleE::VOICE: return &voiceSynth();
    case ModuleE::LEAD: return &leadSynth();
    case ModuleE::DRONE: return &droneSynth();
    case ModuleE::TAPE: return &tapeSynth();
    case ModuleE::STRINGS: return &stringsSynth();
    case ModuleE::DRUMS: return &drumsSynth();
    case ModuleE::ATMOS: return &atmosSynth();
    case ModuleE::POLY: return &polySynth();
    default: return nullptr;
    }
}

// --- The filters of the groups (26.09.2026, Filters.h) -------------------------------------------------------------
/** @brief A filter a group's presets may take: the model, the range of its mode, and how much filter FM. */
struct FilterChoice {
    int m_model;      ///< the filter model (Filters.h)
    float m_modeLo;   ///< the low end of its mode's range
    float m_modeHi;   ///< ... the high end
    float m_fm;       ///< how much filter FM
};

/**
 * @brief The filters a group draws from, by the instruments its sounds come from: the basses on the Moog ladder (now
 *        and then the diode ladder), the squelch on the diode ladder and the Korg35, glass on the Xpander's high and
 *        band passes, the Juno's strings on the IR3109, the Oberheim's brass on the SEM, the dark and dusty ones on the
 *        Polivoks and the Wasp, the cosmic drips on the comb and the phaser; empty for a synth without a filter model.
 * @param[in] m      the module
 * @param[in] group  the group
 * @return the filters the group draws from
 */
const std::vector<FilterChoice>& filterChoices(ModuleE m, const std::string& group)
{
    enum { MOOG, PROPHET, JUNO, SEM, XPANDER, DIODE, KORG, POLIVOKS, WASP, COMB };
    const float BP2 = 2.0f / 7.0f, BP4 = 3.0f / 7.0f, HP2 = 4.0f / 7.0f, NOTCH = 6.0f / 7.0f, PHASER = 1.0f;
    using LT = std::vector<FilterChoice>;
    static const std::vector<std::pair<std::string, LT>> voice = {
        { "Ladder Bass", { { MOOG, 0, 0, 0 }, { MOOG, 0, 0, 0 }, { MOOG, 0, 0, 0 }, { DIODE, 0, 0, 0 } } },
        { "Deep Ostinato", { { MOOG, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Pluck Sequence", { { PROPHET, 0, 0, 0 }, { SEM, 0, 0, 0 }, { MOOG, 0, 0, 0 }, { JUNO, 0, 0, 0 } } },
        { "Resonant Sweep", { { MOOG, 0, 0, 0 }, { DIODE, 0, 0, 0 }, { KORG, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Hollow Pulse", { { SEM, 0.3f, 0.5f, 0 }, { XPANDER, BP2, BP2, 0 }, { JUNO, 0, 0, 0 } } },
        { "Glass Arp", { { XPANDER, HP2, HP2, 0.1f }, { XPANDER, BP4, BP4, 0 }, { SEM, 0.7f, 0.9f, 0 }, { COMB, 0, 0, 0 } } },
        { "Tape Sequence", { { JUNO, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Soft Pad Voice", { { JUNO, 0, 0, 0 }, { SEM, 0, 0, 0 } } },
        { "Squelch Arp", { { DIODE, 0, 0, 0 }, { DIODE, 0, 0, 0 }, { KORG, 0, 0, 0 }, { MOOG, 0, 0, 0 } } },
        { "Staccato Pulse", { { PROPHET, 0, 0, 0 }, { JUNO, 0, 0, 0 } } },
        { "Legato Glide", { { MOOG, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Warm Unison", { { PROPHET, 0, 0, 0 }, { SEM, 0, 0, 0 }, { MOOG, 0, 0, 0 } } },
        { "Bright Stab", { { JUNO, 0, 0, 0 }, { XPANDER, HP2, HP2, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Dark Throb", { { POLIVOKS, 0, 0.3f, 0 }, { MOOG, 0, 0, 0 }, { WASP, 0, 0.2f, 0 } } },
        { "Accent Ratchet", { { DIODE, 0, 0, 0 }, { KORG, 0, 0, 0 }, { MOOG, 0, 0, 0 } } },
        { "Cosmic Drip", { { COMB, 0, 0.4f, 0 }, { COMB, 0.6f, 1.0f, 0 }, { XPANDER, PHASER, PHASER, 0.2f }, { WASP, 0.4f, 0.6f, 0.15f } } },
    };
    static const std::vector<std::pair<std::string, LT>> lead = {
        { "Solo Saw", { { MOOG, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Singing Pulse", { { SEM, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Flute Lead", { { SEM, 0, 0, 0 }, { JUNO, 0, 0, 0 } } },
        { "Portamento", { { MOOG, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Glass Whistle", { { XPANDER, BP2, BP2, 0 }, { SEM, 0.35f, 0.45f, 0 } } },
        { "Theremin", { { SEM, 0, 0, 0 }, { MOOG, 0, 0, 0 } } },
        { "Screaming Filter", { { KORG, 0, 0, 0 }, { KORG, 0, 0, 0 }, { DIODE, 0, 0, 0 } } },
        { "Soft Horn", { { PROPHET, 0, 0, 0 }, { SEM, 0, 0, 0 } } },
        { "Ethereal Sine", { { SEM, 0, 0, 0 }, { JUNO, 0, 0, 0 } } },
        { "Brass Lead", { { PROPHET, 0, 0, 0 }, { SEM, 0, 0, 0 } } },
        { "Hollow Oboe", { { XPANDER, BP4, BP4, 0 }, { SEM, 0.42f, 0.5f, 0 } } },
        { "Twin Oscillator", { { JUNO, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Cosmic Siren", { { KORG, 0, 0, 0.2f }, { COMB, 0, 0.4f, 0 }, { XPANDER, PHASER, PHASER, 0.3f } } },
        { "Warm Mono", { { MOOG, 0, 0, 0 } } },
        { "Bell Lead", { { XPANDER, HP2, HP2, 0.25f }, { SEM, 0.7f, 0.8f, 0.1f } } },
        { "Dusty Solo", { { POLIVOKS, 0, 0.2f, 0 }, { WASP, 0, 0.2f, 0 }, { MOOG, 0, 0, 0 } } },
    };
    static const std::vector<std::pair<std::string, LT>> drone = {
        { "Dark Bordun", { { MOOG, 0, 0, 0 } } },
        { "Organ Pedal", { { SEM, 0, 0, 0 } } },
        { "Pulse Hum", { { PROPHET, 0, 0, 0 } } },
        { "Resonant Earth", { { DIODE, 0, 0, 0 }, { MOOG, 0, 0, 0 } } },
        { "Glowing Root", { { PROPHET, 0, 0, 0 } } },
        { "Hollow Pipe", { { XPANDER, BP2, BP2, 0 } } },
        { "Warm Floor", { { SEM, 0, 0, 0 } } },
        { "Filtered Void", { { COMB, 0, 0.3f, 0 }, { MOOG, 0, 0, 0 } } },
        { "Singing Ground", { { SEM, 0, 0.1f, 0 }, { XPANDER, BP2, BP2, 0 } } },
        { "Iron Drone", { { POLIVOKS, 0, 0.2f, 0 }, { WASP, 0, 0.2f, 0 } } },
        { "Soft Monolith", { { JUNO, 0, 0, 0 } } },
        { "Bright Axis", { { SEM, 0.6f, 0.75f, 0 } } },
        { "Deep Current", { { MOOG, 0, 0, 0 } } },
        { "Breathing Root", { { PROPHET, 0, 0, 0 } } },
        { "Temple Hum", { { SEM, 0.42f, 0.5f, 0 }, { XPANDER, NOTCH, NOTCH, 0 } } },
        { "Night Floor", { { MOOG, 0, 0, 0 }, { DIODE, 0, 0, 0 } } },
    };
    static const std::vector<std::pair<std::string, LT>> poly = {
        { "Analog Pad", { { PROPHET, 0, 0, 0 }, { SEM, 0, 0, 0 } } },
        { "Juno Strings", { { JUNO, 0, 0, 0 } } },
        { "Oberheim Brass", { { SEM, 0, 0, 0 } } },
        { "Sync Sweep", { { PROPHET, 0, 0, 0 }, { MOOG, 0, 0, 0 } } },
        { "Formant Pad", { { XPANDER, BP2, BP2, 0 }, { SEM, 0.35f, 0.45f, 0 } } },
        { "Vocal Pad", { { SEM, 0, 0, 0 }, { XPANDER, BP2, BP2, 0 } } },
        { "Glass Pad", { { XPANDER, HP2, HP2, 0 }, { SEM, 0.7f, 0.8f, 0 } } },
        { "Single Cycle Pad", { { JUNO, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Bowed Pad", { { SEM, 0, 0, 0 }, { PROPHET, 0, 0, 0 } } },
        { "Tube Pad", { { PROPHET, 0, 0, 0 }, { MOOG, 0, 0, 0 } } },
        { "PPG Choir", { { PROPHET, 0, 0, 0 } } },
        { "PPG Upper", { { PROPHET, 0, 0, 0 }, { XPANDER, BP2, BP2, 0 } } },
        { "Overtone Pad", { { SEM, 0, 0, 0 } } },
        { "Morph Pad", { { COMB, 0, 0.3f, 0 }, { XPANDER, PHASER, PHASER, 0 } } },
        { "Sampled Air", { { SEM, 0, 0, 0 }, { JUNO, 0, 0, 0 } } },
        { "Dark Drone Pad", { { POLIVOKS, 0, 0.2f, 0 }, { MOOG, 0, 0, 0 }, { WASP, 0, 0.2f, 0 } } },
    };
    static const LT none;
    const std::vector<std::pair<std::string, LT>>* table = nullptr;
    switch (m) {
    case ModuleE::VOICE: table = &voice; break;
    case ModuleE::LEAD: table = &lead; break;
    case ModuleE::DRONE: table = &drone; break;
    case ModuleE::POLY: table = &poly; break;
    default: return none;
    }
    for (const auto& e : *table) if (e.first == group) return e.second;
    return none;
}

// --- The modulation of the groups (26.09.2026, Modulation.h) ---------------------------------------------------------
/** @brief A knob a modulation sets, by key, drawn from its range. In an atom of modulation "lfo@_..." is the LFO the
 *         atom is given, "mod#_..." its slot of the matrix, and a source of -1 that LFO. */
struct KnobRange {
    const char* m_key;   ///< the knob's key ("lfo@_rate", "mod#_amount" ...)
    float m_lo;          ///< the low end of its range
    float m_hi;          ///< ... the high end
};
/** @brief A modulation a group's presets may carry: how often, and its knobs. */
struct ModRecipe {
    float m_chance;                  ///< how often a preset of the group carries it, 0..1
    std::vector<KnobRange> m_knobs;  ///< the knobs it sets
};
/** @brief One modulation of a synth's pool; @p pitch: it moves the pitch (never on the foundation, the bass and the drone). */
struct ModAtom {
    bool m_pitch;                    ///< it moves the pitch
    std::vector<KnobRange> m_knobs;  ///< the knobs it sets
};

/**
 * @brief The modulation that makes a group what it is -- after the instruments and players its sounds come from: plucks
 *        and stabs that decay under a held gate, the pulse width moving on the hollow pulses and the humming drones,
 *        the sequencer's filter sweep synced to the bars, sample-and-hold drips, tape wobble, a vibrato that comes in
 *        late, the PPG's and the morphing pads' tables walked by a slow LFO, the brass's filter swell. Sources and
 *        targets by number (ModSourceNames, ModDestNames; the tape keys and the strings: ShortModDestNames).
 * @param[in] m      the module
 * @param[in] group  the group
 * @return the group's modulation recipes
 */
const std::vector<ModRecipe>& modRecipes(ModuleE m, const std::string& group)
{
    using RT = std::vector<ModRecipe>;
    // Sources: the atom's LFO -1, the mod envelope 5, the filter envelope 6, velocity 7, the modulation lane 8. Targets:
    // pitch 1, pulse width 2, blend 3, table 4, cutoff 5, resonance 6, filter mode 7, filter FM 8, level 9, pan 10 (the
    // smaller matrix: pitch 1, tone 2, level 3, pan 4).
    static const std::vector<std::pair<std::string, RT>> voice = {
        { "Ladder Bass", { { 0.5f, { { "amp_sustain", 0.5f, 0.8f }, { "amp_decay2", 250.0f, 700.0f } } } } },
        { "Deep Ostinato", { { 0.5f, { { "mod_attack", 0.5f, 3.0f }, { "mod_decay", 80.0f, 300.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.08f, 0.2f } } } } },
        { "Pluck Sequence", { { 0.9f, { { "amp_sustain", 0.0f, 0.25f }, { "amp_decay2", 120.0f, 450.0f } } },
                              { 0.9f, { { "amp_sustain", 0.0f, 0.2f }, { "amp_decay2", 150.0f, 400.0f }, { "env_velocity", 0.3f, 0.6f } } } } },
        { "Resonant Sweep", { { 0.85f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 1, 2 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.08f, 0.2f } } } } },
        { "Hollow Pulse", { { 0.9f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.3f, 1.5f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.2f, 0.45f } } } } },
        { "Glass Arp", { { 0.7f, { { "amp_sustain", 0.1f, 0.4f }, { "amp_decay2", 200.0f, 600.0f } } },
                         { 0.7f, { { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.12f } } } } },
        { "Tape Sequence", { { 0.85f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.4f, 1.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.06f, 0.09f } } } } },
        { "Soft Pad Voice", { { 0.85f, { { "amp_attack", 60.0f, 300.0f }, { "lfo@_rate", 0.1f, 0.4f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.04f, 0.1f } } } } },
        { "Squelch Arp", { { 0.8f, { { "env_velocity", 0.3f, 0.7f } } } } },
        { "Staccato Pulse", { { 0.9f, { { "amp_sustain", 0.0f, 0.2f }, { "amp_decay2", 60.0f, 200.0f } } } } },
        { "Legato Glide", { { 0.75f, { { "lfo@_rate", 4.8f, 5.8f }, { "lfo@_fade", 0.5f, 1.5f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.08f, 0.12f } } } } },
        { "Warm Unison", { { 0.6f, { { "lfo@_rate", 0.1f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 6, 6 }, { "mod#_amt", 0.06f, 0.15f } } } } },
        { "Bright Stab", { { 0.8f, { { "amp_sustain", 0.3f, 0.6f }, { "amp_decay2", 200.0f, 500.0f } } } } },
        { "Dark Throb", { { 0.7f, { { "lfo@_sync", 5, 6 }, { "mod#_src", -1, -1 }, { "mod#_dst", 9, 9 }, { "mod#_amt", -0.45f, -0.25f } } },
                          { 0.7f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 3, 4 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Accent Ratchet", { { 0.7f, { { "amp_sustain", 0.2f, 0.5f }, { "amp_decay2", 80.0f, 250.0f } } } } },
        { "Cosmic Drip", { { 0.8f, { { "lfo@_shape", 5, 5 }, { "lfo@_sync", 7, 7 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } },
                           { 0.8f, { { "mod_attack", 0.5f, 2.0f }, { "mod_decay", 30.0f, 90.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.25f, 0.4f } } } } },
    };
    static const std::vector<std::pair<std::string, RT>> lead = {
        { "Solo Saw", { { 0.6f, { { "mod_attack", 0.5f, 2.0f }, { "mod_decay", 150.0f, 500.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.08f, 0.2f } } } } },
        { "Singing Pulse", { { 0.85f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.2f, 0.8f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.25f } } } } },
        { "Flute Lead", { { 0.8f, { { "amp_attack", 20.0f, 60.0f } } } } },
        { "Portamento", { { 0.5f, { { "lfo@_rate", 0.1f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.1f } } } } },
        { "Glass Whistle", { { 0.6f, { { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.1f, 0.25f } } } } },
        { "Screaming Filter", { { 0.8f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.3f, 1.0f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.08f, 0.18f } } } } },
        { "Soft Horn", { { 0.8f, { { "amp_attack", 20.0f, 80.0f }, { "filt_attack", 30.0f, 120.0f } } } } },
        { "Brass Lead", { { 0.8f, { { "mod_attack", 0.5f, 2.0f }, { "mod_decay", 40.0f, 100.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 1, 1 }, { "mod#_amt", -0.15f, -0.1f } } } } },
        { "Twin Oscillator", { { 0.8f, { { "lfo@_rate", 0.1f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.2f, 0.35f } } } } },
        { "Cosmic Siren", { { 0.8f, { { "lfo@_rate", 0.1f, 0.4f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.25f, 0.4f } } } } },
        { "Bell Lead", { { 0.85f, { { "amp_sustain", 0.2f, 0.5f }, { "amp_decay2", 400.0f, 1200.0f } } } } },
        { "Dusty Solo", { { 0.8f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.3f, 1.0f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.07f, 0.1f } } } } },
    };
    static const std::vector<std::pair<std::string, RT>> drone = {
        { "Pulse Hum", { { 0.9f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.15f, 0.3f } } } } },
        { "Resonant Earth", { { 0.8f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.02f, 0.08f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Iron Drone", { { 0.7f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.1f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.1f } } } } },
        { "Deep Current", { { 0.8f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Breathing Root", { { 0.9f, { { "lfo@_rate", 0.05f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Temple Hum", { { 0.8f, { { "lfo@_rate", 0.03f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Organ Pedal", { { 0.6f, { { "lfo@_rate", 0.03f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Hollow Pipe", { { 0.7f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.04f, 0.12f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.25f } } } } },
    };
    static const std::vector<std::pair<std::string, RT>> poly = {
        { "Analog Pad", { { 0.7f, { { "lfo@_rate", 0.05f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.12f } } },
                          { 0.7f, { { "lfo@_rate", 0.2f, 0.5f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.07f } } } } },
        { "Juno Strings", { { 0.6f, { { "lfo@_rate", 4.5f, 5.5f }, { "lfo@_fade", 0.8f, 2.0f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } } } },
        { "Oberheim Brass", { { 0.85f, { { "filt_link", 0, 0 }, { "filt_attack", 0.05f, 0.3f }, { "filt_decay", 0.5f, 1.5f }, { "filt_sustain", 0.3f, 0.5f }, { "filt_release", 0.5f, 2.0f } } } } },
        { "Sync Sweep", { { 0.85f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 2, 3 }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.3f, 0.5f } } } } },
        { "Formant Pad", { { 0.8f, { { "lfo@_rate", 0.05f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.4f } } } } },
        { "Vocal Pad", { { 0.8f, { { "lfo@_rate", 0.05f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.4f } } } } },
        { "Glass Pad", { { 0.8f, { { "lfo@_rate", 0.1f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 10, 10 }, { "mod#_amt", 0.3f, 0.6f } } } } },
        { "Bowed Pad", { { 0.7f, { { "lfo@_rate", 4.5f, 5.5f }, { "lfo@_fade", 1.0f, 2.0f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.06f, 0.09f } } } } },
        { "PPG Choir", { { 0.85f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.05f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.5f } } } } },
        { "PPG Upper", { { 0.85f, { { "filt_link", 0, 0 }, { "filt_attack", 1.0f, 3.0f }, { "filt_decay", 2.0f, 5.0f }, { "filt_sustain", 0.3f, 0.6f }, { "filt_release", 2.0f, 5.0f } } },
                         { 0.85f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.05f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.5f } } } } },
        { "Morph Pad", { { 0.85f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.4f } } } } },
        { "Dark Drone Pad", { { 0.7f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Sampled Air", { { 0.7f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.03f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.4f } } } } },
    };
    static const std::vector<std::pair<std::string, RT>> tape = {
        { "Cathedral Choir", { { 0.6f, { { "amp_attack", 200.0f, 800.0f } } } } },
        { "Ghost Choir", { { 0.8f, { { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Silk Strings", { { 0.6f, { { "amp_attack", 100.0f, 400.0f } } } } },
        { "Warped Strings", { { 0.8f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.5f, 1.5f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } } } },
        { "Breathy Flute", { { 0.8f, { { "lfo@_rate", 4.0f, 5.5f }, { "lfo@_fade", 0.3f, 0.8f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Wobbly Flute", { { 0.8f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.5f, 1.5f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } } } },
    };
    static const std::vector<std::pair<std::string, RT>> strings = {
        { "Violin Section", { { 0.8f, { { "lfo@_rate", 5.0f, 6.0f }, { "lfo@_fade", 0.5f, 1.0f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } } } },
        { "Brass Machine", { { 0.8f, { { "amp_sustain", 0.6f, 0.8f }, { "amp_decay", 0.5f, 1.5f } } } } },
        { "Phased Strings", { { 0.8f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 2, 2 }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Short Bow", { { 0.85f, { { "amp_sustain", 0.3f, 0.6f }, { "amp_decay", 0.3f, 1.0f } } } } },
        { "Slow Swell", { { 0.6f, { { "lfo@_rate", 0.03f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.08f, 0.15f } } } } },
    };
    static const RT none;
    const std::vector<std::pair<std::string, RT>>* table = nullptr;
    switch (m) {
    case ModuleE::VOICE: table = &voice; break;
    case ModuleE::LEAD: table = &lead; break;
    case ModuleE::DRONE: table = &drone; break;
    case ModuleE::POLY: table = &poly; break;
    case ModuleE::TAPE: table = &tape; break;
    case ModuleE::STRINGS: table = &strings; break;
    default: return none;
    }
    for (const auto& e : *table) if (e.first == group) return e.second;
    return none;
}

/**
 * @brief The pool of modulations a synth's presets draw from beside their group's own, a colour each: the cutoff
 *        drifting or pulsing with the bars, sample and hold, the velocity and the row's modulation lane on filter and
 *        timbre, a pluck of the mod envelope, pulse width modulation, a wobble or a late vibrato, the pan wandering, the
 *        SEM's morph and the resonance breathing, a gate of the level in time, a zap of filter FM; on the pad synth the
 *        table walking and every key its own place and pitch; on the tape keys and the strings wobble, tone, tremolo,
 *        pan.
 * @param[in] m  the module
 * @return the pool of modulations its presets draw from
 */
const std::vector<ModAtom>& modPool(ModuleE m)
{
    using AT = std::vector<ModAtom>;
    static const AT voices = {
        { false, { { "lfo@_shape", 0, 1 }, { "lfo@_rate", 0.03f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.04f, 0.12f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 3, 5 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.15f } } },
        { false, { { "lfo@_shape", 5, 5 }, { "lfo@_sync", 6, 7 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.15f } } },
        { false, { { "mod#_src", 7, 7 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.25f } } },
        { false, { { "mod#_src", 8, 8 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.2f, 0.5f } } },
        { false, { { "mod#_src", 8, 8 }, { "mod#_dst", 6, 6 }, { "mod#_amt", 0.1f, 0.3f } } },
        { false, { { "mod_attack", 0.5f, 2.0f }, { "mod_decay", 40.0f, 250.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.3f } } },
        { false, { { "wave", 0.4f, 0.8f }, { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.2f, 2.0f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.15f, 0.35f } } },
        { true, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.3f, 1.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } },
        { true, { { "lfo@_rate", 4.5f, 6.0f }, { "lfo@_fade", 0.4f, 1.5f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.07f, 0.11f } } },
        { false, { { "lfo@_rate", 0.05f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 10, 10 }, { "mod#_amt", 0.2f, 0.5f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.05f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.15f, 0.35f } } },
        { false, { { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 6, 6 }, { "mod#_amt", 0.08f, 0.2f } } },
        { true, { { "mod_attack", 0.5f, 1.0f }, { "mod_decay", 15.0f, 60.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.2f, 0.3f } } },
        { false, { { "lfo@_shape", 4, 4 }, { "lfo@_sync", 5, 6 }, { "mod#_src", -1, -1 }, { "mod#_dst", 9, 9 }, { "mod#_amt", -0.3f, -0.15f } } },
        { false, { { "mod#_src", 6, 6 }, { "mod#_dst", 8, 8 }, { "mod#_amt", 0.1f, 0.3f } } },
    };
    static const AT lead = {
        { false, { { "lfo@_shape", 0, 1 }, { "lfo@_rate", 0.03f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.04f, 0.12f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.5f, 2.0f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.06f, 0.14f } } },
        { false, { { "mod#_src", 7, 7 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.25f } } },
        { false, { { "mod_attack", 0.5f, 3.0f }, { "mod_decay", 80.0f, 400.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.08f, 0.2f } } },
        { false, { { "wave", 0.5f, 0.9f }, { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.2f, 1.5f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.3f } } },
        { true, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.3f, 1.0f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } },
        { true, { { "mod_attack", 0.5f, 1.0f }, { "mod_decay", 20.0f, 80.0f }, { "mod#_src", 5, 5 }, { "mod#_dst", 1, 1 }, { "mod#_amt", -0.2f, -0.1f } } },
        { false, { { "lfo@_rate", 0.05f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 10, 10 }, { "mod#_amt", 0.15f, 0.35f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.05f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.15f, 0.3f } } },
        { false, { { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 6, 6 }, { "mod#_amt", 0.06f, 0.15f } } },
        { false, { { "mod#_src", 6, 6 }, { "mod#_dst", 8, 8 }, { "mod#_amt", 0.1f, 0.25f } } },
    };
    static const AT drone = {
        { false, { { "lfo@_shape", 0, 1 }, { "lfo@_rate", 0.02f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.15f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 1, 3 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.12f } } },
        { false, { { "wave", 0.4f, 0.8f }, { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.03f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.3f } } },
        { false, { { "lfo@_rate", 0.02f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 10, 10 }, { "mod#_amt", 0.1f, 0.3f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.02f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.1f, 0.25f } } },
        { false, { { "lfo@_rate", 0.03f, 0.12f }, { "mod#_src", -1, -1 }, { "mod#_dst", 6, 6 }, { "mod#_amt", 0.06f, 0.15f } } },
        { false, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.02f, 0.08f }, { "mod#_src", -1, -1 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.15f, 0.35f } } },
    };
    static const AT pads = {
        { false, { { "lfo@_shape", 0, 1 }, { "lfo@_rate", 0.03f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.12f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.03f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.15f, 0.35f } } },
        { true, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.2f, 0.8f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.04f, 0.06f } } },
        { false, { { "lfo@_rate", 0.05f, 0.25f }, { "mod#_src", -1, -1 }, { "mod#_dst", 10, 10 }, { "mod#_amt", 0.3f, 0.6f } } },
        { false, { { "lfo@_rate", 0.04f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 6, 6 }, { "mod#_amt", 0.06f, 0.15f } } },
        { false, { { "mod_attack", 0.5f, 2.0f }, { "mod_decay", 1.0f, 4.0f }, { "mod_sustain", 0.2f, 0.5f }, { "mod#_src", 5, 5 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.25f } } },
        { false, { { "mod#_src", 7, 7 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } },
        { false, { { "lfo@_rate", 3.0f, 5.0f }, { "lfo@_fade", 1.0f, 2.0f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 9, 9 }, { "mod#_amt", -0.2f, -0.1f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.03f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.1f, 0.3f } } },
        { true, { { "lfo@_rate", 4.5f, 5.5f }, { "lfo@_fade", 1.0f, 2.5f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } },
    };
    static const AT small = {   // the tape keys and the strings: pitch 1, tone 2, level 3, pan 4
        { true, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.3f, 1.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.04f, 0.07f } } },
        { false, { { "lfo@_rate", 0.03f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.05f, 0.12f } } },
        { false, { { "lfo@_rate", 4.0f, 6.0f }, { "lfo@_fade", 0.5f, 1.5f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.06f, 0.12f } } },
        { false, { { "lfo@_rate", 0.03f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.5f } } },
    };
    static const AT none;
    switch (m) {
    case ModuleE::VOICE: return voices;
    case ModuleE::LEAD: return lead;
    case ModuleE::DRONE: return drone;
    case ModuleE::POLY: return pads;
    case ModuleE::TAPE: case ModuleE::STRINGS: return small;
    default: return none;
    }
}

/**
 * @brief The groups whose pitch is the foundation (the bass groups the composer draws the bass from; the drone throughout):
 *         no pitch modulation.
 * @param[in] m      the module
 * @param[in] group  the group
 * @return true when the group's pitch is the foundation
 */
bool foundation(ModuleE m, const std::string& group)
{
    return m == ModuleE::DRONE || (m == ModuleE::VOICE && (group == "Ladder Bass" || group == "Deep Ostinato" || group == "Dark Throb" || group == "Warm Unison"));
}

// --- The oscillators of the groups (26.09.2026, Vco.h) ---------------------------------------------------------------
/** @brief A VCO a group's presets may take: the model, hard sync (VCO 2's interval and the filter envelope's sweep of
 *         it, the Prophet's Poly-Mod), and cross mod. */
struct VcoChoice {
    int m_model;      ///< the VCO model (Vco.h)
    bool m_sync;      ///< hard sync
    float m_osc2Lo;   ///< VCO 2's interval: the low end of its range, semitones
    float m_osc2Hi;   ///< ... the high end
    float m_sweep;    ///< the filter envelope's sweep of it (the Prophet's Poly-Mod)
    float m_cross;    ///< cross mod
};

/**
 * @brief The VCOs a group draws from, by the instruments its sounds come from: the basses on the Moog (the sub basses
 *        on the steady E-mu), the sync sweeps on the Prophet, the round pulses and pads on the SEM, the plucks and the
 *        squelch on the ARP, glass and bells on the E-mu (with cross mod now and then) -- and still the voices' own
 *        analog oscillators here and there.
 * @param[in] m      the module
 * @param[in] group  the group
 * @return the VCOs the group draws from
 */
const std::vector<VcoChoice>& vcoChoices(ModuleE m, const std::string& group)
{
    enum { ANALOG, MOOG, PROPHET, SEM, ARP, EMU };
    using LT = std::vector<VcoChoice>;
    const VcoChoice moog { MOOG, false, 0, 0, 0, 0 }, prophet { PROPHET, false, 0, 0, 0, 0 }, sem { SEM, false, 0, 0, 0, 0 };
    const VcoChoice arp { ARP, false, 0, 0, 0, 0 }, emu { EMU, false, 0, 0, 0, 0 }, analog { ANALOG, false, 0, 0, 0, 0 };
    const VcoChoice sweep { PROPHET, true, 5.0f, 19.0f, 0.4f, 0 }, emuX { EMU, false, 0, 0, 0, 0.3f }, arpX { ARP, false, 0, 0, 0, 0.3f };
    static const std::vector<std::pair<std::string, LT>> voice = {
        { "Ladder Bass", { moog, moog, moog, analog, emu } },
        { "Deep Ostinato", { moog, emu, analog } },
        { "Pluck Sequence", { arp, prophet, analog } },
        { "Resonant Sweep", { moog, prophet, arp } },
        { "Hollow Pulse", { sem, arp } },
        { "Glass Arp", { emu, arp, prophet } },
        { "Tape Sequence", { sem, moog } },
        { "Soft Pad Voice", { sem, sem, analog } },
        { "Squelch Arp", { arp, moog } },
        { "Staccato Pulse", { prophet, emu } },
        { "Legato Glide", { moog } },
        { "Warm Unison", { sem, moog } },
        { "Bright Stab", { sweep, sweep, prophet } },
        { "Dark Throb", { moog, sem } },
        { "Accent Ratchet", { arp, analog } },
        { "Cosmic Drip", { emu, arpX, emuX } },
    };
    static const std::vector<std::pair<std::string, LT>> lead = {
        { "Solo Saw", { moog, moog, prophet } },
        { "Singing Pulse", { arp, prophet } },
        { "Flute Lead", { sem, emu } },
        { "Portamento", { moog } },
        { "Glass Whistle", { emu } },
        { "Theremin", { emu } },
        { "Screaming Filter", { sweep, sweep, arp } },
        { "Soft Horn", { sem, prophet } },
        { "Ethereal Sine", { emu } },
        { "Brass Lead", { prophet, sem } },
        { "Hollow Oboe", { arp, sem } },
        { "Twin Oscillator", { sem, moog } },
        { "Cosmic Siren", { arpX, sweep } },
        { "Warm Mono", { moog } },
        { "Bell Lead", { emuX, arpX } },
        { "Dusty Solo", { sem, moog } },
    };
    static const std::vector<std::pair<std::string, LT>> drone = {
        { "Dark Bordun", { moog } }, { "Organ Pedal", { emu } }, { "Pulse Hum", { sem, arp } }, { "Resonant Earth", { moog } },
        { "Glowing Root", { sem } }, { "Hollow Pipe", { arp } }, { "Warm Floor", { moog, sem } }, { "Filtered Void", { emu } },
        { "Singing Ground", { sem } }, { "Iron Drone", { arp, moog } }, { "Soft Monolith", { sem } }, { "Bright Axis", { prophet } },
        { "Deep Current", { emu } }, { "Breathing Root", { moog } }, { "Temple Hum", { arp } }, { "Night Floor", { moog } },
    };
    static const LT none;
    const std::vector<std::pair<std::string, LT>>* table = nullptr;
    switch (m) {
    case ModuleE::VOICE: table = &voice; break;
    case ModuleE::LEAD: table = &lead; break;
    case ModuleE::DRONE: table = &drone; break;
    default: return none;
    }
    for (const auto& e : *table) if (e.first == group) return e.second;
    return none;
}

/** @brief The pad synth's analog groups on the VCO models' waves (Wavetable.h): a table and the range of its place. */
struct PolyTable {
    const char* m_table;   ///< the table's name
    float m_posLo;         ///< the low end of its place's range
    float m_posHi;         ///< ... the high end
};
/**
 * @brief The tables and places the pad synth's analog group @p group draws from (empty for the others).
 * @param[in] group  the pad synth's group
 * @return its tables and places, empty for a group that is not analog
 */
const std::vector<PolyTable>& polyTables(const std::string& group)
{
    // The waves' frames: the ramp into the square over the first third, then the pulse narrowing.
    static const std::vector<std::pair<std::string, std::vector<PolyTable>>> t = {
        { "Analog Pad", { { "PWM", 0.05f, 0.5f }, { "SEM Waves", 0.35f, 0.8f }, { "921 Waves", 0.35f, 0.8f }, { "Prophet Waves", 0.35f, 0.8f } } },
        { "Oberheim Brass", { { "Classic", 0.45f, 0.55f }, { "SEM Waves", 0.0f, 0.12f }, { "SEM Waves", 0.0f, 0.12f } } },
        { "Juno Strings", { { "PWM", 0.2f, 0.6f }, { "Prophet Waves", 0.4f, 0.8f } } },
    };
    static const std::vector<PolyTable> none;
    for (const auto& e : t) if (e.first == group) return e.second;
    return none;
}

/**
 * @brief The sixty-four presets of every group of a synth, the knob values from the axes (Presets.h).
 * @param[in] m  the module
 * @param[in] s  its names and groups
 * @return the presets of every group
 */
std::vector<SoundPreset> build(ModuleE m, const Synth& s)
{
    ParamStore store;
    std::vector<SoundPreset> out;
    out.reserve(s.m_groups.size() * 64);
    for (size_t g = 0; g < s.m_groups.size(); ++g) {
        const Group& grp = s.m_groups[g];
        for (int v = 0; v < 64; ++v) {
            const int a = v / 8, n = v % 8;
            SoundPreset p;
            p.m_group = grp.m_name;
            p.m_name = std::string(s.m_adjectives[grp.m_adjectives][a]) + " " + grp.m_nouns[n];
            Rng rng;
            rng.seed(mixSeed(0x5E7u + 131u * static_cast<uint64_t>(m) + 17u * g, static_cast<uint64_t>(v)));
            for (const Axis& ax : grp.m_axes) {
                // Where on the knob's range: the adjective's row or the noun's column (with a little of its own), or a draw.
                float t = ax.m_axis == 'A' ? a / 7.0f : (ax.m_axis == 'B' ? n / 7.0f : rng.uniform());
                if (ax.m_axis != 'R') t = std::clamp(t + 0.12f * (rng.uniform() - 0.5f), 0.0f, 1.0f);
                const ParamDesc& d = store.desc(store.id(m, 0, ax.m_k));
                float value;
                if (d.m_curve == CurveE::LOG && ax.m_lo > 0.0f) value = ax.m_lo * std::pow(ax.m_hi / ax.m_lo, t);
                else value = ax.m_lo + t * (ax.m_hi - ax.m_lo);
                if (d.m_curve == CurveE::CHOICE || d.m_curve == CurveE::INT || d.m_curve == CurveE::TOGGLE) value = std::round(value);
                value = std::clamp(value, d.m_minValue, d.m_maxValue);
                p.m_values.push_back({ ax.m_k, value });
            }
            // The filter (26.09.2026): drawn after the axes, so every other knob keeps the value it had.
            const std::vector<FilterChoice>& filters = filterChoices(m, grp.m_name);
            if (!filters.empty()) {
                const FilterChoice& fc = filters[static_cast<size_t>(rng.below(static_cast<int>(filters.size())))];
                const float mode = fc.m_modeLo + (fc.m_modeHi - fc.m_modeLo) * rng.uniform();
                const bool voice = m == ModuleE::VOICE, isPoly = m == ModuleE::POLY;
                p.m_values.push_back({ voice ? voice::FILTER : (isPoly ? poly::FILTER : lead::FILTER), static_cast<float>(fc.m_model) });
                p.m_values.push_back({ voice ? voice::FILTER_MODE : (isPoly ? poly::FILTER_MODE : lead::FILTER_MODE), mode });
                if (!isPoly) p.m_values.push_back({ voice ? voice::FILTER_FM : lead::FILTER_FM, fc.m_fm });
                // The group's cutoffs were set for a 24 dB ladder: the 12 dB filters (SEM, Korg35, Polivoks, Wasp) are
                // brighter at the same cutoff and go down, the diode ladder (steeper, its peak at the cutoff) and the Juno
                // a little up -- so a group keeps its brightness whatever filter it drew.
                static const float CutoffBy[FilterModels] = { 1.0f, 1.0f, 1.05f, 0.72f, 1.0f, 1.1f, 0.8f, 0.8f, 0.8f, 1.0f };
                const int cutoff = voice ? voice::CUTOFF : (isPoly ? poly::CUTOFF : lead::CUTOFF);
                const ParamDesc& cd = store.desc(store.id(m, 0, cutoff));
                for (auto& e : p.m_values)
                    if (e.first == cutoff) e.second = std::clamp(e.second * CutoffBy[fc.m_model], cd.m_minValue, cd.m_maxValue);
            }
            // The modulation (26.09.2026): drawn after the filter, so the knobs above keep their values. The group's own
            // first, then one to three of the synth's pool; every atom gets the next free LFO and slot of the matrix (the
            // mod envelope one at most), and the foundation no pitch.
            {
                const int count = ParamStore::moduleCount(m);
                auto indexOf = [&](const std::string& key) {
                    for (int k = 0; k < count; ++k) if (key == store.desc(store.id(m, 0, k)).m_key) return k;
                    return -1;
                };
                const bool small = m == ModuleE::TAPE || m == ModuleE::STRINGS;
                const int maxLfo = small ? 2 : Lfos, maxSlot = small ? 4 : ModSlots;
                int lfo = 0, slot = 0;
                bool env = false;
                auto apply = [&](const std::vector<KnobRange>& knobs) {
                    bool needLfo = false, needSlot = false, needEnv = false;
                    for (const KnobRange& kr : knobs) {
                        const std::string key = kr.m_key;
                        needLfo = needLfo || key.find('@') != std::string::npos;
                        needSlot = needSlot || key.find('#') != std::string::npos;
                        needEnv = needEnv || key.rfind("mod_", 0) == 0;
                    }
                    if ((needLfo && lfo >= maxLfo) || (needSlot && slot >= maxSlot) || (needEnv && env)) return false;
                    const int L = needLfo ? ++lfo : 0, S = needSlot ? ++slot : 0;
                    env = env || needEnv;
                    for (const KnobRange& kr : knobs) {
                        std::string key = kr.m_key;
                        if (const size_t at = key.find('@'); at != std::string::npos) key.replace(at, 1, std::to_string(L));
                        if (const size_t at = key.find('#'); at != std::string::npos) key.replace(at, 1, std::to_string(S));
                        const int k = indexOf(key);
                        if (k < 0) continue;
                        const ParamDesc& d = store.desc(store.id(m, 0, k));
                        const float t = rng.uniform();
                        float value = d.m_curve == CurveE::LOG && kr.m_lo > 0.0f ? kr.m_lo * std::pow(kr.m_hi / kr.m_lo, t) : kr.m_lo + t * (kr.m_hi - kr.m_lo);
                        if (kr.m_lo == -1.0f && kr.m_hi == -1.0f) value = static_cast<float>(L);   // the atom's own LFO as the source
                        if (d.m_curve == CurveE::CHOICE || d.m_curve == CurveE::INT || d.m_curve == CurveE::TOGGLE) value = std::round(value);
                        p.m_values.push_back({ k, std::clamp(value, d.m_minValue, d.m_maxValue) });
                    }
                    return true;
                };
                const std::vector<ModRecipe>& recipes = modRecipes(m, grp.m_name);
                if (!recipes.empty()) {
                    const ModRecipe& r = recipes[static_cast<size_t>(rng.below(static_cast<int>(recipes.size())))];
                    if (rng.uniform() < r.m_chance) apply(r.m_knobs);
                }
                const std::vector<ModAtom>& pool = modPool(m);
                if (!pool.empty()) {
                    const bool steady = foundation(m, grp.m_name);
                    std::vector<bool> used(pool.size(), false);
                    static const float More[3] = { 0.85f, 0.55f, 0.25f };
                    for (float chance : More) {
                        if (rng.uniform() >= chance) break;
                        for (int tries = 0; tries < 6; ++tries) {
                            const size_t pick = static_cast<size_t>(rng.below(static_cast<int>(pool.size())));
                            if (used[pick] || (steady && pool[pick].m_pitch)) continue;
                            used[pick] = true;
                            apply(pool[pick].m_knobs);
                            break;
                        }
                    }
                }
                // The VCO (Vco.h), drawn after all of the above, which keeps its values. The sync sweep takes the next
                // free slot: the filter envelope on VCO 2's pitch.
                const std::vector<VcoChoice>& vcos = vcoChoices(m, grp.m_name);
                if (!vcos.empty()) {
                    const VcoChoice& vc = vcos[static_cast<size_t>(rng.below(static_cast<int>(vcos.size())))];
                    p.m_values.push_back({ indexOf("vco"), static_cast<float>(vc.m_model) });
                    if (vc.m_sync) {
                        p.m_values.push_back({ indexOf("sync"), 1.0f });
                        p.m_values.push_back({ indexOf("osc2_pitch"), std::round(vc.m_osc2Lo + (vc.m_osc2Hi - vc.m_osc2Lo) * rng.uniform()) });
                        const float amount = vc.m_sweep * (0.6f + 0.4f * rng.uniform());
                        if (slot < maxSlot) {
                            const std::string sweep = "mod" + std::to_string(++slot);
                            p.m_values.push_back({ indexOf(sweep + "_src"), 6.0f });    // the filter envelope
                            p.m_values.push_back({ indexOf(sweep + "_dst"), 11.0f });   // on VCO 2's pitch
                            p.m_values.push_back({ indexOf(sweep + "_amt"), amount });
                        }
                    }
                    if (vc.m_cross > 0.0f) p.m_values.push_back({ indexOf("cross_mod"), vc.m_cross * (0.5f + 0.5f * rng.uniform()) });
                }
            }
            // The pad synth's analog groups, on a VCO model's waves now and then (Wavetable.h).
            if (m == ModuleE::POLY) {
                const std::vector<PolyTable>& tables = polyTables(grp.m_name);
                if (!tables.empty()) {
                    const PolyTable& pt = tables[static_cast<size_t>(rng.below(static_cast<int>(tables.size())))];
                    const float pos = pt.m_posLo + (pt.m_posHi - pt.m_posLo) * rng.uniform();
                    for (auto& e : p.m_values) {
                        if (e.first == poly::TABLE) e.second = tableOf(pt.m_table);
                        if (e.first == poly::POSITION) e.second = pos;
                    }
                }
            }
            out.push_back(std::move(p));
        }
    }
    return out;
}

} // namespace

const std::vector<SoundPreset>& factoryPresets(ModuleE module)
{
    static std::mutex lock;
    static std::vector<SoundPreset> built[static_cast<int>(ModuleE::COUNT)];
    static bool done[static_cast<int>(ModuleE::COUNT)] = {};
    static const std::vector<SoundPreset> none;
    const Synth* s = synthOf(module);
    if (s == nullptr) return none;
    std::lock_guard<std::mutex> g(lock);
    const int i = static_cast<int>(module);
    if (!done[i]) { built[i] = build(module, *s); done[i] = true; }
    return built[i];
}

bool presetLeaves(ModuleE module, int k)
{
    switch (module) {
    case ModuleE::LEAD: case ModuleE::DRONE:
        return k == lead::LEVEL || k == lead::PAN || k == lead::ECHO_SEND || k == lead::REVERB_SEND || k == lead::AUTO_PAN
            || k == lead::LOW_CUT || k == lead::DISTANCE || k == lead::BLEND_SEND || k == lead::EARLY_SEND || k == lead::SHIMMER_SEND;
    case ModuleE::TAPE:
        return k == tape::LEVEL || k == tape::PAN || k == tape::ECHO_SEND || k == tape::REVERB_SEND || k == tape::LOW_CUT
            || k == tape::DISTANCE || k == tape::SPREAD || k == tape::BLEND_SEND || k == tape::EARLY_SEND || k == tape::SHIMMER_SEND;
    case ModuleE::STRINGS:
        return k == strings::LEVEL || k == strings::PAN || k == strings::ECHO_SEND || k == strings::REVERB_SEND
            || k == strings::LOW_CUT || k == strings::DISTANCE || k == strings::BLEND_SEND || k == strings::EARLY_SEND
            || k == strings::SHIMMER_SEND;
    case ModuleE::DRUMS:
        return k == drums::LEVEL || k == drums::ECHO_SEND || k == drums::REVERB_SEND || k == drums::LOW_CUT || k == drums::BLEND_SEND || k == drums::EARLY_SEND;
    case ModuleE::POLY:
        return k == poly::LEVEL || k == poly::PAN || k == poly::ECHO_SEND || k == poly::REVERB_SEND || k == poly::LOW_CUT
            || k == poly::DISTANCE || k == poly::BLEND_SEND || k == poly::EARLY_SEND || k == poly::SHIMMER_SEND;
    case ModuleE::ATMOS:
        return k == atmos::WIND || k == atmos::SWEEPS || k == atmos::BLEEPS || k == atmos::GRAINS || k == atmos::LEVEL
            || k == atmos::ECHO_SEND || k == atmos::REVERB_SEND || k == atmos::LOW_CUT || k == atmos::SHIMMER_SEND;
    default:
        return false;
    }
}

std::vector<std::pair<int, float>> presetKnobs(ModuleE module, const SoundPreset& preset)
{
    ParamStore store;
    std::vector<std::pair<int, float>> out;
    const int count = ParamStore::moduleCount(module);
    for (int k = 0; k < count; ++k) {
        if (presetLeaves(module, k)) continue;
        float v = store.desc(store.id(module, 0, k)).m_defValue;
        for (const auto& e : preset.m_values) if (e.first == k) v = e.second;
        out.push_back({ k, v });
    }
    return out;
}

std::string presetText(const ParamStore& params, ModuleE module, int instance)
{
    std::string out;
    const int count = ParamStore::moduleCount(module);
    for (int k = 0; k < count; ++k) {
        if (presetLeaves(module, k)) continue;
        const int id = params.id(module, instance, k);
        char line[96];
        std::snprintf(line, sizeof(line), "%s=%.6g\n", params.desc(id).m_key, static_cast<double>(params.get(id)));
        out += line;
    }
    return out;
}

bool presetFromText(ModuleE module, const std::string& name, const std::string& text, SoundPreset& out)
{
    ParamStore store;
    out = SoundPreset{};
    out.m_group = "User";
    out.m_name = name;
    const int count = ParamStore::moduleCount(module);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        for (int k = 0; k < count; ++k) {
            const ParamDesc& d = store.desc(store.id(module, 0, k));
            if (key != d.m_key || presetLeaves(module, k)) continue;
            out.m_values.push_back({ k, std::clamp(static_cast<float>(std::atof(line.c_str() + eq + 1)), d.m_minValue, d.m_maxValue) });
        }
    }
    return !out.m_values.empty();
}

void applyPreset(ParamStore& params, ModuleE module, int instance, const SoundPreset& preset)
{
    for (const auto& e : presetKnobs(module, preset)) params.set(params.id(module, instance, e.first), e.second);
}

} // namespace eph
