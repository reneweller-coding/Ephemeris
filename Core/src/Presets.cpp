/**
 * @file Presets.cpp
 * @brief The factory presets: sixteen groups per synth, sixty-four presets per group (Presets.h).
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
    int k;   ///< the knob (its index in the module)
    float lo;   ///< the low end of its range in the group
    float hi;   ///< the high end
    char axis;   ///< what moves it: 'A' the adjective, 'B' the noun, 'R' a draw
};

/** @brief A group: its name, which of the synth's four adjective rows it uses, its nouns and its knobs. */
struct Group {
    const char* name;   ///< the group's name
    int adjectives;   ///< which of the synth's four adjective rows it uses
    const char* nouns[8];   ///< its eight nouns
    std::vector<Axis> axes;   ///< its knobs
};

/** @brief A synth: four rows of adjectives, each from dark to bright, and its sixteen groups. */
struct Synth {
    const char* adjectives[4][8];   ///< four rows of eight adjectives, each from dark to bright
    std::vector<Group> groups;   ///< its sixteen groups
};

// --- The modular voice of the rows --------------------------------------------------------------------------
namespace vo = voice;
/** @brief The modular voice of the rows: its names and groups. */
const Synth& voiceSynth()
{
    static const Synth s{
        { { "Umbral", "Midnight", "Smoky", "Amber", "Copper", "Golden", "Silver", "Radiant" },
          { "Buried", "Nocturnal", "Dusky", "Velvet", "Burnished", "Lucid", "Crystal", "Solar" },
          { "Obsidian", "Slate", "Iron", "Bronze", "Brass", "Chrome", "Neon", "Stellar" },
          { "Abyssal", "Shadowed", "Misty", "Hazy", "Pale", "Clear", "Bright", "Blazing" } },
        {
            { "Ladder Bass", 0, { "Keel", "Anchor", "Engine", "Undertow", "Foundry", "Bedrock", "Axle", "Piston" },
              { { vo::Wave, 0.0f, 0.15f, 'R' }, { vo::Detune, 0.0f, 3.0f, 'R' }, { vo::Drift, 0.5f, 2.0f, 'R' }, { vo::Drive, 4.0f, 12.0f, 'B' },
                { vo::Cutoff, 160.0f, 700.0f, 'A' }, { vo::Resonance, 0.1f, 0.35f, 'B' }, { vo::EnvAmount, 1.5f, 3.5f, 'A' },
                { vo::Decay, 120.0f, 450.0f, 'B' }, { vo::KeyTrack, 0.3f, 0.6f, 'R' }, { vo::Accent, 0.4f, 0.7f, 'R' },
                { vo::AmpDecay, 60.0f, 250.0f, 'B' }, { vo::Glide, 20.0f, 90.0f, 'R' } } },
            { "Deep Ostinato", 1, { "Current", "Tide", "Pendulum", "Heartbeat", "Loop", "Circuit", "Treadmill", "Rotor" },
              { { vo::Wave, 0.0f, 0.35f, 'B' }, { vo::Detune, 0.0f, 2.0f, 'R' }, { vo::Drift, 0.5f, 2.0f, 'R' }, { vo::Drive, 2.0f, 8.0f, 'R' },
                { vo::Cutoff, 110.0f, 380.0f, 'A' }, { vo::Resonance, 0.05f, 0.25f, 'R' }, { vo::EnvAmount, 0.8f, 2.2f, 'A' },
                { vo::Decay, 200.0f, 700.0f, 'B' }, { vo::AmpDecay, 150.0f, 500.0f, 'B' }, { vo::Glide, 30.0f, 120.0f, 'R' } } },
            { "Pluck Sequence", 2, { "Harp", "Quill", "Pizzicato", "Droplet", "Needle", "Spark", "Plectrum", "Pebble" },
              { { vo::Wave, 0.0f, 0.5f, 'R' }, { vo::Detune, 3.0f, 8.0f, 'R' }, { vo::Drift, 1.0f, 3.0f, 'R' }, { vo::Drive, 3.0f, 9.0f, 'R' },
                { vo::Cutoff, 300.0f, 1500.0f, 'A' }, { vo::Resonance, 0.2f, 0.5f, 'B' }, { vo::EnvAmount, 2.5f, 5.0f, 'A' },
                { vo::Decay, 40.0f, 180.0f, 'B' }, { vo::KeyTrack, 0.5f, 0.9f, 'R' }, { vo::AmpDecay, 30.0f, 150.0f, 'B' }, { vo::Glide, 5.0f, 30.0f, 'R' } } },
            { "Resonant Sweep", 3, { "Arc", "Comet", "Crescent", "Horizon", "Sweep", "Aurora", "Nebula", "Parabola" },
              { { vo::Wave, 0.0f, 0.3f, 'R' }, { vo::Detune, 3.0f, 8.0f, 'R' }, { vo::Drive, 4.0f, 10.0f, 'R' }, { vo::Cutoff, 250.0f, 1200.0f, 'A' },
                { vo::Resonance, 0.45f, 0.75f, 'B' }, { vo::EnvAmount, 2.0f, 4.5f, 'A' }, { vo::Decay, 150.0f, 600.0f, 'B' },
                { vo::AmpDecay, 80.0f, 300.0f, 'R' } } },
            { "Hollow Pulse", 0, { "Chamber", "Vessel", "Cavern", "Pipe", "Shell", "Hollow", "Grotto", "Flute" },
              { { vo::Wave, 0.7f, 1.0f, 'R' }, { vo::PulseWidth, 0.12f, 0.45f, 'A' }, { vo::Detune, 4.0f, 10.0f, 'R' },
                { vo::Cutoff, 400.0f, 2500.0f, 'B' }, { vo::Resonance, 0.15f, 0.4f, 'R' }, { vo::EnvAmount, 1.5f, 3.5f, 'R' },
                { vo::Decay, 100.0f, 400.0f, 'R' }, { vo::AmpDecay, 60.0f, 250.0f, 'R' } } },
            { "Glass Arp", 1, { "Prism", "Lattice", "Mirror", "Icicle", "Facet", "Lens", "Quartz", "Filament" },
              { { vo::Wave, 0.4f, 0.9f, 'R' }, { vo::PulseWidth, 0.2f, 0.45f, 'R' }, { vo::Detune, 3.0f, 7.0f, 'R' },
                { vo::Cutoff, 1500.0f, 6000.0f, 'A' }, { vo::Resonance, 0.1f, 0.3f, 'B' }, { vo::EnvAmount, 1.0f, 3.0f, 'R' },
                { vo::Decay, 60.0f, 250.0f, 'B' }, { vo::KeyTrack, 0.6f, 1.0f, 'R' }, { vo::AmpDecay, 40.0f, 200.0f, 'R' } } },
            { "Tape Sequence", 2, { "Reel", "Spool", "Cassette", "Capstan", "Loopback", "Splice", "Leader", "Tapehead" },
              { { vo::Wave, 0.0f, 0.5f, 'R' }, { vo::Detune, 6.0f, 12.0f, 'A' }, { vo::Drift, 3.0f, 6.0f, 'B' }, { vo::Drive, 6.0f, 14.0f, 'R' },
                { vo::Cutoff, 500.0f, 2500.0f, 'A' }, { vo::Resonance, 0.15f, 0.35f, 'R' }, { vo::EnvAmount, 1.5f, 3.0f, 'R' },
                { vo::Decay, 120.0f, 400.0f, 'B' }, { vo::AmpDecay, 60.0f, 250.0f, 'R' } } },
            { "Soft Pad Voice", 3, { "Haze", "Veil", "Mist", "Cloudbank", "Breath", "Down", "Fleece", "Murmur" },
              { { vo::Wave, 0.0f, 0.4f, 'R' }, { vo::Detune, 5.0f, 10.0f, 'R' }, { vo::Drive, 0.0f, 4.0f, 'R' }, { vo::Cutoff, 150.0f, 500.0f, 'A' },
                { vo::Resonance, 0.0f, 0.15f, 'R' }, { vo::EnvAmount, 0.5f, 1.5f, 'B' }, { vo::Decay, 300.0f, 1200.0f, 'R' },
                { vo::AmpDecay, 200.0f, 800.0f, 'B' }, { vo::Glide, 40.0f, 150.0f, 'R' } } },
            { "Squelch Arp", 0, { "Circuitry", "Squall", "Gremlin", "Relay", "Sprocket", "Gear", "Zapper", "Coil" },
              { { vo::Wave, 0.0f, 0.4f, 'R' }, { vo::Detune, 2.0f, 6.0f, 'R' }, { vo::Drive, 8.0f, 16.0f, 'B' }, { vo::Cutoff, 200.0f, 900.0f, 'A' },
                { vo::Resonance, 0.55f, 0.8f, 'B' }, { vo::EnvAmount, 3.0f, 5.5f, 'A' }, { vo::Decay, 80.0f, 250.0f, 'R' },
                { vo::AmpDecay, 40.0f, 180.0f, 'R' } } },
            { "Staccato Pulse", 1, { "Morse", "Ticker", "Metronome", "Telegraph", "Beacon", "Signal", "Blip", "Semaphore" },
              { { vo::Wave, 0.3f, 1.0f, 'R' }, { vo::PulseWidth, 0.2f, 0.5f, 'R' }, { vo::Detune, 3.0f, 7.0f, 'R' },
                { vo::Cutoff, 500.0f, 2500.0f, 'A' }, { vo::Resonance, 0.1f, 0.35f, 'R' }, { vo::EnvAmount, 1.5f, 3.5f, 'R' },
                { vo::Decay, 30.0f, 120.0f, 'B' }, { vo::AmpDecay, 15.0f, 60.0f, 'B' } } },
            { "Legato Glide", 2, { "Glider", "Slipstream", "Wake", "Contrail", "Glissade", "Ribbon", "Sailplane", "Updraft" },
              { { vo::Wave, 0.0f, 0.6f, 'R' }, { vo::Detune, 3.0f, 8.0f, 'R' }, { vo::Cutoff, 300.0f, 1500.0f, 'A' },
                { vo::Resonance, 0.15f, 0.4f, 'R' }, { vo::EnvAmount, 1.0f, 3.0f, 'R' }, { vo::Decay, 300.0f, 900.0f, 'B' },
                { vo::AmpDecay, 300.0f, 900.0f, 'B' }, { vo::Glide, 120.0f, 400.0f, 'A' } } },
            { "Warm Unison", 3, { "Hearth", "Ember", "Lantern", "Candle", "Kiln", "Fireside", "Furnace", "Brazier" },
              { { vo::Wave, 0.0f, 0.3f, 'R' }, { vo::Detune, 8.0f, 12.0f, 'A' }, { vo::Drift, 2.0f, 4.0f, 'R' }, { vo::Drive, 3.0f, 8.0f, 'R' },
                { vo::Cutoff, 600.0f, 2000.0f, 'B' }, { vo::Resonance, 0.1f, 0.3f, 'R' }, { vo::EnvAmount, 1.0f, 2.5f, 'R' },
                { vo::Decay, 150.0f, 500.0f, 'R' }, { vo::AmpDecay, 80.0f, 300.0f, 'R' } } },
            { "Bright Stab", 0, { "Flare", "Flash", "Strobe", "Blade", "Shard", "Spike", "Laser", "Glint" },
              { { vo::Wave, 0.2f, 0.8f, 'R' }, { vo::Detune, 4.0f, 9.0f, 'R' }, { vo::Cutoff, 2000.0f, 8000.0f, 'A' },
                { vo::Resonance, 0.1f, 0.3f, 'R' }, { vo::EnvAmount, 1.0f, 2.0f, 'R' }, { vo::Decay, 50.0f, 200.0f, 'B' },
                { vo::AmpDecay, 80.0f, 250.0f, 'B' } } },
            { "Dark Throb", 1, { "Tremor", "Quake", "Magma", "Rift", "Chasm", "Growl", "Rumble", "Maelstrom" },
              { { vo::Wave, 0.0f, 0.2f, 'R' }, { vo::Detune, 0.0f, 4.0f, 'R' }, { vo::Drive, 8.0f, 16.0f, 'R' }, { vo::Cutoff, 80.0f, 260.0f, 'A' },
                { vo::Resonance, 0.3f, 0.55f, 'B' }, { vo::EnvAmount, 2.0f, 4.0f, 'R' }, { vo::Decay, 250.0f, 800.0f, 'B' },
                { vo::AmpDecay, 100.0f, 400.0f, 'R' } } },
            { "Accent Ratchet", 2, { "Ratchet", "Rattle", "Stutter", "Clockwork", "Escapement", "Gallop", "Trigger", "Hammer" },
              { { vo::Wave, 0.0f, 0.7f, 'R' }, { vo::Detune, 2.0f, 6.0f, 'R' }, { vo::Accent, 0.6f, 1.0f, 'B' }, { vo::Cutoff, 400.0f, 2000.0f, 'A' },
                { vo::Resonance, 0.2f, 0.45f, 'R' }, { vo::EnvAmount, 2.0f, 4.0f, 'R' }, { vo::Decay, 40.0f, 150.0f, 'B' },
                { vo::AmpDecay, 25.0f, 90.0f, 'R' } } },
            { "Cosmic Drip", 3, { "Pulsar", "Quasar", "Orbit", "Satellite", "Meteor", "Planetoid", "Starfall", "Zenith" },
              { { vo::Wave, 0.2f, 0.7f, 'R' }, { vo::Detune, 4.0f, 9.0f, 'R' }, { vo::Resonance, 0.35f, 0.6f, 'B' },
                { vo::Cutoff, 800.0f, 3000.0f, 'A' }, { vo::EnvAmount, 1.5f, 4.0f, 'R' }, { vo::Decay, 100.0f, 400.0f, 'B' },
                { vo::KeyTrack, 0.8f, 1.0f, 'R' }, { vo::Glide, 40.0f, 150.0f, 'R' }, { vo::AmpDecay, 60.0f, 240.0f, 'R' } } },
        } };
    return s;
}

