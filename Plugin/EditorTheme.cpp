/**
 * @file EditorTheme.cpp
 * @brief The palette, the families, the synths' panels and the look and feel (EditorTheme.h).
 * @author Rene Weller
 */
#include "EditorTheme.h"
#include "EphemerisData.h"

void drawLogo( juce::Graphics &g, juce::Rectangle<float> r );   // PluginEditor.cpp
#include "eph/Engine.h"
#include <cmath>

namespace ephui
{

using namespace colour;

juce::Colour familyColour( Family f )
{
    switch ( f )
    {
    case Family::SOURCE: return juce::Colour( 0xffe0a458 );     // amber
    case Family::FILTER: return juce::Colour( 0xffd9825b );     // copper
    case Family::ENVELOPE: return juce::Colour( 0xff9fbf6f );   // sage
    case Family::MOTION: return juce::Colour( 0xff6fb8ae );     // teal
    default: return juce::Colour( 0xff6f8fb8 );                 // steel blue
    }
}

juce::Colour channelColour( int channel )
{
    // The rows the sources' amber, the lead the filter's copper, the drone the room's blue, the tape keys a warm
    // sand, the strings sage, the pad synth teal, the drums a brick red, the atmosphere a dusk lavender.
    if ( channel < eph::Rows ) return juce::Colour( 0xffe0a458 );
    switch ( channel - eph::Rows )
    {
    case 0: return juce::Colour( 0xffd9825b );
    case 1: return juce::Colour( 0xff6f8fb8 );
    case 2: return juce::Colour( 0xffc2a27e );
    case 3: return juce::Colour( 0xff9fbf6f );
    case 4: return juce::Colour( 0xff6fb8ae );
    case 5: return juce::Colour( 0xffc4695a );
    default: return juce::Colour( 0xff8f93c8 );
    }
}

const std::vector<GroupSpec> &layoutOf( eph::ModuleE m )
{
    using eph::ModuleE;
    using F = Family;
    static const std::vector<GroupSpec> none;
    static const std::vector<GroupSpec> sends = {
        { "Sends", F::SPACE, { "echo", "echo2", "reverb", "blend", "early", "shimmer" } }
    };
    auto with = []( std::vector<GroupSpec> a, const std::vector<GroupSpec> &b )
    {
        a.insert( a.end(), b.begin(), b.end() );
        return a;
    };
    // 26.09.2026: the envelopes in full, the LFOs and the modulation matrix (Modulation.h); "@mod3" is a slot of the
    // matrix (its source, target and amount in one control), "~lfo1_shape" a narrow menu.
    static const std::vector<GroupSpec> lfos4 = {
        { "LFO 1", F::MOTION, { "lfo1_rate", "~lfo1_shape", "~lfo1_sync", "lfo1_retrig", "lfo1_fade" } },
        { "LFO 2", F::MOTION, { "lfo2_rate", "~lfo2_shape", "~lfo2_sync", "lfo2_retrig", "lfo2_fade" } },
        { "LFO 3", F::MOTION, { "lfo3_rate", "~lfo3_shape", "~lfo3_sync", "lfo3_retrig", "lfo3_fade" } },
        { "LFO 4", F::MOTION, { "lfo4_rate", "~lfo4_shape", "~lfo4_sync", "lfo4_retrig", "lfo4_fade" } },
        { "Mod Matrix", F::MOTION, { "@mod1", "@mod2", "@mod3", "@mod4", "@mod5", "@mod6", "@mod7", "@mod8" } }
    };
    static const std::vector<GroupSpec> lfos2 = {
        { "LFO 1", F::MOTION, { "lfo1_rate", "~lfo1_shape", "~lfo1_sync", "lfo1_retrig", "lfo1_fade" } },
        { "LFO 2", F::MOTION, { "lfo2_rate", "~lfo2_shape", "~lfo2_sync", "lfo2_retrig", "lfo2_fade" } },
        { "Mod Matrix", F::MOTION, { "@mod1", "@mod2", "@mod3", "@mod4" } }
    };
    static const std::vector<GroupSpec> voiceEnvelopes = {
        { "Amp Envelope", F::ENVELOPE, { "amp_attack", "amp_decay2", "amp_sustain", "amp_decay" } },
        { "Filter Envelope",
          F::ENVELOPE,
          { "filt_attack", "*decay", "filt_sustain", "filt_release", "filt_link", "env_velocity" } },
        { "Mod Envelope", F::ENVELOPE, { "mod_attack", "mod_decay", "mod_sustain", "mod_release" } }
    };
    static const std::vector<GroupSpec> voice =
        with( with( { { "Oscillators",
                        F::SOURCE,
                        { "vco", "wave", "detune", "pw", "drift", "drive", "osc2_pitch", "sync", "cross_mod" } },
                      { "Wavetable", F::SOURCE, { "table", "table_pos", "table_mod" } },
                      { "Filter",
                        F::FILTER,
                        { "filter", "*cutoff", "*resonance", "filter_mode", "env_amount", "keytrack", "accent",
                          "filter_fm" } } },
                    voiceEnvelopes ),
              with( { { "Glide", F::MOTION, { "glide" } } }, lfos4 ) );
    static const std::vector<GroupSpec> lead = with(
        with( with( { { "Oscillators",
                        F::SOURCE,
                        { "vco", "wave", "detune", "pw", "drift", "drive", "osc2_pitch", "sync", "cross_mod" } },
                      { "Filter",
                        F::FILTER,
                        { "filter", "*cutoff", "*resonance", "filter_mode", "env_amount", "keytrack", "accent",
                          "filter_fm" } } },
                    voiceEnvelopes ),
              with( { { "Performance", F::MOTION, { "glide", "vibrato", "vibrato_rate", "auto_pan" } } }, lfos4 ) ),
        with( { { "Mix", F::SPACE, { "*level", "pan", "low_cut", "distance" } } }, sends ) );
    static const std::vector<GroupSpec> tape =
        with( with( { { "Tapes", F::SOURCE, { "set", "*vowel", "age" } },
                      { "Tone", F::FILTER, { "*tone" } },
                      { "Envelope", F::ENVELOPE, { "amp_attack", "amp_decay", "amp_sustain", "amp_release" } },
                      { "Transport", F::MOTION, { "wow", "flutter", "sag" } } },
                    lfos2 ),
              with( { { "Mix", F::SPACE, { "*level", "pan", "spread", "low_cut", "distance" } } }, sends ) );
    static const std::vector<GroupSpec> strings =
        with( with( { { "Registers", F::SOURCE, { "*registration", "feet", "animate", "animate_rate" } },
                      { "Tone", F::FILTER, { "*tone" } },
                      { "Envelope", F::ENVELOPE, { "attack", "amp_decay", "amp_sustain", "release" } },
                      { "Ensemble", F::MOTION, { "ensemble_type", "*ensemble", "phaser" } } },
                    lfos2 ),
              with( { { "Mix", F::SPACE, { "*level", "pan", "low_cut", "distance" } } }, sends ) );
    static const std::vector<GroupSpec> poly =
        with( with( { { "Wavetable", F::SOURCE, { "table", "*position", "scan", "scan_rate" } },
                      { "Oscillators", F::SOURCE, { "detune", "spread", "drift" } },
                      { "Filter", F::FILTER, { "filter", "*cutoff", "*resonance", "filter_mode", "env_amount" } },
                      { "Amp Envelope", F::ENVELOPE, { "attack", "amp_decay", "amp_sustain", "release" } },
                      { "Filter Envelope",
                        F::ENVELOPE,
                        { "filt_link", "filt_attack", "filt_decay", "filt_sustain", "filt_release", "env_velocity" } },
                      { "Mod Envelope", F::ENVELOPE, { "mod_attack", "mod_decay", "mod_sustain", "mod_release" } },
                      { "Ensemble", F::MOTION, { "*chorus" } } },
                    lfos4 ),
              with( { { "Mix", F::SPACE, { "*level", "pan", "low_cut", "distance" } } }, sends ) );
    static const std::vector<GroupSpec> atmos = {
        { "Wind", F::SOURCE, { "*wind", "wind_tone" } },      { "Sweeps", F::MOTION, { "sweeps", "sweep_level" } },
        { "Bleeps", F::SOURCE, { "bleeps", "bleep_level" } }, { "Grains", F::MOTION, { "grains", "grain_density" } },
        { "Mix", F::SPACE, { "*level", "low_cut" } },         { "Sends", F::SPACE, { "echo", "reverb", "shimmer" } }
    };
    static const std::vector<GroupSpec> drums = { { "Kit", F::SOURCE, { "*kick_hz", "decay", "tone" } },
                                                  { "Mix", F::SPACE, { "*level", "low_cut" } },
                                                  { "Sends", F::SPACE, { "echo", "reverb", "blend", "early" } } };
    static const std::vector<GroupSpec> echo = {
        { "Tape Echo", F::MOTION, { "type", "*time", "*feedback", "tone", "drive" } },
        { "Tape", F::MOTION, { "wow", "flutter" } },
        { "Return", F::SPACE, { "pingpong", "return", "low_cut", "duck" } }
    };
    static const std::vector<GroupSpec> spring = { { "Spring", F::SPACE, { "*decay", "tone", "return" } } };
    static const std::vector<GroupSpec> delay = {
        { "Echo 2", F::MOTION, { "*time", "*feedback", "tone", "pingpong", "return", "low_cut" } }
    };
    static const std::vector<GroupSpec> reverb = {
        { "Hall", F::SPACE, { "type", "*size", "*decay", "damping", "predelay" } },
        { "Hall Return", F::SPACE, { "lowcut", "highcut", "return", "duck" } }
    };
    static const std::vector<GroupSpec> blend = {
        { "Blend Room", F::SPACE, { "*decay", "predelay", "damping", "low_cut", "high_cut", "return", "into_hall" } }
    };
    static const std::vector<GroupSpec> early = {
        { "Early Reflections", F::SPACE, { "*size", "low_cut", "high_cut", "return" } }
    };
    static const std::vector<GroupSpec> shimmer = {
        { "Shimmer", F::SPACE, { "*decay", "*amount", "low_cut", "high_cut", "return" } }
    };
    static const std::vector<GroupSpec> master = { { "Output", F::SPACE, { "*level", "compress", "ceiling", "clip" } },
                                                   { "Stereo", F::SPACE, { "width", "mono" } },
                                                   { "Low End", F::FILTER, { "sub_ceiling", "sub_solo" } },
                                                   { "Mix Bus", F::MOTION, { "cascade", "tame", "motion" } } };
    static const std::vector<GroupSpec> compose = {
        { "Composer", F::SOURCE, { "style", "key", "scale", "bpm", "style_tempo", "piece_minutes", "pick_sounds" } },
        { "Concert", F::MOTION, { "concert_minutes", "night_set", "morph_to", "concert_arc", "album" } }
    };
    static const std::vector<GroupSpec> cue = { { "Cues", F::SPACE, { "enabled", "port" } } };
    static const std::vector<GroupSpec> perform = {
        { "Perform", F::MOTION, { "*filter", "transpose", "hold", "throw" } },
        // 01.10.2026: what a MIDI keyboard plays, and whether the composer does
        // 02.10.2026: the split, Scale Lock, the velocity curve
        { "Keyboard",
          F::SOURCE,
          { "keyboard_part", "~keyboard_mode", "composer", "keyboard_lower", "~keyboard_split", "keyboard_scale",
            "~keyboard_velocity" } }
    };
    static const std::vector<GroupSpec> row = with(
        { { "Sequence", F::SOURCE, { "active", "mode", "length", "division", "direction", "octave", "transpose" } },
          { "Change", F::MOTION, { "mutation", "gate", "*sweep", "punch" } },
          { "Mix", F::SPACE, { "*level", "pan", "low_cut", "distance" } } },
        sends );
    static const std::vector<GroupSpec> custom = {
        { "Use", F::SOURCE, { "use" } },
        { "Tempo and Form",
          F::SOURCE,
          { "bpm_low", "bpm_high", "minutes_low", "minutes_high", "phases_low", "phases_high", "intro", "coda",
            "new_tempo", "new_key" } },
        { "Layers",
          F::MOTION,
          { "peak_rows", "mutation", "tape", "strings", "lead", "bleeps", "drums", "lead_density" } },
        { "Moves and Sound", F::ENVELOPE, { "hand_move", "hand_rest", "darkness", "hall", "level" } }
    };
    switch ( m )
    {
    case ModuleE::VOICE: return voice;
    case ModuleE::LEAD:
    case ModuleE::DRONE: return lead;
    case ModuleE::TAPE: return tape;
    case ModuleE::STRINGS: return strings;
    case ModuleE::POLY: return poly;
    case ModuleE::ATMOS: return atmos;
    case ModuleE::DRUMS: return drums;
    case ModuleE::ECHO: return echo;
    case ModuleE::SPRING: return spring;
    case ModuleE::ECHO2: return delay;
    case ModuleE::REVERB: return reverb;
    case ModuleE::BLEND: return blend;
    case ModuleE::EARLY: return early;
    case ModuleE::SHIMMER: return shimmer;
    case ModuleE::MASTER: return master;
    case ModuleE::COMPOSE: return compose;
    case ModuleE::CUE: return cue;
    case ModuleE::PERFORM: return perform;
    case ModuleE::ROW: return row;
    case ModuleE::CUSTOM: return custom;
    default: return none;
    }
}

// ---------------------------------------------------------------------------------------------------------------------

const frame::Skin &skin()
{
    static const frame::Skin s = []
    {
        frame::Skin k;
        k.m_name = "Ephemeris";
        k.m_bg = Bg;
        k.m_panel = Panel;
        k.m_group = Group;
        k.m_raised = Raised;
        k.m_edge = Edge;
        k.m_ink = Ink;
        k.m_dim = Dim;
        k.m_faint = Faint;
        k.m_accent = Amber;
        k.m_onset = Onset;
        k.m_good = Green;
        k.m_bad = Red;
        for ( int f = 0; f < 5; ++f ) k.m_families[f] = familyColour( static_cast<Family>( f ) );
        for ( int d = 0; d < 3; ++d )
            k.m_decks[d] = familyColour( d == 0 ? Family::SOURCE : d == 1 ? Family::MOTION : Family::SPACE );
        k.m_radius = 4.0f;
        k.m_tracking = 0.05f;
        k.m_typeface = "Georgia";   // engraved, as the tables of an old ephemeris
        k.m_titleBold = true;
        k.m_backdropData = EphemerisData::backdrop_jpg;
        k.m_backdropSize = EphemerisData::backdrop_jpgSize;
        k.m_backdropTop = 0.8f;
        k.m_backdropPage = 0.55f;
        k.m_logo = []( juce::Graphics &g, juce::Rectangle<float> r ) { drawLogo( g, r ); };
        return k;
    }();
    return s;
}

}   // namespace ephui
