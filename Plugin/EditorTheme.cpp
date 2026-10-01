/**
 * @file EditorTheme.cpp
 * @brief The palette, the families, the synths' panels and the look and feel (EditorTheme.h).
 */
#include "EditorTheme.h"
#include "EphemerisData.h"

void drawLogo(juce::Graphics& g, juce::Rectangle<float> r);   // PluginEditor.cpp
#include "eph/Engine.h"
#include <cmath>

namespace ephui {

using namespace colour;

juce::Colour familyColour(Family f)
{
    switch (f) {
    case Family::Source:   return juce::Colour(0xffe0a458);   // amber
    case Family::Filter:   return juce::Colour(0xffd9825b);   // copper
    case Family::Envelope: return juce::Colour(0xff9fbf6f);   // sage
    case Family::Motion:   return juce::Colour(0xff6fb8ae);   // teal
    default:               return juce::Colour(0xff6f8fb8);   // steel blue
    }
}

juce::Colour channelColour(int c)
{
    // The rows the sources' amber, the lead the filter's copper, the drone the room's blue, the tape keys a warm
    // sand, the strings sage, the pad synth teal, the drums a brick red, the atmosphere a dusk lavender.
    if (c < eph::kRows) return juce::Colour(0xffe0a458);
    switch (c - eph::kRows) {
    case 0: return juce::Colour(0xffd9825b);
    case 1: return juce::Colour(0xff6f8fb8);
    case 2: return juce::Colour(0xffc2a27e);
    case 3: return juce::Colour(0xff9fbf6f);
    case 4: return juce::Colour(0xff6fb8ae);
    case 5: return juce::Colour(0xffc4695a);
    default: return juce::Colour(0xff8f93c8);
    }
}

const std::vector<GroupSpec>& layoutOf(eph::Module m)
{
    using eph::Module;
    using F = Family;
    static const std::vector<GroupSpec> none;
    static const std::vector<GroupSpec> sends = { { "Sends", F::Space, { "echo", "echo2", "reverb", "blend", "early", "shimmer" } } };
    auto with = [](std::vector<GroupSpec> a, const std::vector<GroupSpec>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
    // 26.09.2026: the envelopes in full, the LFOs and the modulation matrix (Modulation.h); "@mod3" is a slot of the
    // matrix (its source, target and amount in one control), "~lfo1_shape" a narrow menu.
    static const std::vector<GroupSpec> lfos4 = { { "LFO 1", F::Motion, { "lfo1_rate", "~lfo1_shape", "~lfo1_sync", "lfo1_retrig", "lfo1_fade" } },
        { "LFO 2", F::Motion, { "lfo2_rate", "~lfo2_shape", "~lfo2_sync", "lfo2_retrig", "lfo2_fade" } },
        { "LFO 3", F::Motion, { "lfo3_rate", "~lfo3_shape", "~lfo3_sync", "lfo3_retrig", "lfo3_fade" } },
        { "LFO 4", F::Motion, { "lfo4_rate", "~lfo4_shape", "~lfo4_sync", "lfo4_retrig", "lfo4_fade" } },
        { "Mod Matrix", F::Motion, { "@mod1", "@mod2", "@mod3", "@mod4", "@mod5", "@mod6", "@mod7", "@mod8" } } };
    static const std::vector<GroupSpec> lfos2 = { { "LFO 1", F::Motion, { "lfo1_rate", "~lfo1_shape", "~lfo1_sync", "lfo1_retrig", "lfo1_fade" } },
        { "LFO 2", F::Motion, { "lfo2_rate", "~lfo2_shape", "~lfo2_sync", "lfo2_retrig", "lfo2_fade" } },
        { "Mod Matrix", F::Motion, { "@mod1", "@mod2", "@mod3", "@mod4" } } };
    static const std::vector<GroupSpec> voiceEnvelopes = {
        { "Amp Envelope", F::Envelope, { "amp_attack", "amp_decay2", "amp_sustain", "amp_decay" } },
        { "Filter Envelope", F::Envelope, { "filt_attack", "*decay", "filt_sustain", "filt_release", "filt_link", "env_velocity" } },
        { "Mod Envelope", F::Envelope, { "mod_attack", "mod_decay", "mod_sustain", "mod_release" } } };
    static const std::vector<GroupSpec> voice = with(with({
        { "Oscillators", F::Source, { "vco", "wave", "detune", "pw", "drift", "drive", "osc2_pitch", "sync", "cross_mod" } },
        { "Wavetable", F::Source, { "table", "table_pos", "table_mod" } },
        { "Filter", F::Filter, { "filter", "*cutoff", "*resonance", "filter_mode", "env_amount", "keytrack", "accent", "filter_fm" } } },
        voiceEnvelopes), with({ { "Glide", F::Motion, { "glide" } } }, lfos4));
    static const std::vector<GroupSpec> lead = with(with(with({
        { "Oscillators", F::Source, { "vco", "wave", "detune", "pw", "drift", "drive", "osc2_pitch", "sync", "cross_mod" } },
        { "Filter", F::Filter, { "filter", "*cutoff", "*resonance", "filter_mode", "env_amount", "keytrack", "accent", "filter_fm" } } },
        voiceEnvelopes), with({
        { "Performance", F::Motion, { "glide", "vibrato", "vibrato_rate", "auto_pan" } } }, lfos4)), with({
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends));
    static const std::vector<GroupSpec> tape = with(with({
        { "Tapes", F::Source, { "set", "*vowel", "age" } },
        { "Tone", F::Filter, { "*tone" } },
        { "Envelope", F::Envelope, { "amp_attack", "amp_decay", "amp_sustain", "amp_release" } },
        { "Transport", F::Motion, { "wow", "flutter", "sag" } } }, lfos2), with({
        { "Mix", F::Space, { "*level", "pan", "spread", "low_cut", "distance" } } }, sends));
    static const std::vector<GroupSpec> strings = with(with({
        { "Registers", F::Source, { "*registration", "feet", "animate", "animate_rate" } },
        { "Tone", F::Filter, { "*tone" } },
        { "Envelope", F::Envelope, { "attack", "amp_decay", "amp_sustain", "release" } },
        { "Ensemble", F::Motion, { "ensemble_type", "*ensemble", "phaser" } } }, lfos2), with({
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends));
    static const std::vector<GroupSpec> poly = with(with({
        { "Wavetable", F::Source, { "table", "*position", "scan", "scan_rate" } },
        { "Oscillators", F::Source, { "detune", "spread", "drift" } },
        { "Filter", F::Filter, { "filter", "*cutoff", "*resonance", "filter_mode", "env_amount" } },
        { "Amp Envelope", F::Envelope, { "attack", "amp_decay", "amp_sustain", "release" } },
        { "Filter Envelope", F::Envelope, { "filt_link", "filt_attack", "filt_decay", "filt_sustain", "filt_release", "env_velocity" } },
        { "Mod Envelope", F::Envelope, { "mod_attack", "mod_decay", "mod_sustain", "mod_release" } },
        { "Ensemble", F::Motion, { "*chorus" } } }, lfos4), with({
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends));
    static const std::vector<GroupSpec> atmos = {
        { "Wind", F::Source, { "*wind", "wind_tone" } },
        { "Sweeps", F::Motion, { "sweeps", "sweep_level" } },
        { "Bleeps", F::Source, { "bleeps", "bleep_level" } },
        { "Grains", F::Motion, { "grains", "grain_density" } },
        { "Mix", F::Space, { "*level", "low_cut" } },
        { "Sends", F::Space, { "echo", "reverb", "shimmer" } } };
    static const std::vector<GroupSpec> drums = {
        { "Kit", F::Source, { "*kick_hz", "decay", "tone" } },
        { "Mix", F::Space, { "*level", "low_cut" } },
        { "Sends", F::Space, { "echo", "reverb", "blend", "early" } } };
    static const std::vector<GroupSpec> echo = {
        { "Tape Echo", F::Motion, { "type", "*time", "*feedback", "tone", "drive" } },
        { "Tape", F::Motion, { "wow", "flutter" } },
        { "Return", F::Space, { "pingpong", "return", "low_cut", "duck" } } };
    static const std::vector<GroupSpec> spring = { { "Spring", F::Space, { "*decay", "tone", "return" } } };
    static const std::vector<GroupSpec> delay = {
        { "Echo 2", F::Motion, { "*time", "*feedback", "tone", "pingpong", "return", "low_cut" } } };
    static const std::vector<GroupSpec> reverb = {
        { "Hall", F::Space, { "type", "*size", "*decay", "damping", "predelay" } },
        { "Hall Return", F::Space, { "lowcut", "highcut", "return", "duck" } } };
    static const std::vector<GroupSpec> blend = {
        { "Blend Room", F::Space, { "*decay", "predelay", "damping", "low_cut", "high_cut", "return", "into_hall" } } };
    static const std::vector<GroupSpec> early = { { "Early Reflections", F::Space, { "*size", "low_cut", "high_cut", "return" } } };
    static const std::vector<GroupSpec> shimmer = { { "Shimmer", F::Space, { "*decay", "*amount", "low_cut", "high_cut", "return" } } };
    static const std::vector<GroupSpec> master = {
        { "Output", F::Space, { "*level", "compress", "ceiling", "clip" } },
        { "Stereo", F::Space, { "width", "mono" } },
        { "Low End", F::Filter, { "sub_ceiling", "sub_solo" } },
        { "Mix Bus", F::Motion, { "cascade", "tame", "motion" } } };
    static const std::vector<GroupSpec> compose = {
        { "Composer", F::Source, { "style", "key", "scale", "bpm", "style_tempo", "piece_minutes", "pick_sounds" } },
        { "Concert", F::Motion, { "concert_minutes", "night_set", "morph_to", "concert_arc", "album" } } };
    static const std::vector<GroupSpec> cue = { { "Cues", F::Space, { "enabled", "port" } } };
    static const std::vector<GroupSpec> perform = { { "Perform", F::Motion, { "*filter", "transpose", "hold", "throw" } } };
    static const std::vector<GroupSpec> row = with({
        { "Sequence", F::Source, { "active", "mode", "length", "division", "direction", "octave", "transpose" } },
        { "Change", F::Motion, { "mutation", "gate", "*sweep", "punch" } },
        { "Mix", F::Space, { "*level", "pan", "low_cut", "distance" } } }, sends);
    static const std::vector<GroupSpec> custom = {
        { "Use", F::Source, { "use" } },
        { "Tempo and Form", F::Source, { "bpm_low", "bpm_high", "minutes_low", "minutes_high", "phases_low", "phases_high",
                                         "intro", "coda", "new_tempo", "new_key" } },
        { "Layers", F::Motion, { "peak_rows", "mutation", "tape", "strings", "lead", "bleeps", "drums", "lead_density" } },
        { "Moves and Sound", F::Envelope, { "hand_move", "hand_rest", "darkness", "hall", "level" } } };
    switch (m) {
    case Module::Voice: return voice;
    case Module::Lead: case Module::Drone: return lead;
    case Module::Tape: return tape;
    case Module::Strings: return strings;
    case Module::Poly: return poly;
    case Module::Atmos: return atmos;
    case Module::Drums: return drums;
    case Module::Echo: return echo;
    case Module::Spring: return spring;
    case Module::Echo2: return delay;
    case Module::Reverb: return reverb;
    case Module::Blend: return blend;
    case Module::Early: return early;
    case Module::Shimmer: return shimmer;
    case Module::Master: return master;
    case Module::Compose: return compose;
    case Module::Cue: return cue;
    case Module::Perform: return perform;
    case Module::Row: return row;
    case Module::Custom: return custom;
    default: return none;
    }
}

// ---------------------------------------------------------------------------------------------------------------------

const frame::Skin& skin()
{
    static const frame::Skin s = [] {
        frame::Skin k;
        k.name = "Ephemeris";
        k.bg = bg; k.panel = panel; k.group = group; k.raised = raised; k.edge = edge;
        k.ink = ink; k.dim = dim; k.faint = faint; k.accent = amber; k.onset = onset; k.good = green; k.bad = red;
        for (int f = 0; f < 5; ++f) k.families[f] = familyColour(static_cast<Family>(f));
        for (int d = 0; d < 3; ++d) k.decks[d] = familyColour(d == 0 ? Family::Source : d == 1 ? Family::Motion : Family::Space);
        k.radius = 4.0f;
        k.tracking = 0.05f;
        k.typeface = "Georgia";        // engraved, as the tables of an old ephemeris
        k.titleBold = true;
        k.backdropData = EphemerisData::backdrop_jpg;
        k.backdropSize = EphemerisData::backdrop_jpgSize;
        k.backdropTop = 0.8f;
        k.backdropPage = 0.55f;
        k.logo = [](juce::Graphics& g, juce::Rectangle<float> r) { drawLogo(g, r); };
        return k;
    }();
    return s;
}

} // namespace ephui