// --- The lead ---------------------------------------------------------------------------------------------------
namespace le = lead;
/** @brief The lead's names and groups. */
const Synth& leadSynth()
{
    static const Synth s{
        { { "Distant", "Lonely", "Dreaming", "Wandering", "Soaring", "Singing", "Shining", "Blazing" },
          { "Hushed", "Silken", "Mellow", "Tender", "Vivid", "Keen", "Piercing", "Brilliant" },
          { "Moonlit", "Starlit", "Twilit", "Misty", "Clear", "Sunlit", "Gleaming", "Dazzling" },
          { "Hollow", "Grainy", "Rustic", "Smooth", "Polished", "Glossy", "Glassy", "Sparkling" } },
        {
            { "Solo Saw", 0, { "Voyager", "Wanderer", "Pilgrim", "Navigator", "Nomad", "Seeker", "Traveller", "Rover" },
              { { le::Wave, 0.0f, 0.2f, 'R' }, { le::Detune, 2.0f, 6.0f, 'R' }, { le::Drive, 6.0f, 12.0f, 'R' }, { le::Cutoff, 1000.0f, 4000.0f, 'A' },
                { le::Resonance, 0.15f, 0.35f, 'B' }, { le::EnvAmount, 1.0f, 3.0f, 'R' }, { le::Decay, 300.0f, 900.0f, 'B' },
                { le::AmpDecay, 200.0f, 600.0f, 'R' }, { le::Glide, 60.0f, 150.0f, 'R' }, { le::Vibrato, 12.0f, 24.0f, 'R' }, { le::VibratoRate, 4.6f, 5.8f, 'R' } } },
            { "Singing Pulse", 1, { "Aria", "Cantor", "Hymn", "Canticle", "Chant", "Refrain", "Ballad", "Carol" },
              { { le::Wave, 0.7f, 1.0f, 'R' }, { le::PulseWidth, 0.2f, 0.45f, 'A' }, { le::Detune, 2.0f, 5.0f, 'R' }, { le::Cutoff, 1200.0f, 4000.0f, 'B' },
                { le::Resonance, 0.1f, 0.3f, 'R' }, { le::Vibrato, 15.0f, 30.0f, 'R' }, { le::Glide, 80.0f, 200.0f, 'R' }, { le::VibratoRate, 4.8f, 6.0f, 'R' } } },
            { "Flute Lead", 2, { "Reed", "Piccolo", "Ocarina", "Fife", "Recorder", "Panpipe", "Pipe", "Kaval" },
              { { le::Wave, 0.6f, 0.9f, 'R' }, { le::PulseWidth, 0.4f, 0.5f, 'R' }, { le::Detune, 1.0f, 3.0f, 'R' }, { le::Drive, 0.0f, 4.0f, 'R' },
                { le::Cutoff, 900.0f, 2500.0f, 'A' }, { le::Resonance, 0.02f, 0.15f, 'R' }, { le::EnvAmount, 0.3f, 1.2f, 'B' },
                { le::AmpDecay, 150.0f, 400.0f, 'R' }, { le::Glide, 20.0f, 60.0f, 'R' }, { le::Vibrato, 8.0f, 20.0f, 'B' }, { le::VibratoRate, 4.8f, 5.8f, 'R' } } },
            { "Portamento", 3, { "Slide", "Swoop", "Glide", "Swan", "Heron", "Falcon", "Kestrel", "Albatross" },
              { { le::Wave, 0.0f, 0.4f, 'R' }, { le::Detune, 2.0f, 6.0f, 'R' }, { le::Cutoff, 800.0f, 3000.0f, 'A' }, { le::Resonance, 0.2f, 0.4f, 'R' },
                { le::Glide, 180.0f, 450.0f, 'B' }, { le::Vibrato, 10.0f, 25.0f, 'R' }, { le::AmpDecay, 250.0f, 700.0f, 'R' } } },
            { "Glass Whistle", 0, { "Icewind", "Frost", "Glacier", "Snowfield", "Hailstone", "Rime", "Firn", "Floe" },
              { { le::Wave, 0.5f, 0.9f, 'R' }, { le::Detune, 1.0f, 4.0f, 'R' }, { le::Cutoff, 3000.0f, 8000.0f, 'A' }, { le::Resonance, 0.05f, 0.2f, 'R' },
                { le::EnvAmount, 0.5f, 1.5f, 'B' }, { le::Vibrato, 5.0f, 15.0f, 'R' }, { le::Glide, 30.0f, 100.0f, 'R' } } },
            { "Theremin", 1, { "Ghost", "Phantom", "Specter", "Wraith", "Apparition", "Spirit", "Shade", "Revenant" },
              { { le::Wave, 0.85f, 1.0f, 'R' }, { le::PulseWidth, 0.45f, 0.5f, 'R' }, { le::Detune, 0.0f, 2.0f, 'R' }, { le::Cutoff, 700.0f, 2000.0f, 'A' },
                { le::Resonance, 0.0f, 0.1f, 'R' }, { le::Glide, 250.0f, 500.0f, 'B' }, { le::Vibrato, 25.0f, 40.0f, 'B' }, { le::VibratoRate, 5.0f, 6.5f, 'R' },
                { le::AmpDecay, 300.0f, 800.0f, 'R' } } },
            { "Screaming Filter", 2, { "Tempest", "Cyclone", "Hurricane", "Typhoon", "Gale", "Storm", "Whirlwind", "Tornado" },
              { { le::Wave, 0.0f, 0.3f, 'R' }, { le::Detune, 2.0f, 6.0f, 'R' }, { le::Drive, 10.0f, 18.0f, 'R' }, { le::Cutoff, 1500.0f, 5000.0f, 'A' },
                { le::Resonance, 0.5f, 0.72f, 'B' }, { le::EnvAmount, 1.5f, 3.0f, 'R' }, { le::Vibrato, 10.0f, 25.0f, 'R' } } },
            { "Soft Horn", 3, { "Horn", "Bugle", "Cornet", "Clarion", "Herald", "Fanfare", "Summons", "Call" },
              { { le::Wave, 0.2f, 0.5f, 'R' }, { le::Detune, 2.0f, 5.0f, 'R' }, { le::Cutoff, 600.0f, 1800.0f, 'A' }, { le::Resonance, 0.05f, 0.2f, 'R' },
                { le::EnvAmount, 1.5f, 3.0f, 'B' }, { le::Decay, 150.0f, 400.0f, 'R' }, { le::AmpDecay, 200.0f, 500.0f, 'R' }, { le::Vibrato, 6.0f, 16.0f, 'R' } } },
            { "Ethereal Sine", 0, { "Halo", "Aura", "Nimbus", "Corona", "Glow", "Radiance", "Luster", "Gleam" },
              { { le::Wave, 0.9f, 1.0f, 'R' }, { le::PulseWidth, 0.48f, 0.5f, 'R' }, { le::Detune, 0.0f, 2.0f, 'R' }, { le::Drive, 0.0f, 2.0f, 'R' },
                { le::Cutoff, 500.0f, 1300.0f, 'A' }, { le::Resonance, 0.0f, 0.05f, 'R' }, { le::Vibrato, 5.0f, 15.0f, 'B' }, { le::Glide, 60.0f, 200.0f, 'R' } } },
            { "Brass Lead", 1, { "Trumpet", "Trombone", "Brasswind", "Bellow", "Proclamation", "Decree", "Anthem", "Salute" },
              { { le::Wave, 0.0f, 0.3f, 'R' }, { le::Detune, 3.0f, 7.0f, 'R' }, { le::Drive, 8.0f, 14.0f, 'R' }, { le::Cutoff, 800.0f, 2500.0f, 'A' },
                { le::Resonance, 0.1f, 0.3f, 'R' }, { le::EnvAmount, 2.0f, 4.0f, 'B' }, { le::Decay, 100.0f, 300.0f, 'R' }, { le::Vibrato, 8.0f, 18.0f, 'R' } } },
            { "Hollow Oboe", 2, { "Oboe", "Chalumeau", "Shawm", "Bassoon", "Duduk", "Cor", "Crumhorn", "Musette" },
              { { le::Wave, 0.8f, 1.0f, 'R' }, { le::PulseWidth, 0.08f, 0.25f, 'A' }, { le::Detune, 1.0f, 4.0f, 'R' }, { le::Cutoff, 1000.0f, 3000.0f, 'B' },
                { le::Resonance, 0.15f, 0.35f, 'R' }, { le::Vibrato, 8.0f, 18.0f, 'R' } } },
            { "Twin Oscillator", 3, { "Gemini", "Twin", "Binary", "Duet", "Doublet", "Tandem", "Castor", "Pollux" },
              { { le::Wave, 0.0f, 0.5f, 'R' }, { le::Detune, 5.0f, 10.0f, 'A' }, { le::Cutoff, 1200.0f, 3500.0f, 'B' }, { le::Resonance, 0.1f, 0.3f, 'R' },
                { le::Vibrato, 8.0f, 20.0f, 'R' }, { le::Drift, 1.0f, 3.0f, 'R' } } },
            { "Cosmic Siren", 0, { "Siren", "Signal", "Beacon", "Lighthouse", "Transmitter", "Emitter", "Broadcast", "Callsign" },
              { { le::Wave, 0.3f, 0.8f, 'R' }, { le::Detune, 2.0f, 6.0f, 'R' }, { le::Cutoff, 2000.0f, 6000.0f, 'A' }, { le::Resonance, 0.3f, 0.55f, 'R' },
                { le::Glide, 200.0f, 450.0f, 'B' }, { le::Vibrato, 20.0f, 35.0f, 'R' } } },
            { "Warm Mono", 1, { "Solace", "Comfort", "Refuge", "Harbor", "Haven", "Shelter", "Sanctuary", "Retreat" },
              { { le::Wave, 0.0f, 0.3f, 'R' }, { le::Detune, 2.0f, 5.0f, 'R' }, { le::Drive, 4.0f, 9.0f, 'R' }, { le::Cutoff, 500.0f, 1500.0f, 'A' },
                { le::Resonance, 0.1f, 0.25f, 'R' }, { le::EnvAmount, 1.0f, 2.5f, 'B' }, { le::Vibrato, 10.0f, 20.0f, 'R' } } },
            { "Bell Lead", 2, { "Bell", "Chime", "Carillon", "Tolling", "Peal", "Knell", "Campanile", "Belfry" },
              { { le::Wave, 0.4f, 0.8f, 'R' }, { le::Detune, 2.0f, 5.0f, 'R' }, { le::Cutoff, 2000.0f, 6000.0f, 'A' }, { le::Resonance, 0.2f, 0.4f, 'R' },
                { le::EnvAmount, 2.0f, 4.0f, 'R' }, { le::Decay, 80.0f, 250.0f, 'B' }, { le::AmpDecay, 600.0f, 1500.0f, 'B' }, { le::Vibrato, 0.0f, 8.0f, 'R' } } },
            { "Dusty Solo", 3, { "Relic", "Artifact", "Heirloom", "Memento", "Keepsake", "Souvenir", "Fossil", "Remnant" },
              { { le::Wave, 0.0f, 0.5f, 'R' }, { le::Drift, 2.5f, 4.0f, 'A' }, { le::Detune, 3.0f, 8.0f, 'R' }, { le::Cutoff, 700.0f, 2500.0f, 'B' },
                { le::Drive, 6.0f, 12.0f, 'R' }, { le::Vibrato, 12.0f, 25.0f, 'R' } } },
        } };
    return s;
}

// --- The drone (the lead's table): the foundation, in pure intervals -----------------------------------------------
/** @brief The drone's names and groups: the foundation, in pure intervals. */
const Synth& droneSynth()
{
    // Every group: detune 0 .. 1 cent (the addon's pure foundation), no accent, a long release.
    auto base = [](std::vector<Axis> a) {
        a.push_back({ le::Detune, 0.0f, 1.0f, 'R' });
        a.push_back({ le::Accent, 0.0f, 0.0f, 'R' });
        a.push_back({ le::AmpDecay, 1200.0f, 2000.0f, 'R' });
        return a;
    };
    static const Synth s{
        { { "Sunken", "Buried", "Earthen", "Granite", "Ancient", "Hallowed", "Gilded", "Sunlit" },
          { "Nameless", "Silent", "Endless", "Patient", "Steady", "Glowing", "Burning", "Shining" },
          { "Black", "Ashen", "Grey", "Umber", "Ochre", "Russet", "Amber", "Ivory" },
          { "Polar", "Arctic", "Tundra", "Boreal", "Temperate", "Tropic", "Equatorial", "Solar" } },
        {
            { "Dark Bordun", 0, { "Bordun", "Ground", "Root", "Basis", "Plinth", "Pillar", "Pedestal", "Footing" },
              base({ { le::Wave, 0.0f, 0.2f, 'R' }, { le::Cutoff, 120.0f, 380.0f, 'A' }, { le::Resonance, 0.1f, 0.35f, 'B' }, { le::Drive, 2.0f, 6.0f, 'R' }, { le::Drift, 2.0f, 5.0f, 'R' } }) },
            { "Organ Pedal", 1, { "Organ", "Diapason", "Bourdon", "Principal", "Register", "Manual", "Console", "Pipework" },
              base({ { le::Wave, 0.8f, 1.0f, 'R' }, { le::PulseWidth, 0.3f, 0.5f, 'A' }, { le::Cutoff, 300.0f, 900.0f, 'B' }, { le::Resonance, 0.05f, 0.2f, 'R' }, { le::Drive, 0.0f, 3.0f, 'R' } }) },
            { "Pulse Hum", 2, { "Hum", "Drone", "Buzz", "Thrum", "Purr", "Whir", "Throb", "Murmur" },
              base({ { le::Wave, 0.6f, 0.9f, 'R' }, { le::PulseWidth, 0.1f, 0.3f, 'A' }, { le::Cutoff, 200.0f, 600.0f, 'B' }, { le::Resonance, 0.2f, 0.4f, 'R' } }) },
            { "Resonant Earth", 3, { "Canyon", "Gorge", "Ravine", "Basin", "Valley", "Crater", "Caldera", "Fjord" },
              base({ { le::Wave, 0.0f, 0.3f, 'R' }, { le::Cutoff, 150.0f, 450.0f, 'A' }, { le::Resonance, 0.45f, 0.7f, 'B' }, { le::Drive, 3.0f, 7.0f, 'R' } }) },
            { "Glowing Root", 0, { "Emberbed", "Coalface", "Cinder", "Hearthstone", "Glow", "Smoulder", "Brand", "Coal" },
              base({ { le::Wave, 0.0f, 0.3f, 'R' }, { le::Cutoff, 400.0f, 1200.0f, 'A' }, { le::Resonance, 0.15f, 0.35f, 'R' }, { le::Drive, 4.0f, 10.0f, 'B' } }) },
            { "Hollow Pipe", 1, { "Tube", "Conduit", "Culvert", "Tunnel", "Shaft", "Well", "Cistern", "Aqueduct" },
              base({ { le::Wave, 0.9f, 1.0f, 'R' }, { le::PulseWidth, 0.08f, 0.2f, 'A' }, { le::Cutoff, 300.0f, 800.0f, 'B' }, { le::Resonance, 0.1f, 0.3f, 'R' } }) },
            { "Warm Floor", 2, { "Loam", "Soil", "Humus", "Clay", "Peat", "Marl", "Silt", "Terra" },
              base({ { le::Wave, 0.1f, 0.4f, 'R' }, { le::Cutoff, 250.0f, 700.0f, 'A' }, { le::Resonance, 0.05f, 0.2f, 'R' }, { le::Drive, 5.0f, 12.0f, 'B' } }) },
            { "Filtered Void", 3, { "Void", "Vacuum", "Nadir", "Absence", "Stillness", "Vastness", "Nullspace", "Deepfield" },
              base({ { le::Wave, 0.0f, 0.2f, 'R' }, { le::Cutoff, 80.0f, 200.0f, 'A' }, { le::Resonance, 0.2f, 0.5f, 'B' }, { le::Drift, 3.0f, 6.0f, 'R' } }) },
            { "Singing Ground", 0, { "Hymnal", "Psalm", "Plainsong", "Vespers", "Matins", "Compline", "Litany", "Requiem" },
              base({ { le::Wave, 0.2f, 0.5f, 'R' }, { le::Cutoff, 500.0f, 1500.0f, 'A' }, { le::Resonance, 0.3f, 0.5f, 'B' }, { le::Vibrato, 1.0f, 3.0f, 'R' }, { le::VibratoRate, 1.5f, 3.0f, 'R' } }) },
            { "Iron Drone", 1, { "Anvil", "Forge", "Smelter", "Crucible", "Girder", "Rivet", "Bulkhead", "Hull" },
              base({ { le::Wave, 0.0f, 0.3f, 'R' }, { le::Drive, 12.0f, 20.0f, 'B' }, { le::Cutoff, 200.0f, 700.0f, 'A' }, { le::Resonance, 0.2f, 0.4f, 'R' } }) },
            { "Soft Monolith", 2, { "Monolith", "Obelisk", "Menhir", "Dolmen", "Cairn", "Stele", "Megalith", "Henge" },
              base({ { le::Wave, 0.5f, 0.8f, 'R' }, { le::Cutoff, 150.0f, 400.0f, 'A' }, { le::Resonance, 0.0f, 0.15f, 'B' }, { le::Drive, 0.0f, 3.0f, 'R' } }) },
            { "Bright Axis", 3, { "Axis", "Meridian", "Equator", "Latitude", "Longitude", "Zenith", "Solstice", "Equinox" },
              base({ { le::Wave, 0.2f, 0.6f, 'R' }, { le::Cutoff, 1000.0f, 3000.0f, 'A' }, { le::Resonance, 0.1f, 0.3f, 'B' } }) },
            { "Deep Current", 0, { "Gulfstream", "Riptide", "Upwelling", "Thermocline", "Seafloor", "Trench", "Seamount", "Benthos" },
              base({ { le::Wave, 0.0f, 0.2f, 'R' }, { le::Cutoff, 100.0f, 300.0f, 'A' }, { le::EnvAmount, 0.5f, 2.0f, 'B' }, { le::Decay, 800.0f, 2000.0f, 'R' } }) },
            { "Breathing Root", 1, { "Lungs", "Bellows", "Heave", "Swell", "Respite", "Sigh", "Inhale", "Exhale" },
              base({ { le::Wave, 0.0f, 0.4f, 'R' }, { le::Drift, 4.0f, 7.0f, 'B' }, { le::Cutoff, 200.0f, 600.0f, 'A' }, { le::Resonance, 0.15f, 0.35f, 'R' } }) },
            { "Temple Hum", 2, { "Temple", "Shrine", "Sanctum", "Crypt", "Chapel", "Cloister", "Nave", "Apse" },
              base({ { le::Wave, 0.7f, 1.0f, 'R' }, { le::PulseWidth, 0.35f, 0.5f, 'R' }, { le::Cutoff, 250.0f, 700.0f, 'A' }, { le::Resonance, 0.25f, 0.45f, 'B' } }) },
            { "Night Floor", 3, { "Nightfall", "Dusk", "Gloaming", "Eventide", "Vigil", "Nocturne", "Nightwatch", "Moonset" },
              base({ { le::Wave, 0.0f, 0.3f, 'R' }, { le::Cutoff, 100.0f, 260.0f, 'A' }, { le::Resonance, 0.05f, 0.25f, 'R' }, { le::Drive, 1.0f, 5.0f, 'B' } }) },
        } };
    return s;
}

// --- The tape keys: choir, strings and flute tapes --------------------------------------------------------------------
/** @brief The tape keys' names and groups: choir, strings and flute tapes. */
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
              { { tape::Set, 0.0f, 0.0f, 'R' }, { tape::Vowel, 0.1f, 0.4f, 'B' }, { tape::Tone, 5000.0f, 9000.0f, 'A' }, { tape::Age, 0.2f, 0.5f, 'R' },
                { tape::Wow, 3.0f, 6.0f, 'R' }, { tape::Flutter, 1.0f, 2.0f, 'R' }, { tape::Sag, 0.5f, 1.2f, 'R' } } },
            { "Dark Choir", 1, { "Lament", "Dirge", "Elegy", "Threnody", "Mourners", "Keening", "Wake", "Pall" },
              { { tape::Set, 0.0f, 0.0f, 'R' }, { tape::Vowel, 0.0f, 0.3f, 'R' }, { tape::Tone, 2000.0f, 4500.0f, 'A' }, { tape::Age, 0.4f, 0.7f, 'B' },
                { tape::Wow, 4.0f, 8.0f, 'R' } } },
            { "Worn Choir", 2, { "Archive", "Attic", "Scrapbook", "Photograph", "Letter", "Diary", "Almanac", "Ledger" },
              { { tape::Set, 0.0f, 0.0f, 'R' }, { tape::Age, 0.7f, 1.0f, 'A' }, { tape::Wow, 7.0f, 12.0f, 'B' }, { tape::Flutter, 2.0f, 4.0f, 'R' },
                { tape::Tone, 3000.0f, 6000.0f, 'R' } } },
            { "Bright Choir", 3, { "Seraph", "Cherub", "Angels", "Heralds", "Hosanna", "Gloria", "Alleluia", "Sanctus" },
              { { tape::Set, 0.0f, 0.0f, 'R' }, { tape::Tone, 8000.0f, 14000.0f, 'A' }, { tape::Age, 0.0f, 0.3f, 'R' }, { tape::Vowel, 0.3f, 0.7f, 'B' },
                { tape::Wow, 2.0f, 4.0f, 'R' } } },
            { "Vowel Choir", 0, { "Vowels", "Syllables", "Phonemes", "Diphthong", "Whispers", "Utterance", "Murmurs", "Breaths" },
              { { tape::Set, 0.0f, 0.0f, 'R' }, { tape::Vowel, 0.0f, 1.0f, 'A' }, { tape::Tone, 4000.0f, 8000.0f, 'B' }, { tape::Age, 0.3f, 0.6f, 'R' } } },
            { "Ghost Choir", 1, { "Ghosts", "Phantoms", "Echoes", "Shadows", "Spirits", "Revenants", "Wraiths", "Specters" },
              { { tape::Set, 0.0f, 0.0f, 'R' }, { tape::Tone, 2500.0f, 5000.0f, 'A' }, { tape::Age, 0.6f, 0.9f, 'R' }, { tape::Wow, 6.0f, 10.0f, 'B' },
                { tape::Vowel, 0.5f, 0.9f, 'R' } } },
            { "Silk Strings", 2, { "Silk", "Satin", "Chiffon", "Gossamer", "Organza", "Lace", "Taffeta", "Voile" },
              { { tape::Set, 1.0f, 1.0f, 'R' }, { tape::Tone, 5000.0f, 10000.0f, 'A' }, { tape::Age, 0.1f, 0.4f, 'B' }, { tape::Wow, 2.0f, 5.0f, 'R' } } },
            { "Worn Strings", 3, { "Gramophone", "Phonograph", "Wireless", "Parlour", "Salon", "Ballroom", "Tearoom", "Veranda" },
              { { tape::Set, 1.0f, 1.0f, 'R' }, { tape::Age, 0.6f, 1.0f, 'A' }, { tape::Wow, 6.0f, 11.0f, 'B' }, { tape::Flutter, 1.5f, 3.5f, 'R' } } },
            { "Dark Strings", 0, { "Autumn", "November", "Winter", "Harvest", "Fallow", "Twilight", "Evensong", "Candlemas" },
              { { tape::Set, 1.0f, 1.0f, 'R' }, { tape::Tone, 1800.0f, 4000.0f, 'A' }, { tape::Age, 0.3f, 0.6f, 'B' }, { tape::Wow, 3.0f, 7.0f, 'R' } } },
            { "Bright Strings", 1, { "Spring", "April", "Meadow", "Blossom", "Morning", "Daybreak", "Sunrise", "Summer" },
              { { tape::Set, 1.0f, 1.0f, 'R' }, { tape::Tone, 9000.0f, 15000.0f, 'A' }, { tape::Age, 0.0f, 0.3f, 'R' }, { tape::Wow, 2.0f, 4.0f, 'B' } } },
            { "Warped Strings", 2, { "Warp", "Wobble", "Mirage", "Carousel", "Funhouse", "Seasick", "Swoon", "Vertigo" },
              { { tape::Set, 1.0f, 1.0f, 'R' }, { tape::Wow, 9.0f, 12.0f, 'A' }, { tape::Sag, 1.5f, 2.5f, 'B' }, { tape::Flutter, 2.0f, 4.0f, 'R' } } },
            { "Pure Flute", 3, { "Brook", "Stream", "Rill", "Creek", "Rivulet", "Fountain", "Cascade", "Wellspring" },
              { { tape::Set, 2.0f, 2.0f, 'R' }, { tape::Tone, 6000.0f, 12000.0f, 'A' }, { tape::Age, 0.0f, 0.3f, 'B' }, { tape::Wow, 2.0f, 4.0f, 'R' } } },
            { "Breathy Flute", 0, { "Breeze", "Zephyr", "Draught", "Gust", "Puff", "Waft", "Airflow", "Sough" },
              { { tape::Set, 2.0f, 2.0f, 'R' }, { tape::Tone, 4000.0f, 8000.0f, 'A' }, { tape::Age, 0.3f, 0.6f, 'B' }, { tape::Vowel, 0.3f, 0.8f, 'R' } } },
            { "Worn Flute", 1, { "Shepherd", "Herder", "Hillside", "Pasture", "Flock", "Crook", "Fold", "Heath" },
              { { tape::Set, 2.0f, 2.0f, 'R' }, { tape::Age, 0.6f, 1.0f, 'A' }, { tape::Wow, 6.0f, 11.0f, 'B' }, { tape::Tone, 3000.0f, 7000.0f, 'R' } } },
            { "Dark Flute", 2, { "Forest", "Thicket", "Grove", "Copse", "Bramble", "Undergrowth", "Wildwood", "Deadwood" },
              { { tape::Set, 2.0f, 2.0f, 'R' }, { tape::Tone, 1800.0f, 4000.0f, 'A' }, { tape::Age, 0.2f, 0.6f, 'B' } } },
            { "Wobbly Flute", 3, { "Kite", "Balloon", "Pinwheel", "Whirligig", "Weathervane", "Windmill", "Mobile", "Yoyo" },
              { { tape::Set, 2.0f, 2.0f, 'R' }, { tape::Wow, 8.0f, 12.0f, 'A' }, { tape::Flutter, 2.5f, 4.0f, 'B' }, { tape::Sag, 1.0f, 2.5f, 'R' } } },
        } };
    return s;
}

// --- The string machine ---------------------------------------------------------------------------------------------------
namespace st = strings;
/** @brief The string machine's names and groups. */
const Synth& stringsSynth()
{
    static const Synth s{
        { { "Velvet", "Dusky", "Hazy", "Soft", "Silken", "Lustrous", "Radiant", "Luminous" },
          { "Frozen", "Wintry", "Cool", "Calm", "Serene", "Balmy", "Sunny", "Golden" },
          { "Deep", "Dark", "Shaded", "Muted", "Glowing", "Bright", "Vivid", "Brilliant" },
          { "Slow", "Drifting", "Floating", "Hovering", "Rising", "Soaring", "Ascending", "Celestial" } },
        {
            { "Violin Section", 0, { "Bow", "Rosin", "Fiddle", "Scroll", "Fingerboard", "Tailpiece", "Soundpost", "Purfling" },
              { { st::Registration, 0.0f, 0.4f, 'R' }, { st::Tone, 3500.0f, 8000.0f, 'A' }, { st::Ensemble, 0.6f, 0.9f, 'B' }, { st::Animate, 0.1f, 0.3f, 'R' },
                { st::Attack, 0.2f, 0.8f, 'R' }, { st::Release, 0.8f, 2.0f, 'R' } } },
            { "Viola Haze", 1, { "Serenade", "Sonatina", "Romance", "Pavane", "Siciliano", "Barcarolle", "Berceuse", "Reverie" },
              { { st::Registration, 0.8f, 1.3f, 'R' }, { st::Tone, 2500.0f, 6000.0f, 'A' }, { st::Ensemble, 0.7f, 1.0f, 'B' }, { st::Animate, 0.2f, 0.4f, 'R' } } },
            { "Cello Floor", 2, { "Mahogany", "Walnut", "Ebony", "Rosewood", "Maple", "Spruce", "Cedar", "Oak" },
              { { st::Registration, 1.8f, 2.3f, 'R' }, { st::Feet, 0.2f, 0.5f, 'R' }, { st::Tone, 1500.0f, 4000.0f, 'A' }, { st::Attack, 0.3f, 1.2f, 'B' } } },
            { "Bass Strings", 3, { "Cellar", "Vaults", "Catacomb", "Bedrock", "Foundations", "Cavern", "Grotto", "Undercroft" },
              { { st::Registration, 2.8f, 3.2f, 'R' }, { st::Feet, 0.1f, 0.4f, 'A' }, { st::Tone, 1200.0f, 3000.0f, 'B' } } },
            { "Full Ensemble", 0, { "Orchestra", "Symphony", "Ensemble", "Philharmonic", "Chorale", "Tutti", "Consort", "Assembly" },
              { { st::Registration, 3.8f, 4.3f, 'R' }, { st::Tone, 3000.0f, 7000.0f, 'A' }, { st::Ensemble, 0.7f, 1.0f, 'B' } } },
            { "Hollow Strings", 1, { "Conch", "Gourd", "Husk", "Pod", "Calabash", "Nutshell", "Cocoon", "Seashell" },
              { { st::Registration, 4.8f, 5.3f, 'R' }, { st::Tone, 2500.0f, 6000.0f, 'A' }, { st::Phaser, 0.0f, 0.3f, 'B' } } },
            { "Brass Machine", 2, { "Fanfare", "Procession", "Parade", "Pageant", "Ceremony", "Coronation", "Triumph", "Jubilee" },
              { { st::Registration, 5.8f, 6.3f, 'R' }, { st::Tone, 3000.0f, 8000.0f, 'A' }, { st::Attack, 0.05f, 0.3f, 'B' }, { st::Ensemble, 0.3f, 0.7f, 'R' } } },
            { "String Organ", 3, { "Harmonium", "Chapel", "Vestry", "Sacristy", "Parish", "Hymnbook", "Pew", "Steeple" },
              { { st::Registration, 6.7f, 7.0f, 'R' }, { st::Tone, 2500.0f, 6000.0f, 'A' }, { st::Ensemble, 0.4f, 0.8f, 'B' } } },
            { "Animated Blend", 0, { "Kaleidoscope", "Chameleon", "Mosaic", "Tapestry", "Quilt", "Patchwork", "Collage", "Montage" },
              { { st::Registration, 0.0f, 7.0f, 'A' }, { st::Animate, 0.6f, 1.0f, 'B' }, { st::AnimateRate, 0.02f, 0.12f, 'R' } } },
            { "Phased Strings", 1, { "Swirl", "Vortex", "Eddy", "Spiral", "Whirlpool", "Gyre", "Maelstrom", "Helix" },
              { { st::Registration, 0.0f, 4.0f, 'A' }, { st::Phaser, 0.4f, 0.9f, 'B' }, { st::Ensemble, 0.6f, 0.9f, 'R' } } },
            { "Slow Swell", 2, { "Tidewater", "Swell", "Surge", "Floodtide", "Groundswell", "Upsurge", "Crest", "Billow" },
              { { st::Attack, 1.2f, 3.0f, 'A' }, { st::Release, 2.0f, 5.0f, 'B' }, { st::Registration, 0.0f, 5.0f, 'R' } } },
            { "Short Bow", 3, { "Staccato", "Spiccato", "Pizzicato", "Detache", "Martele", "Sautille", "Ricochet", "Tremolo" },
              { { st::Attack, 0.01f, 0.08f, 'A' }, { st::Release, 0.2f, 0.6f, 'B' }, { st::Tone, 3000.0f, 8000.0f, 'R' }, { st::Registration, 0.0f, 5.0f, 'R' } } },
            { "Wide Ensemble", 0, { "Panorama", "Vista", "Expanse", "Horizon", "Prairie", "Steppe", "Savanna", "Tundra" },
              { { st::EnsembleType, 2.0f, 2.0f, 'R' }, { st::Registration, 0.0f, 5.0f, 'A' }, { st::Ensemble, 0.7f, 1.0f, 'B' } } },
            { "Chorus Strings", 1, { "Chorus", "Unison", "Harmony", "Accord", "Concord", "Consonance", "Resonance", "Euphony" },
              { { st::EnsembleType, 1.0f, 1.0f, 'R' }, { st::Registration, 0.0f, 5.0f, 'A' }, { st::Ensemble, 0.6f, 1.0f, 'B' } } },
            { "Morphing Registers", 2, { "Metamorphosis", "Chrysalis", "Transfiguration", "Shapeshifter", "Mutation", "Evolution", "Transit", "Passage" },
              { { st::Animate, 0.8f, 1.0f, 'A' }, { st::AnimateRate, 0.1f, 0.5f, 'B' }, { st::Registration, 1.0f, 6.0f, 'R' } } },
            { "Dark Solina", 3, { "Nightshade", "Belladonna", "Mandrake", "Hemlock", "Wolfsbane", "Foxglove", "Henbane", "Aconite" },
              { { st::Tone, 1000.0f, 2200.0f, 'A' }, { st::Registration, 0.0f, 4.0f, 'B' }, { st::Ensemble, 0.8f, 1.0f, 'R' } } },
        } };
    return s;
}

// --- The drums --------------------------------------------------------------------------------------------------------------
namespace dr = drums;
/** @brief The drums' names and groups. */
const Synth& drumsSynth()
{
    static const Synth s{
        { { "Muffled", "Soft", "Warm", "Round", "Firm", "Punchy", "Snappy", "Crisp" },
          { "Distant", "Dusty", "Vintage", "Analog", "Tight", "Modern", "Polished", "Sharp" },
          { "Heavy", "Deep", "Low", "Dense", "Solid", "Lean", "Light", "Brisk" },
          { "Rusty", "Leaden", "Iron", "Steel", "Chrome", "Titanium", "Platinum", "Diamond" } },
        {
            { "Tight Kit", 0, { "Clasp", "Buckle", "Latch", "Clamp", "Bolt", "Lock", "Knot", "Stitch" },
              { { dr::KickHz, 50.0f, 60.0f, 'A' }, { dr::Decay, 0.3f, 0.6f, 'B' }, { dr::Tone, 0.4f, 0.7f, 'R' } } },
            { "Deep Kit", 1, { "Barrel", "Cask", "Keg", "Vat", "Cistern", "Silo", "Tank", "Hopper" },
              { { dr::KickHz, 38.0f, 46.0f, 'A' }, { dr::Decay, 0.9f, 1.6f, 'B' }, { dr::Tone, 0.2f, 0.5f, 'R' } } },
            { "Boom Kit", 2, { "Cannon", "Mortar", "Salvo", "Volley", "Broadside", "Thunderclap", "Detonation", "Blast" },
              { { dr::KickHz, 40.0f, 50.0f, 'R' }, { dr::Decay, 1.4f, 2.0f, 'A' }, { dr::Tone, 0.2f, 0.4f, 'B' } } },
            { "Dry Machine", 3, { "Robot", "Android", "Automaton", "Cyborg", "Mechanism", "Contraption", "Gadget", "Device" },
              { { dr::KickHz, 48.0f, 58.0f, 'R' }, { dr::Decay, 0.3f, 0.5f, 'A' }, { dr::Tone, 0.5f, 0.8f, 'B' } } },
            { "Click Kit", 0, { "Switch", "Toggle", "Button", "Lever", "Trigger", "Latchkey", "Keystroke", "Tick" },
              { { dr::KickHz, 55.0f, 70.0f, 'A' }, { dr::Decay, 0.3f, 0.5f, 'R' }, { dr::Tone, 0.7f, 1.0f, 'B' } } },
            { "Warm Analog", 1, { "Valve", "Tube", "Transistor", "Capacitor", "Resistor", "Diode", "Oscillator", "Rectifier" },
              { { dr::KickHz, 44.0f, 52.0f, 'A' }, { dr::Decay, 0.7f, 1.1f, 'B' }, { dr::Tone, 0.3f, 0.5f, 'R' } } },
            { "Bright Machine", 2, { "Photon", "Proton", "Electron", "Neutron", "Positron", "Muon", "Quark", "Gluon" },
              { { dr::KickHz, 50.0f, 62.0f, 'R' }, { dr::Decay, 0.5f, 0.9f, 'A' }, { dr::Tone, 0.7f, 0.95f, 'B' } } },
            { "Dark Machine", 3, { "Bunker", "Blockhouse", "Hangar", "Depot", "Warehouse", "Foundry", "Mill", "Refinery" },
              { { dr::KickHz, 40.0f, 50.0f, 'R' }, { dr::Decay, 0.6f, 1.2f, 'A' }, { dr::Tone, 0.05f, 0.3f, 'B' } } },
            { "Round Kit", 0, { "Marble", "Bead", "Pearl", "Orb", "Sphere", "Globe", "Ball", "Bubble" },
              { { dr::KickHz, 45.0f, 55.0f, 'A' }, { dr::Decay, 0.8f, 1.3f, 'R' }, { dr::Tone, 0.4f, 0.6f, 'B' } } },
            { "Punchy Kit", 1, { "Fist", "Knuckle", "Jab", "Hook", "Uppercut", "Punch", "Strike", "Blow" },
              { { dr::KickHz, 52.0f, 64.0f, 'A' }, { dr::Decay, 0.5f, 0.8f, 'B' }, { dr::Tone, 0.55f, 0.8f, 'R' } } },
            { "Sub Kit", 2, { "Aftershock", "Epicenter", "Fault", "Upheaval", "Subduction", "Seism", "Landslide", "Avalanche" },
              { { dr::KickHz, 35.0f, 42.0f, 'A' }, { dr::Decay, 1.0f, 1.8f, 'B' }, { dr::Tone, 0.1f, 0.3f, 'R' } } },
            { "Snappy Kit", 3, { "Whip", "Crack", "Snap", "Lash", "Flick", "Twig", "Branch", "Kindling" },
              { { dr::KickHz, 55.0f, 68.0f, 'R' }, { dr::Decay, 0.35f, 0.6f, 'A' }, { dr::Tone, 0.6f, 0.9f, 'B' } } },
            { "Soft Kit", 0, { "Pillow", "Cushion", "Felt", "Wool", "Velour", "Suede", "Moss", "Feather" },
              { { dr::KickHz, 44.0f, 54.0f, 'R' }, { dr::Decay, 0.6f, 1.0f, 'A' }, { dr::Tone, 0.1f, 0.35f, 'B' } } },
            { "Rock Kit", 1, { "Stadium", "Arena", "Garage", "Studio", "Stage", "Amphitheatre", "Coliseum", "Hall" },
              { { dr::KickHz, 50.0f, 62.0f, 'A' }, { dr::Decay, 0.8f, 1.2f, 'B' }, { dr::Tone, 0.5f, 0.75f, 'R' } } },
            { "Tribal Kit", 2, { "Ritual", "Rite", "Totem", "Bonfire", "Trance", "Dance", "Gathering", "Tribe" },
              { { dr::KickHz, 40.0f, 55.0f, 'A' }, { dr::Decay, 1.0f, 1.6f, 'B' }, { dr::Tone, 0.3f, 0.6f, 'R' } } },
            { "Metal Kit", 3, { "Scrap", "Chain", "Sheet", "Plate", "Pipe", "Rail", "Spring", "Grate" },
              { { dr::KickHz, 58.0f, 80.0f, 'A' }, { dr::Decay, 0.4f, 0.9f, 'B' }, { dr::Tone, 0.8f, 1.0f, 'R' } } },
        } };
    return s;
}

// --- The atmosphere: its colours; the amounts are the composer's ---------------------------------------------------------------
namespace at = atmos;
/** @brief The atmosphere's names and groups: its colours (the amounts are the composer's). */
const Synth& atmosSynth()
{
    static const Synth s{
        { { "Silent", "Faint", "Quiet", "Soft", "Stirring", "Restless", "Rushing", "Howling" },
          { "Frozen", "Cold", "Cool", "Mild", "Warm", "Hot", "Searing", "Molten" },
          { "Dim", "Dusky", "Grey", "Pale", "Clear", "Bright", "Glaring", "Blinding" },
          { "Far", "Remote", "Outer", "Upper", "Lower", "Inner", "Near", "Close" } },
        {
            { "Polar Wind", 0, { "Icefield", "Permafrost", "Snowdrift", "Blizzard", "Whiteout", "Icecap", "Hoarfrost", "Frostline" },
              { { at::WindTone, 150.0f, 450.0f, 'A' }, { at::SweepLevel, -30.0f, -20.0f, 'B' }, { at::BleepLevel, -35.0f, -28.0f, 'R' }, { at::GrainDensity, 3.0f, 10.0f, 'R' } } },
            { "Desert Wind", 1, { "Dune", "Mesa", "Sirocco", "Mirage", "Oasis", "Sandstorm", "Erg", "Wadi" },
              { { at::WindTone, 800.0f, 2000.0f, 'A' }, { at::SweepLevel, -24.0f, -14.0f, 'B' }, { at::GrainDensity, 6.0f, 16.0f, 'R' } } },
            { "Solar Wind", 2, { "Corona", "Prominence", "Sunspot", "Photosphere", "Chromosphere", "Heliosphere", "Sunwind", "Solarflare" },
              { { at::WindTone, 1500.0f, 4000.0f, 'A' }, { at::BleepLevel, -26.0f, -16.0f, 'B' }, { at::SweepLevel, -24.0f, -16.0f, 'R' } } },
            { "Deep Space", 3, { "Nebula", "Cosmos", "Galaxy", "Quasar", "Pulsar", "Magnetar", "Blackhole", "Wormhole" },
              { { at::WindTone, 150.0f, 400.0f, 'A' }, { at::SweepLevel, -30.0f, -22.0f, 'R' }, { at::GrainDensity, 2.0f, 6.0f, 'B' } } },
            { "Radio Static", 0, { "Static", "Shortwave", "Longwave", "Crackle", "Interference", "Frequency", "Wavelength", "Carrier" },
              { { at::WindTone, 1000.0f, 3000.0f, 'R' }, { at::BleepLevel, -20.0f, -12.0f, 'A' }, { at::GrainDensity, 20.0f, 40.0f, 'B' } } },
            { "Night Sky", 1, { "Constellation", "Firmament", "Milkyway", "Zodiac", "Heavens", "Skyline", "Canopy", "Dome" },
              { { at::WindTone, 300.0f, 800.0f, 'A' }, { at::GrainDensity, 4.0f, 12.0f, 'B' }, { at::BleepLevel, -30.0f, -22.0f, 'R' } } },
            { "Starfield", 2, { "Stardust", "Starlight", "Starburst", "Stargazer", "Starmap", "Starfall", "Starseed", "Starshine" },
              { { at::GrainDensity, 15.0f, 40.0f, 'A' }, { at::WindTone, 600.0f, 1500.0f, 'B' }, { at::BleepLevel, -26.0f, -18.0f, 'R' } } },
            { "Ion Storm", 3, { "Ionosphere", "Plasma", "Lightning", "Thunderhead", "Squallline", "Stormfront", "Downburst", "Supercell" },
              { { at::SweepLevel, -14.0f, -6.0f, 'A' }, { at::WindTone, 800.0f, 2500.0f, 'B' } } },
            { "Satellite Chatter", 0, { "Telemetry", "Uplink", "Downlink", "Transponder", "Orbiter", "Probe", "Lander", "Relay" },
              { { at::BleepLevel, -18.0f, -10.0f, 'A' }, { at::GrainDensity, 8.0f, 20.0f, 'B' } } },
            { "Ocean Floor", 1, { "Seabed", "Reef", "Kelpforest", "Seagrass", "Lagoon", "Atoll", "Shoal", "Tidepool" },
              { { at::WindTone, 150.0f, 350.0f, 'A' }, { at::SweepLevel, -26.0f, -18.0f, 'B' }, { at::GrainDensity, 1.0f, 4.0f, 'R' } } },
            { "Aurora", 2, { "Borealis", "Australis", "Northlights", "Skyfire", "Curtains", "Streamers", "Coronae", "Glowbands" },
              { { at::WindTone, 500.0f, 1500.0f, 'A' }, { at::SweepLevel, -20.0f, -10.0f, 'B' }, { at::GrainDensity, 6.0f, 15.0f, 'R' } } },
            { "Dust Cloud", 3, { "Dust", "Pollen", "Spores", "Ash", "Soot", "Powder", "Haze", "Smoke" },
              { { at::GrainDensity, 25.0f, 40.0f, 'A' }, { at::WindTone, 400.0f, 1000.0f, 'B' } } },
            { "Cathedral Air", 0, { "Nave", "Cloister", "Crypt", "Belltower", "Rafters", "Vaulting", "Buttress", "Spire" },
              { { at::WindTone, 250.0f, 600.0f, 'A' }, { at::BleepLevel, -34.0f, -26.0f, 'R' }, { at::SweepLevel, -28.0f, -18.0f, 'B' } } },
            { "Signal Drift", 1, { "Signal", "Transmission", "Broadcast", "Channel", "Band", "Waveband", "Numbers", "Station" },
              { { at::BleepLevel, -22.0f, -12.0f, 'A' }, { at::WindTone, 400.0f, 1200.0f, 'B' }, { at::GrainDensity, 4.0f, 10.0f, 'R' } } },
            { "Magnetic Field", 2, { "Magnet", "Lodestone", "Compass", "Fluxline", "Dipole", "Magnetosphere", "Northpole", "Fieldline" },
              { { at::SweepLevel, -18.0f, -8.0f, 'A' }, { at::GrainDensity, 10.0f, 25.0f, 'B' } } },
            { "Ether", 3, { "Ether", "Aether", "Quintessence", "Pneuma", "Anima", "Essence", "Vapor", "Mist" },
              { { at::WindTone, 200.0f, 600.0f, 'A' }, { at::GrainDensity, 1.0f, 6.0f, 'B' }, { at::SweepLevel, -30.0f, -20.0f, 'R' }, { at::BleepLevel, -36.0f, -28.0f, 'R' } } },
        } };
    return s;
}

// --- The pad synth: analog pads on the PWM table, the Oberheim's brass, the PPG's and Waldorf's planes -----------------
namespace po = poly;
/** @brief The index of a wavetable by name (Wavetable.h), as a knob value. */
float tableOf(const char* name)
{
    for (int i = 0; i < kWavetableCount; ++i) if (std::string(kWavetableNames[i]) == name) return static_cast<float>(i);
    return 1.0f;
}
/** @brief The pad synth's names and groups. */
const Synth& polySynth()
{
    // Every group: a slow envelope and the ensemble unless it says otherwise; the detune within 14 cents.
    auto base = [](const char* first, const char* last, std::vector<Axis> a) {
        a.insert(a.begin(), { po::Table, tableOf(first), tableOf(last), 'R' });
        auto has = [&](int k) { for (const Axis& x : a) if (x.k == k) return true; return false; };
        if (!has(po::Attack)) a.push_back({ po::Attack, 0.8f, 2.5f, 'R' });
        if (!has(po::Release)) a.push_back({ po::Release, 2.5f, 6.0f, 'R' });
        if (!has(po::Chorus)) a.push_back({ po::Chorus, 0.3f, 0.7f, 'R' });
        if (!has(po::Detune)) a.push_back({ po::Detune, 4.0f, 12.0f, 'R' });
        if (!has(po::Scan)) a.push_back({ po::Scan, 0.2f, 0.6f, 'R' });
        if (!has(po::ScanRate)) a.push_back({ po::ScanRate, 0.02f, 0.1f, 'R' });
        return a;
    };
    static const Synth s{
        { { "Umbral", "Dusky", "Velvet", "Hazy", "Amber", "Golden", "Silver", "Radiant" },
          { "Nocturnal", "Shadowed", "Misty", "Soft", "Warm", "Luminous", "Crystal", "Solar" },
          { "Deep", "Sombre", "Muted", "Mellow", "Clear", "Bright", "Gleaming", "Blazing" },
          { "Abyssal", "Distant", "Veiled", "Drifting", "Floating", "Shining", "Starlit", "Celestial" } },
        {
            { "Analog Pad", 0, { "Horizon", "Plateau", "Tundra", "Steppe", "Prairie", "Savanna", "Mesa", "Delta" },
              base("PWM", "PWM", { { po::Position, 0.05f, 0.5f, 'B' }, { po::Scan, 0.3f, 0.7f, 'R' }, { po::Cutoff, 700.0f, 4000.0f, 'A' },
                                   { po::Resonance, 0.05f, 0.25f, 'R' }, { po::EnvAmount, 0.3f, 1.2f, 'R' }, { po::Spread, 0.4f, 0.8f, 'R' } }) },
            { "Juno Strings", 1, { "Ribbon", "Silk", "Satin", "Gossamer", "Chiffon", "Velour", "Tulle", "Organza" },
              base("PWM", "PWM", { { po::Position, 0.2f, 0.6f, 'R' }, { po::Scan, 0.4f, 0.8f, 'R' }, { po::ScanRate, 0.3f, 0.6f, 'R' },
                                   { po::Cutoff, 1500.0f, 7000.0f, 'A' }, { po::Attack, 0.2f, 1.0f, 'B' }, { po::Release, 1.5f, 4.0f, 'R' },
                                   { po::Chorus, 0.7f, 1.0f, 'R' }, { po::Detune, 4.0f, 10.0f, 'R' } }) },
            { "Oberheim Brass", 2, { "Fanfare", "Herald", "Clarion", "Bugle", "Signal", "Summons", "Tattoo", "Reveille" },
              base("Classic", "Classic", { { po::Position, 0.45f, 0.55f, 'R' }, { po::Scan, 0.0f, 0.15f, 'R' }, { po::Cutoff, 400.0f, 2500.0f, 'A' },
                                           { po::Resonance, 0.1f, 0.3f, 'R' }, { po::EnvAmount, 1.5f, 3.0f, 'B' }, { po::Attack, 0.15f, 0.8f, 'R' },
                                           { po::Release, 1.0f, 3.0f, 'R' }, { po::Chorus, 0.2f, 0.5f, 'R' }, { po::Detune, 5.0f, 12.0f, 'R' } }) },
            { "Sync Sweep", 3, { "Comet", "Meteor", "Bolide", "Streak", "Tracer", "Flare", "Arc", "Trail" },
              base("Sync", "Sync", { { po::Position, 0.0f, 0.4f, 'B' }, { po::Scan, 0.3f, 0.8f, 'R' }, { po::Cutoff, 1500.0f, 8000.0f, 'A' },
                                     { po::Resonance, 0.1f, 0.3f, 'R' }, { po::Detune, 3.0f, 8.0f, 'R' } }) },
            { "Formant Pad", 0, { "Throat", "Larynx", "Palate", "Vowel", "Chant", "Mantra", "Intonation", "Cantor" },
              base("Formant", "Formant", { { po::Position, 0.1f, 0.7f, 'B' }, { po::Scan, 0.4f, 0.9f, 'R' }, { po::Cutoff, 1500.0f, 6000.0f, 'A' },
                                           { po::Resonance, 0.05f, 0.2f, 'R' } }) },
            { "Vocal Pad", 1, { "Chorale", "Anthem", "Canticle", "Motet", "Hymn", "Madrigal", "Oratorio", "Cantata" },
              base("Alto", "Basso", { { po::Position, 0.2f, 0.8f, 'B' }, { po::Cutoff, 1200.0f, 5000.0f, 'A' }, { po::Attack, 1.0f, 3.0f, 'R' },
                                      { po::Chorus, 0.4f, 0.7f, 'R' } }) },
            { "Glass Pad", 2, { "Prism", "Lens", "Crystal", "Quartz", "Facet", "Mirror", "Pane", "Icicle" },
              base("Glass", "Glass", { { po::Position, 0.3f, 1.0f, 'B' }, { po::Cutoff, 3000.0f, 10000.0f, 'A' }, { po::Attack, 0.5f, 2.0f, 'R' },
                                       { po::Release, 3.0f, 7.0f, 'R' } }) },
            { "Single Cycle Pad", 3, { "Relic", "Cameo", "Locket", "Token", "Charm", "Amulet", "Talisman", "Keepsake" },
              base("FM Synth", "Theremin", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 1000.0f, 5000.0f, 'A' }, { po::Resonance, 0.05f, 0.2f, 'R' } }) },
            { "Bowed Pad", 0, { "Bow", "Rosin", "Gut", "Bridge", "Scroll", "Fingerboard", "Soundpost", "Purfling" },
              base("Bowed 1", "Bowed 3", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 800.0f, 4000.0f, 'A' }, { po::Attack, 1.0f, 3.0f, 'R' },
                                           { po::Release, 3.0f, 7.0f, 'R' } }) },
            { "Tube Pad", 1, { "Valve", "Filament", "Cathode", "Anode", "Glow", "Triode", "Pentode", "Heater" },
              base("Tube 1", "Tube 3", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 600.0f, 3500.0f, 'A' }, { po::Resonance, 0.05f, 0.25f, 'R' } }) },
            { "PPG Choir", 2, { "Wave", "Table", "Palette", "Spectrum", "Index", "Slot", "Bank", "Matrix" },
              base("PPG 00", "PPG 25", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 1500.0f, 7000.0f, 'A' }, { po::Resonance, 0.1f, 0.35f, 'R' },
                                         { po::Chorus, 0.2f, 0.5f, 'R' }, { po::Attack, 0.5f, 2.0f, 'R' }, { po::Release, 2.0f, 5.0f, 'R' } }) },
            { "PPG Upper", 3, { "Cirrus", "Stratus", "Nimbus", "Cumulus", "Altostratus", "Contrail", "Halo", "Corona" },
              base("PPG 22", "PPG Upper", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 3000.0f, 10000.0f, 'A' }, { po::Chorus, 0.3f, 0.6f, 'R' } }) },
            { "Overtone Pad", 0, { "Partial", "Harmonic", "Series", "Octave", "Twelfth", "Fifteenth", "Seventeenth", "Nineteenth" },
              base("Overtones 1", "Overtones 3", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 1500.0f, 8000.0f, 'A' } }) },
            { "Morph Pad", 1, { "Chimera", "Hybrid", "Mutation", "Metamorph", "Shapeshift", "Transit", "Alloy", "Amalgam" },
              base("Consonant 1", "Morph 3", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Scan, 0.4f, 0.9f, 'R' }, { po::Cutoff, 1000.0f, 6000.0f, 'A' } }) },
            { "Sampled Air", 2, { "Breeze", "Draught", "Gust", "Zephyr", "Current", "Updraft", "Thermal", "Jetstream" },
              base("Sampled 1", "Sampled 3", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 1000.0f, 6000.0f, 'A' }, { po::Attack, 1.5f, 4.0f, 'R' } }) },
            { "Dark Drone Pad", 3, { "Undercroft", "Catacomb", "Ossuary", "Vault", "Barrow", "Tomb", "Sepulchre", "Mausoleum" },
              base("Drone Bank", "Vox Synth", { { po::Position, 0.0f, 1.0f, 'B' }, { po::Cutoff, 300.0f, 1500.0f, 'A' }, { po::Attack, 2.0f, 6.0f, 'R' },
                                               { po::Release, 4.0f, 9.0f, 'R' }, { po::Resonance, 0.1f, 0.3f, 'R' } }) },
        } };
    return s;
}

/** @brief The names and groups of module @p m, null for a module without presets. */
const Synth* synthOf(Module m)
{
    switch (m) {
    case Module::Voice: return &voiceSynth();
    case Module::Lead: return &leadSynth();
    case Module::Drone: return &droneSynth();
    case Module::Tape: return &tapeSynth();
    case Module::Strings: return &stringsSynth();
    case Module::Drums: return &drumsSynth();
    case Module::Atmos: return &atmosSynth();
    case Module::Poly: return &polySynth();
    default: return nullptr;
    }
}

// --- The filters of the groups (26.09.2026, Filters.h) -------------------------------------------------------------
/** @brief A filter a group's presets may take: the model, the range of its mode, and how much filter FM. */
struct FilterChoice {
    int model;      ///< the filter model (Filters.h)
    float modeLo;   ///< the low end of its mode's range
    float modeHi;   ///< ... the high end
    float fm;       ///< how much filter FM
};

/**
 * @brief The filters a group draws from, by the instruments its sounds come from: the basses on the Moog ladder (now
 *        and then the diode ladder), the squelch on the diode ladder and the Korg35, glass on the Xpander's high and
 *        band passes, the Juno's strings on the IR3109, the Oberheim's brass on the SEM, the dark and dusty ones on the
 *        Polivoks and the Wasp, the cosmic drips on the comb and the phaser; empty for a synth without a filter model.
 */
const std::vector<FilterChoice>& filterChoices(Module m, const std::string& group)
{
    enum { MOOG, PROPHET, JUNO, SEM, XPANDER, DIODE, KORG, POLIVOKS, WASP, COMB };
    const float BP2 = 2.0f / 7.0f, BP4 = 3.0f / 7.0f, HP2 = 4.0f / 7.0f, NOTCH = 6.0f / 7.0f, PHASER = 1.0f;
    using L = std::vector<FilterChoice>;
    static const std::vector<std::pair<std::string, L>> voice = {
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
    static const std::vector<std::pair<std::string, L>> lead = {
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
    static const std::vector<std::pair<std::string, L>> drone = {
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
    static const std::vector<std::pair<std::string, L>> poly = {
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
    static const L none;
    const std::vector<std::pair<std::string, L>>* table = nullptr;
    switch (m) {
    case Module::Voice: table = &voice; break;
    case Module::Lead: table = &lead; break;
    case Module::Drone: table = &drone; break;
    case Module::Poly: table = &poly; break;
    default: return none;
    }
    for (const auto& e : *table) if (e.first == group) return e.second;
    return none;
}

// --- The modulation of the groups (26.09.2026, Modulation.h) ---------------------------------------------------------
/** @brief A knob a modulation sets, by key, drawn from its range. In an atom of modulation "lfo@_..." is the LFO the
 *         atom is given, "mod#_..." its slot of the matrix, and a source of -1 that LFO. */
struct KnobRange {
    const char* key;   ///< the knob's key ("lfo@_rate", "mod#_amount" ...)
    float lo;          ///< the low end of its range
    float hi;          ///< ... the high end
};
/** @brief A modulation a group's presets may carry: how often, and its knobs. */
struct ModRecipe {
    float chance;                  ///< how often a preset of the group carries it, 0..1
    std::vector<KnobRange> knobs;  ///< the knobs it sets
};
/** @brief One modulation of a synth's pool; @p pitch: it moves the pitch (never on the foundation, the bass and the drone). */
struct ModAtom {
    bool pitch;                    ///< it moves the pitch
    std::vector<KnobRange> knobs;  ///< the knobs it sets
};

/**
 * @brief The modulation that makes a group what it is -- after the instruments and players its sounds come from: plucks
 *        and stabs that decay under a held gate, the pulse width moving on the hollow pulses and the humming drones,
 *        the sequencer's filter sweep synced to the bars, sample-and-hold drips, tape wobble, a vibrato that comes in
 *        late, the PPG's and the morphing pads' tables walked by a slow LFO, the brass's filter swell. Sources and
 *        targets by number (kModSourceNames, kModDestNames; the tape keys and the strings: kShortModDestNames).
 */
const std::vector<ModRecipe>& modRecipes(Module m, const std::string& group)
{
    using R = std::vector<ModRecipe>;
    // Sources: the atom's LFO -1, the mod envelope 5, the filter envelope 6, velocity 7, the modulation lane 8. Targets:
    // pitch 1, pulse width 2, blend 3, table 4, cutoff 5, resonance 6, filter mode 7, filter FM 8, level 9, pan 10 (the
    // smaller matrix: pitch 1, tone 2, level 3, pan 4).
    static const std::vector<std::pair<std::string, R>> voice = {
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
    static const std::vector<std::pair<std::string, R>> lead = {
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
    static const std::vector<std::pair<std::string, R>> drone = {
        { "Pulse Hum", { { 0.9f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.15f, 0.3f } } } } },
        { "Resonant Earth", { { 0.8f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.02f, 0.08f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Iron Drone", { { 0.7f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.1f, 0.3f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.1f } } } } },
        { "Deep Current", { { 0.8f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Breathing Root", { { 0.9f, { { "lfo@_rate", 0.05f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Temple Hum", { { 0.8f, { { "lfo@_rate", 0.03f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Organ Pedal", { { 0.6f, { { "lfo@_rate", 0.03f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.2f } } } } },
        { "Hollow Pipe", { { 0.7f, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.04f, 0.12f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.25f } } } } },
    };
    static const std::vector<std::pair<std::string, R>> poly = {
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
    static const std::vector<std::pair<std::string, R>> tape = {
        { "Cathedral Choir", { { 0.6f, { { "amp_attack", 200.0f, 800.0f } } } } },
        { "Ghost Choir", { { 0.8f, { { "lfo@_rate", 0.05f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Silk Strings", { { 0.6f, { { "amp_attack", 100.0f, 400.0f } } } } },
        { "Warped Strings", { { 0.8f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.5f, 1.5f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } } } },
        { "Breathy Flute", { { 0.8f, { { "lfo@_rate", 4.0f, 5.5f }, { "lfo@_fade", 0.3f, 0.8f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Wobbly Flute", { { 0.8f, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.5f, 1.5f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } } } },
    };
    static const std::vector<std::pair<std::string, R>> strings = {
        { "Violin Section", { { 0.8f, { { "lfo@_rate", 5.0f, 6.0f }, { "lfo@_fade", 0.5f, 1.0f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.05f, 0.08f } } } } },
        { "Brass Machine", { { 0.8f, { { "amp_sustain", 0.6f, 0.8f }, { "amp_decay", 0.5f, 1.5f } } } } },
        { "Phased Strings", { { 0.8f, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 2, 2 }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.08f, 0.15f } } } } },
        { "Short Bow", { { 0.85f, { { "amp_sustain", 0.3f, 0.6f }, { "amp_decay", 0.3f, 1.0f } } } } },
        { "Slow Swell", { { 0.6f, { { "lfo@_rate", 0.03f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.08f, 0.15f } } } } },
    };
    static const R none;
    const std::vector<std::pair<std::string, R>>* table = nullptr;
    switch (m) {
    case Module::Voice: table = &voice; break;
    case Module::Lead: table = &lead; break;
    case Module::Drone: table = &drone; break;
    case Module::Poly: table = &poly; break;
    case Module::Tape: table = &tape; break;
    case Module::Strings: table = &strings; break;
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
 */
const std::vector<ModAtom>& modPool(Module m)
{
    using A = std::vector<ModAtom>;
    static const A voices = {
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
    static const A lead = {
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
    static const A drone = {
        { false, { { "lfo@_shape", 0, 1 }, { "lfo@_rate", 0.02f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.15f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_sync", 1, 3 }, { "mod#_src", -1, -1 }, { "mod#_dst", 5, 5 }, { "mod#_amt", 0.05f, 0.12f } } },
        { false, { { "wave", 0.4f, 0.8f }, { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.03f, 0.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.1f, 0.3f } } },
        { false, { { "lfo@_rate", 0.02f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 10, 10 }, { "mod#_amt", 0.1f, 0.3f } } },
        { false, { { "lfo@_shape", 1, 1 }, { "lfo@_rate", 0.02f, 0.1f }, { "mod#_src", -1, -1 }, { "mod#_dst", 7, 7 }, { "mod#_amt", 0.1f, 0.25f } } },
        { false, { { "lfo@_rate", 0.03f, 0.12f }, { "mod#_src", -1, -1 }, { "mod#_dst", 6, 6 }, { "mod#_amt", 0.06f, 0.15f } } },
        { false, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.02f, 0.08f }, { "mod#_src", -1, -1 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.15f, 0.35f } } },
    };
    static const A pads = {
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
    static const A small = {   // the tape keys and the strings: pitch 1, tone 2, level 3, pan 4
        { true, { { "lfo@_shape", 6, 6 }, { "lfo@_rate", 0.3f, 1.2f }, { "mod#_src", -1, -1 }, { "mod#_dst", 1, 1 }, { "mod#_amt", 0.04f, 0.07f } } },
        { false, { { "lfo@_rate", 0.03f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 2, 2 }, { "mod#_amt", 0.05f, 0.12f } } },
        { false, { { "lfo@_rate", 4.0f, 6.0f }, { "lfo@_fade", 0.5f, 1.5f }, { "lfo@_retrig", 1, 1 }, { "mod#_src", -1, -1 }, { "mod#_dst", 3, 3 }, { "mod#_amt", 0.06f, 0.12f } } },
        { false, { { "lfo@_rate", 0.03f, 0.15f }, { "mod#_src", -1, -1 }, { "mod#_dst", 4, 4 }, { "mod#_amt", 0.2f, 0.5f } } },
    };
    static const A none;
    switch (m) {
    case Module::Voice: return voices;
    case Module::Lead: return lead;
    case Module::Drone: return drone;
    case Module::Poly: return pads;
    case Module::Tape: case Module::Strings: return small;
    default: return none;
    }
}

/** @brief The groups whose pitch is the foundation (the bass groups the composer draws the bass from; the drone throughout):
 *         no pitch modulation. */
bool foundation(Module m, const std::string& group)
{
    return m == Module::Drone || (m == Module::Voice && (group == "Ladder Bass" || group == "Deep Ostinato" || group == "Dark Throb" || group == "Warm Unison"));
}

// --- The oscillators of the groups (26.09.2026, Vco.h) ---------------------------------------------------------------
/** @brief A VCO a group's presets may take: the model, hard sync (VCO 2's interval and the filter envelope's sweep of
 *         it, the Prophet's Poly-Mod), and cross mod. */
struct VcoChoice {
    int model;      ///< the VCO model (Vco.h)
    bool sync;      ///< hard sync
    float osc2Lo;   ///< VCO 2's interval: the low end of its range, semitones
    float osc2Hi;   ///< ... the high end
    float sweep;    ///< the filter envelope's sweep of it (the Prophet's Poly-Mod)
    float cross;    ///< cross mod
};

/**
 * @brief The VCOs a group draws from, by the instruments its sounds come from: the basses on the Moog (the sub basses
 *        on the steady E-mu), the sync sweeps on the Prophet, the round pulses and pads on the SEM, the plucks and the
 *        squelch on the ARP, glass and bells on the E-mu (with cross mod now and then) -- and still the voices' own
 *        analog oscillators here and there.
 */
const std::vector<VcoChoice>& vcoChoices(Module m, const std::string& group)
{
    enum { ANALOG, MOOG, PROPHET, SEM, ARP, EMU };
    using L = std::vector<VcoChoice>;
    const VcoChoice moog { MOOG, false, 0, 0, 0, 0 }, prophet { PROPHET, false, 0, 0, 0, 0 }, sem { SEM, false, 0, 0, 0, 0 };
    const VcoChoice arp { ARP, false, 0, 0, 0, 0 }, emu { EMU, false, 0, 0, 0, 0 }, analog { ANALOG, false, 0, 0, 0, 0 };
    const VcoChoice sweep { PROPHET, true, 5.0f, 19.0f, 0.4f, 0 }, emuX { EMU, false, 0, 0, 0, 0.3f }, arpX { ARP, false, 0, 0, 0, 0.3f };
    static const std::vector<std::pair<std::string, L>> voice = {
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
    static const std::vector<std::pair<std::string, L>> lead = {
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
    static const std::vector<std::pair<std::string, L>> drone = {
        { "Dark Bordun", { moog } }, { "Organ Pedal", { emu } }, { "Pulse Hum", { sem, arp } }, { "Resonant Earth", { moog } },
        { "Glowing Root", { sem } }, { "Hollow Pipe", { arp } }, { "Warm Floor", { moog, sem } }, { "Filtered Void", { emu } },
        { "Singing Ground", { sem } }, { "Iron Drone", { arp, moog } }, { "Soft Monolith", { sem } }, { "Bright Axis", { prophet } },
        { "Deep Current", { emu } }, { "Breathing Root", { moog } }, { "Temple Hum", { arp } }, { "Night Floor", { moog } },
    };
    static const L none;
    const std::vector<std::pair<std::string, L>>* table = nullptr;
    switch (m) {
    case Module::Voice: table = &voice; break;
    case Module::Lead: table = &lead; break;
    case Module::Drone: table = &drone; break;
    default: return none;
    }
    for (const auto& e : *table) if (e.first == group) return e.second;
    return none;
}

/** @brief The pad synth's analog groups on the VCO models' waves (Wavetable.h): a table and the range of its place. */
struct PolyTable {
    const char* table;   ///< the table's name
    float posLo;         ///< the low end of its place's range
    float posHi;         ///< ... the high end
};
/** @brief The tables and places the pad synth's analog group @p group draws from (empty for the others). */
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

/** @brief The sixty-four presets of every group of a synth, the knob values from the axes (Presets.h). */
std::vector<SoundPreset> build(Module m, const Synth& s)
{
    ParamStore store;
    std::vector<SoundPreset> out;
    out.reserve(s.groups.size() * 64);
    for (size_t g = 0; g < s.groups.size(); ++g) {
        const Group& grp = s.groups[g];
        for (int v = 0; v < 64; ++v) {
            const int a = v / 8, n = v % 8;
            SoundPreset p;
            p.group = grp.name;
            p.name = std::string(s.adjectives[grp.adjectives][a]) + " " + grp.nouns[n];
            Rng rng;
            rng.seed(mixSeed(0x5E7u + 131u * static_cast<uint64_t>(m) + 17u * g, static_cast<uint64_t>(v)));
            for (const Axis& ax : grp.axes) {
                // Where on the knob's range: the adjective's row or the noun's column (with a little of its own), or a draw.
                float t = ax.axis == 'A' ? a / 7.0f : (ax.axis == 'B' ? n / 7.0f : rng.uniform());
                if (ax.axis != 'R') t = std::clamp(t + 0.12f * (rng.uniform() - 0.5f), 0.0f, 1.0f);
                const ParamDesc& d = store.desc(store.id(m, 0, ax.k));
                float value;
                if (d.curve == Curve::Log && ax.lo > 0.0f) value = ax.lo * std::pow(ax.hi / ax.lo, t);
                else value = ax.lo + t * (ax.hi - ax.lo);
                if (d.curve == Curve::Choice || d.curve == Curve::Int || d.curve == Curve::Toggle) value = std::round(value);
                value = std::clamp(value, d.minValue, d.maxValue);
                p.values.push_back({ ax.k, value });
            }
            // The filter (26.09.2026): drawn after the axes, so every other knob keeps the value it had.
            const std::vector<FilterChoice>& filters = filterChoices(m, grp.name);
            if (!filters.empty()) {
                const FilterChoice& fc = filters[static_cast<size_t>(rng.below(static_cast<int>(filters.size())))];
                const float mode = fc.modeLo + (fc.modeHi - fc.modeLo) * rng.uniform();
                const bool voice = m == Module::Voice, isPoly = m == Module::Poly;
                p.values.push_back({ voice ? voice::Filter : (isPoly ? poly::Filter : lead::Filter), static_cast<float>(fc.model) });
                p.values.push_back({ voice ? voice::FilterMode : (isPoly ? poly::FilterMode : lead::FilterMode), mode });
                if (!isPoly) p.values.push_back({ voice ? voice::FilterFm : lead::FilterFm, fc.fm });
                // The group's cutoffs were set for a 24 dB ladder: the 12 dB filters (SEM, Korg35, Polivoks, Wasp) are
                // brighter at the same cutoff and go down, the diode ladder (steeper, its peak at the cutoff) and the Juno
                // a little up -- so a group keeps its brightness whatever filter it drew.
                static const float kCutoffBy[kFilterModels] = { 1.0f, 1.0f, 1.05f, 0.72f, 1.0f, 1.1f, 0.8f, 0.8f, 0.8f, 1.0f };
                const int cutoff = voice ? voice::Cutoff : (isPoly ? poly::Cutoff : lead::Cutoff);
                const ParamDesc& cd = store.desc(store.id(m, 0, cutoff));
                for (auto& e : p.values)
                    if (e.first == cutoff) e.second = std::clamp(e.second * kCutoffBy[fc.model], cd.minValue, cd.maxValue);
            }
            // The modulation (26.09.2026): drawn after the filter, so the knobs above keep their values. The group's own
            // first, then one to three of the synth's pool; every atom gets the next free LFO and slot of the matrix (the
            // mod envelope one at most), and the foundation no pitch.
            {
                const int count = ParamStore::moduleCount(m);
                auto indexOf = [&](const std::string& key) {
                    for (int k = 0; k < count; ++k) if (key == store.desc(store.id(m, 0, k)).key) return k;
                    return -1;
                };
                const bool small = m == Module::Tape || m == Module::Strings;
                const int maxLfo = small ? 2 : kLfos, maxSlot = small ? 4 : kModSlots;
                int lfo = 0, slot = 0;
                bool env = false;
                auto apply = [&](const std::vector<KnobRange>& knobs) {
                    bool needLfo = false, needSlot = false, needEnv = false;
                    for (const KnobRange& kr : knobs) {
                        const std::string key = kr.key;
                        needLfo = needLfo || key.find('@') != std::string::npos;
                        needSlot = needSlot || key.find('#') != std::string::npos;
                        needEnv = needEnv || key.rfind("mod_", 0) == 0;
                    }
                    if ((needLfo && lfo >= maxLfo) || (needSlot && slot >= maxSlot) || (needEnv && env)) return false;
                    const int L = needLfo ? ++lfo : 0, S = needSlot ? ++slot : 0;
                    env = env || needEnv;
                    for (const KnobRange& kr : knobs) {
                        std::string key = kr.key;
                        if (const size_t at = key.find('@'); at != std::string::npos) key.replace(at, 1, std::to_string(L));
                        if (const size_t at = key.find('#'); at != std::string::npos) key.replace(at, 1, std::to_string(S));
                        const int k = indexOf(key);
                        if (k < 0) continue;
                        const ParamDesc& d = store.desc(store.id(m, 0, k));
                        const float t = rng.uniform();
                        float value = d.curve == Curve::Log && kr.lo > 0.0f ? kr.lo * std::pow(kr.hi / kr.lo, t) : kr.lo + t * (kr.hi - kr.lo);
                        if (kr.lo == -1.0f && kr.hi == -1.0f) value = static_cast<float>(L);   // the atom's own LFO as the source
                        if (d.curve == Curve::Choice || d.curve == Curve::Int || d.curve == Curve::Toggle) value = std::round(value);
                        p.values.push_back({ k, std::clamp(value, d.minValue, d.maxValue) });
                    }
                    return true;
                };
                const std::vector<ModRecipe>& recipes = modRecipes(m, grp.name);
                if (!recipes.empty()) {
                    const ModRecipe& r = recipes[static_cast<size_t>(rng.below(static_cast<int>(recipes.size())))];
                    if (rng.uniform() < r.chance) apply(r.knobs);
                }
                const std::vector<ModAtom>& pool = modPool(m);
                if (!pool.empty()) {
                    const bool steady = foundation(m, grp.name);
                    std::vector<bool> used(pool.size(), false);
                    static const float kMore[3] = { 0.85f, 0.55f, 0.25f };
                    for (float chance : kMore) {
                        if (rng.uniform() >= chance) break;
                        for (int tries = 0; tries < 6; ++tries) {
                            const size_t pick = static_cast<size_t>(rng.below(static_cast<int>(pool.size())));
                            if (used[pick] || (steady && pool[pick].pitch)) continue;
                            used[pick] = true;
                            apply(pool[pick].knobs);
                            break;
                        }
                    }
                }
                // The VCO (Vco.h), drawn after all of the above, which keeps its values. The sync sweep takes the next
                // free slot: the filter envelope on VCO 2's pitch.
                const std::vector<VcoChoice>& vcos = vcoChoices(m, grp.name);
                if (!vcos.empty()) {
                    const VcoChoice& vc = vcos[static_cast<size_t>(rng.below(static_cast<int>(vcos.size())))];
                    p.values.push_back({ indexOf("vco"), static_cast<float>(vc.model) });
                    if (vc.sync) {
                        p.values.push_back({ indexOf("sync"), 1.0f });
                        p.values.push_back({ indexOf("osc2_pitch"), std::round(vc.osc2Lo + (vc.osc2Hi - vc.osc2Lo) * rng.uniform()) });
                        const float amount = vc.sweep * (0.6f + 0.4f * rng.uniform());
                        if (slot < maxSlot) {
                            const std::string sweep = "mod" + std::to_string(++slot);
                            p.values.push_back({ indexOf(sweep + "_src"), 6.0f });    // the filter envelope
                            p.values.push_back({ indexOf(sweep + "_dst"), 11.0f });   // on VCO 2's pitch
                            p.values.push_back({ indexOf(sweep + "_amt"), amount });
                        }
                    }
                    if (vc.cross > 0.0f) p.values.push_back({ indexOf("cross_mod"), vc.cross * (0.5f + 0.5f * rng.uniform()) });
                }
            }
            // The pad synth's analog groups, on a VCO model's waves now and then (Wavetable.h).
            if (m == Module::Poly) {
                const std::vector<PolyTable>& tables = polyTables(grp.name);
                if (!tables.empty()) {
                    const PolyTable& pt = tables[static_cast<size_t>(rng.below(static_cast<int>(tables.size())))];
                    const float pos = pt.posLo + (pt.posHi - pt.posLo) * rng.uniform();
                    for (auto& e : p.values) {
                        if (e.first == poly::Table) e.second = tableOf(pt.table);
                        if (e.first == poly::Position) e.second = pos;
                    }
                }
            }
            out.push_back(std::move(p));
        }
    }
    return out;
}

} // namespace

const std::vector<SoundPreset>& factoryPresets(Module module)
{
    static std::mutex lock;
    static std::vector<SoundPreset> built[static_cast<int>(Module::Count)];
    static bool done[static_cast<int>(Module::Count)] = {};
    static const std::vector<SoundPreset> none;
    const Synth* s = synthOf(module);
    if (s == nullptr) return none;
    std::lock_guard<std::mutex> g(lock);
    const int i = static_cast<int>(module);
    if (!done[i]) { built[i] = build(module, *s); done[i] = true; }
    return built[i];
}

bool presetLeaves(Module module, int k)
{
    switch (module) {
    case Module::Lead: case Module::Drone:
        return k == lead::Level || k == lead::Pan || k == lead::EchoSend || k == lead::ReverbSend || k == lead::AutoPan
            || k == lead::LowCut || k == lead::Distance || k == lead::BlendSend || k == lead::EarlySend || k == lead::ShimmerSend;
    case Module::Tape:
        return k == tape::Level || k == tape::Pan || k == tape::EchoSend || k == tape::ReverbSend || k == tape::LowCut
            || k == tape::Distance || k == tape::Spread || k == tape::BlendSend || k == tape::EarlySend || k == tape::ShimmerSend;
    case Module::Strings:
        return k == strings::Level || k == strings::Pan || k == strings::EchoSend || k == strings::ReverbSend
            || k == strings::LowCut || k == strings::Distance || k == strings::BlendSend || k == strings::EarlySend
            || k == strings::ShimmerSend;
    case Module::Drums:
        return k == drums::Level || k == drums::EchoSend || k == drums::ReverbSend || k == drums::LowCut || k == drums::BlendSend || k == drums::EarlySend;
    case Module::Poly:
        return k == poly::Level || k == poly::Pan || k == poly::EchoSend || k == poly::ReverbSend || k == poly::LowCut
            || k == poly::Distance || k == poly::BlendSend || k == poly::EarlySend || k == poly::ShimmerSend;
    case Module::Atmos:
        return k == atmos::Wind || k == atmos::Sweeps || k == atmos::Bleeps || k == atmos::Grains || k == atmos::Level
            || k == atmos::EchoSend || k == atmos::ReverbSend || k == atmos::LowCut || k == atmos::ShimmerSend;
    default:
        return false;
    }
}

std::vector<std::pair<int, float>> presetKnobs(Module module, const SoundPreset& preset)
{
    ParamStore store;
    std::vector<std::pair<int, float>> out;
    const int count = ParamStore::moduleCount(module);
    for (int k = 0; k < count; ++k) {
        if (presetLeaves(module, k)) continue;
        float v = store.desc(store.id(module, 0, k)).defValue;
        for (const auto& e : preset.values) if (e.first == k) v = e.second;
        out.push_back({ k, v });
    }
    return out;
}

std::string presetText(const ParamStore& params, Module module, int instance)
{
    std::string out;
    const int count = ParamStore::moduleCount(module);
    for (int k = 0; k < count; ++k) {
        if (presetLeaves(module, k)) continue;
        const int id = params.id(module, instance, k);
        char line[96];
        std::snprintf(line, sizeof(line), "%s=%.6g\n", params.desc(id).key, static_cast<double>(params.get(id)));
        out += line;
    }
    return out;
}

bool presetFromText(Module module, const std::string& name, const std::string& text, SoundPreset& out)
{
    ParamStore store;
    out = SoundPreset{};
    out.group = "User";
    out.name = name;
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
            if (key != d.key || presetLeaves(module, k)) continue;
            out.values.push_back({ k, std::clamp(static_cast<float>(std::atof(line.c_str() + eq + 1)), d.minValue, d.maxValue) });
        }
    }
    return !out.values.empty();
}

void applyPreset(ParamStore& params, Module module, int instance, const SoundPreset& preset)
{
    for (const auto& e : presetKnobs(module, preset)) params.set(params.id(module, instance, e.first), e.second);
}

} // namespace eph
