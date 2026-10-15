/**
 * @file Params.cpp
 * @brief Module descriptor tables and the parameter store.
 * @note The store below the tables is copied from Phosphene `Core/src/Params.cpp` at 9a2f615
 *       (24.09.2026); the tables are Ephemeris's own.
 */
#include "eph/synth/Wavetable.h"
#include "eph/Params.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace eph {

const char* const KeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const ScaleNames[] = { "Aeolian", "Dorian", "Phrygian", "Harmonic Minor", "Minor Pentatonic",
                                    "Mixolydian", "Lydian", "Locrian" };
const char* const ShortModSourceNames[] = { "Off", "LFO 1", "LFO 2" };
const char* const ShortModDestNames[] = { "Off", "Pitch", "Tone", "Level", "Pan" };   ///< the smaller matrix's targets (the tape keys, the string machine)
const char* const LfoShapeNames[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random" };
/** @brief an LFO's sync: free or a note value */
const char* const LfoSyncNames[] = { "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T" };
/** @brief the matrix's sources */
const char* const ModSourceNames[] = { "Off", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Mod Env", "Filter Env", "Velocity", "Mod Lane" };
/** @brief the matrix's targets */
const char* const ModDestNames[] = { "Off", "Pitch", "Pulse Width", "Wave", "Table Position", "Cutoff", "Resonance",
                                      "Filter Mode", "Filter FM", "Level", "Pan", "Osc 2 Pitch" };
const char* const VcoNames[] = { "Analog", "Moog 921", "Prophet-5", "Oberheim SEM", "ARP 2600", "E-mu Modular" };
const char* const FilterNames[] = { "Moog Ladder", "Prophet (SSM2040/CEM3320)", "Juno (IR3109)", "Oberheim SEM",
                                    "Xpander (pole mix)", "Diode Ladder (303)", "Korg35 (MS-20)", "Polivoks", "EDP Wasp", "Comb" };
const char* const StyleNames[] = { "Cosmic", "Doom", "Melodic", "Modern", "Drift" };
const char* const MorphNames[] = { "None", "Cosmic", "Doom", "Melodic", "Modern", "Drift" };
const char* const ReverbTypeNames[] = { "Hall", "Plate" };
const char* const EchoTypeNames[] = { "Tape", "BBD" };
const char* const EnsembleTypeNames[] = { "Solina", "Chorus", "Wide" };
const char* const RegistrationNames[] = { "Violins", "Violas", "Cellos", "Basses", "Full", "Hollow", "Brass", "Organ" };
const char* const RowDivisionNames[] = { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1 Bar", "2 Bars", "4 Bars" };
const char* const RowDirectionNames[] = { "Forward", "Backward", "Pendulum", "Random Walk" };
const char* const RowModeNames[] = { "Notes", "Transposer" };
const char* const EchoTimeNames[] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "1/4T" };
const char* const TapeSetNames[] = { "Choir", "Strings", "Flute" };

double echoTimeBeats(EchoTimeE t)
{
    static const double Beats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 2.0 / 3.0 };
    const int i = static_cast<int>(t);
    return i >= 0 && i < static_cast<int>(EchoTimeE::COUNT) ? Beats[i] : 0.75;
}

double rowDivisionBeats(RowDivisionE d)
{
    switch (d) {
    case RowDivisionE::QUARTER:      return 1.0;
    case RowDivisionE::EIGHTH:       return 0.5;
    case RowDivisionE::EIGHTH_T:      return 1.0 / 3.0;
    case RowDivisionE::SIXTEENTH:    return 0.25;
    case RowDivisionE::SIXTEENTH_T:   return 1.0 / 6.0;
    case RowDivisionE::THIRTY_SECOND: return 0.125;
    case RowDivisionE::BAR1:         return 4.0;
    case RowDivisionE::BARS2:        return 8.0;
    case RowDivisionE::BARS4:        return 16.0;
    default:                        return 0.25;
    }
}

namespace {

/** @brief The compose module: what the composer is asked for. */
const ParamDesc ComposeParams[compose::COUNT] = {
    { "bpm",           "Tempo",         "BPM",  60.0f, 160.0f, 118.0f, CurveE::LINEAR },
    { "key",           "Key",           "",      0.0f,  11.0f,   9.0f, CurveE::CHOICE, KeyNames },
    { "scale",         "Scale",         "",      0.0f,   7.0f,   0.0f, CurveE::CHOICE, ScaleNames },
    { "style",         "Style",         "",      0.0f,   4.0f,   0.0f, CurveE::CHOICE, StyleNames },
    { "piece_minutes", "Piece Length",  "min",   4.0f,  40.0f,  16.0f, CurveE::LINEAR },
    { "style_tempo",   "Style Tempo",   "",      0.0f,   1.0f,   1.0f, CurveE::TOGGLE },
    { "concert_minutes", "Concert Length", "min", 0.0f, 720.0f,  0.0f, CurveE::LINEAR },   // up to a night of twelve hours
    { "morph_to",      "Morph To",      "",      0.0f,   5.0f,   0.0f, CurveE::CHOICE, MorphNames },
    { "concert_arc",   "Concert Arc",   "",      0.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "album",         "Album Form",    "",      0.0f,   1.0f,   0.0f, CurveE::TOGGLE },
    { "night_set",     "Night Set",     "",      0.0f,   1.0f,   0.0f, CurveE::TOGGLE },
    { "pick_sounds",   "Composer's Sounds", "",  0.0f,   1.0f,   1.0f, CurveE::TOGGLE },   // a preset per synth and piece
};

/** @brief A row of the rack: its pattern, its voice's place and sends. */
const ParamDesc RowParamDescs[row::COUNT] = {
    { "active",    "Active",     "",      0.0f,  1.0f,  0.0f, CurveE::TOGGLE },
    { "length",    "Length",     "steps", 1.0f, 32.0f, 16.0f, CurveE::INT },
    { "division",  "Division",   "",      0.0f,  8.0f,  3.0f, CurveE::CHOICE, RowDivisionNames },
    { "direction", "Direction",  "",      0.0f,  3.0f,  0.0f, CurveE::CHOICE, RowDirectionNames },
    { "octave",    "Octave",     "",     -3.0f,  3.0f,  0.0f, CurveE::INT },
    { "transpose", "Transpose",  "st",  -12.0f, 12.0f,  0.0f, CurveE::INT },
    // Chance per cycle that one step changes (the Turing machine's shift register, PLAN 5.1).
    { "mutation",  "Mutation",   "",      0.0f,  1.0f,  0.0f, CurveE::LINEAR },
    { "gate",      "Gate",       "%",     5.0f, 100.0f, 50.0f, CurveE::LINEAR },
    { "level",     "Level",      "dB",  -60.0f,  6.0f, -6.0f, CurveE::LINEAR },
    { "pan",       "Pan",        "",     -1.0f,  1.0f,  0.0f, CurveE::LINEAR },
    { "echo",      "Echo Send",  "",      0.0f,  1.0f,  0.3f, CurveE::LINEAR },
    { "mode",      "Mode",       "",      0.0f,  1.0f,  0.0f, CurveE::CHOICE, RowModeNames },
    { "reverb",    "Reverb Send", "",     0.0f,  1.0f,  0.3f, CurveE::LINEAR },
    { "echo2",     "Echo 2 Send", "",     0.0f,  1.0f,  0.0f, CurveE::LINEAR },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 30.0f, CurveE::LOG },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the blend room (the addon's send B)
    { "punch",    "Punch",    "",    0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // the attacks lifted (a transient shaper)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the early reflections (send A)
    { "sweep",    "Filter Sweep", "oct", 0.0f, 2.0f, 0.0f, CurveE::LINEAR },   // the slow Berlin-School sweep of the cutoff
};

/**
 * The modular voice (PLAN 5.2). Ranges after the instruments the style is played on: a ladder from 30 Hz
 * to 16 kHz, a filter envelope of up to six octaves, decays from a tick of 15 ms to the two-second
 * swell a sequence turns into when the hand opens the decay.
 */
const ParamDesc VoiceParams[voice::COUNT] = {
    { "wave",       "Wave",         "",       0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // saw .. pulse
    { "detune",     "Detune",       "ct",     0.0f,    30.0f,    7.0f, CurveE::LINEAR },   // second VCO
    { "pw",         "Pulse Width",  "",       0.05f,    0.5f,    0.5f, CurveE::LINEAR },
    { "drift",      "Drift",        "ct",     0.0f,    15.0f,    3.0f, CurveE::LINEAR },   // stationary spread per VCO
    { "drive",      "Drive",        "dB",     0.0f,    24.0f,    6.0f, CurveE::LINEAR },
    { "cutoff",     "Cutoff",       "Hz",    30.0f, 16000.0f,  600.0f, CurveE::LOG },
    { "resonance",  "Resonance",    "",       0.0f,     1.0f,    0.35f, CurveE::LINEAR },
    { "env_amount", "Env Amount",   "oct",    0.0f,     6.0f,    2.5f, CurveE::LINEAR },
    { "decay",      "Filter Decay", "ms",    15.0f,  2000.0f,  180.0f, CurveE::LOG },
    { "keytrack",   "Key Track",    "",       0.0f,     1.0f,    0.5f, CurveE::LINEAR },
    { "accent",     "Accent",       "",       0.0f,     1.0f,    0.5f, CurveE::LINEAR },
    { "amp_decay",  "Amp Release",  "ms",     5.0f,  2000.0f,   60.0f, CurveE::LOG },
    { "glide",      "Glide",        "ms",     1.0f,   500.0f,   60.0f, CurveE::LOG },
    { "table",      "Wavetable",    "",       0.0f, static_cast<float>(WavetableCount), 0.0f, CurveE::CHOICE, VoiceTableNames },   // 0: analog
    { "table_pos",  "Table Position", "",     0.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "table_mod",  "Table Mod",    "",       0.0f,     1.0f,    0.4f, CurveE::LINEAR },   // the modulation lane on the position
    { "filter",      "Filter",       "",      0.0f,     9.0f,    0.0f, CurveE::CHOICE, FilterNames },   // Filters.h
    { "filter_mode", "Filter Mode",  "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // SEM morph, Xpander response, Polivoks band pass, comb sign
    { "filter_fm",   "Filter FM",    "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // oscillator 1 on the cutoff at audio rate
    { "amp_attack",   "Amp Attack",    "ms",   0.5f,  8000.0f, 2.0f, CurveE::LOG },
    { "amp_decay2",   "Amp Decay",     "ms",   5.0f,  8000.0f, 50.0f, CurveE::LOG },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, CurveE::LINEAR },
    { "filt_attack",  "Filter Attack", "ms",   0.5f,  8000.0f, 1.5f, CurveE::LOG },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "filt_release", "Filter Release", "ms",  5.0f,  8000.0f, 180.0f, CurveE::LOG },
    { "filt_link",    "Release = Decay", "",   0.0f,     1.0f, 1.0f, CurveE::TOGGLE },   // the Minimoog habit: the release takes the decay time
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, CurveE::LINEAR },   // velocity on the filter envelope
    { "mod_attack",   "Mod Attack",    "ms",   0.5f,  8000.0f, 10.0f, CurveE::LOG },
    { "mod_decay",    "Mod Decay",     "ms",   5.0f,  8000.0f, 500.0f, CurveE::LOG },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod_release",  "Mod Release",   "ms",   5.0f,  8000.0f, 300.0f, CurveE::LOG },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.5f, CurveE::LOG },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 5.0f, CurveE::LOG },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.07f, CurveE::LOG },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.2f, CurveE::LOG },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "vco",        "VCO",          "",      0.0f,     5.0f,   0.0f, CurveE::CHOICE, VcoNames },   // Vco.h: 0 the voice's own
    { "sync",       "Sync",         "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },   // VCO 2 hard-synced to VCO 1
    { "osc2_pitch", "Osc 2 Pitch",  "st",  -12.0f,    24.0f,   0.0f, CurveE::LINEAR },   // VCO 2's interval
    { "cross_mod",  "Cross Mod",    "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },   // VCO 1 on VCO 2's frequency
};

/**
 * 24.09.2026: the user heard the first study as dull; the cutoffs and envelope depths of the three
 * voices that play first are raised by about two thirds of an octave.
 */
const char* const VoiceDefaults =
    "voice1.cutoff=650 voice1.resonance=0.4 voice1.env_amount=3.5 voice1.decay=190\n"
    "voice2.wave=0.6 voice2.pw=0.3 voice2.cutoff=1300 voice2.resonance=0.3 voice2.decay=140 voice2.env_amount=2.5\n"
    "voice3.cutoff=1800 voice3.decay=240 voice3.env_amount=2\n";

/** The lead: a brighter, longer, sung version of the voice, with glide and a vibrato that comes in late. */
const ParamDesc LeadParams[lead::COUNT] = {
    { "wave",         "Wave",          "",       0.0f,     1.0f,    0.25f, CurveE::LINEAR },
    { "detune",       "Detune",        "ct",     0.0f,    30.0f,    4.0f, CurveE::LINEAR },
    { "pw",           "Pulse Width",   "",       0.05f,    0.5f,    0.45f, CurveE::LINEAR },
    { "drift",        "Drift",         "ct",     0.0f,    15.0f,    2.0f, CurveE::LINEAR },
    { "drive",        "Drive",         "dB",     0.0f,    24.0f,    8.0f, CurveE::LINEAR },
    { "cutoff",       "Cutoff",        "Hz",    30.0f, 16000.0f, 1500.0f, CurveE::LOG },
    { "resonance",    "Resonance",     "",       0.0f,     1.0f,    0.25f, CurveE::LINEAR },
    { "env_amount",   "Env Amount",    "oct",    0.0f,     6.0f,    2.0f, CurveE::LINEAR },
    { "decay",        "Filter Decay",  "ms",    15.0f,  2000.0f,  450.0f, CurveE::LOG },
    { "keytrack",     "Key Track",     "",       0.0f,     1.0f,    0.6f, CurveE::LINEAR },
    { "accent",       "Accent",        "",       0.0f,     1.0f,    0.3f, CurveE::LINEAR },
    { "amp_decay",    "Amp Release",   "ms",     5.0f,  2000.0f,  300.0f, CurveE::LOG },
    { "glide",        "Glide",         "ms",     1.0f,   500.0f,   80.0f, CurveE::LOG },
    { "level",        "Level",         "dB",   -60.0f,     6.0f,   -9.0f, CurveE::LINEAR },
    { "pan",          "Pan",           "",      -1.0f,     1.0f,    0.15f, CurveE::LINEAR },
    { "echo",         "Echo Send",     "",       0.0f,     1.0f,    0.45f, CurveE::LINEAR },
    { "vibrato",      "Vibrato",       "ct",     0.0f,   100.0f,   18.0f, CurveE::LINEAR },
    { "vibrato_rate", "Vibrato Rate",  "Hz",     1.0f,    10.0f,    5.2f, CurveE::LINEAR },
    { "reverb",       "Reverb Send",   "",       0.0f,     1.0f,    0.4f, CurveE::LINEAR },
    { "auto_pan",     "Auto Pan",      "",       0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // depth of a 0.05 Hz sine on the pan
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 150.0f, CurveE::LOG },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, CurveE::LINEAR },   // into the effect hall (send D)
    { "filter",      "Filter",       "",      0.0f,     9.0f,    0.0f, CurveE::CHOICE, FilterNames },   // Filters.h
    { "filter_mode", "Filter Mode",  "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // SEM morph, Xpander response, Polivoks band pass, comb sign
    { "filter_fm",   "Filter FM",    "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // oscillator 1 on the cutoff at audio rate
    { "amp_attack",   "Amp Attack",    "ms",   0.5f,  8000.0f, 2.0f, CurveE::LOG },
    { "amp_decay2",   "Amp Decay",     "ms",   5.0f,  8000.0f, 50.0f, CurveE::LOG },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, CurveE::LINEAR },
    { "filt_attack",  "Filter Attack", "ms",   0.5f,  8000.0f, 1.5f, CurveE::LOG },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "filt_release", "Filter Release", "ms",  5.0f,  8000.0f, 180.0f, CurveE::LOG },
    { "filt_link",    "Release = Decay", "",   0.0f,     1.0f, 1.0f, CurveE::TOGGLE },   // the Minimoog habit: the release takes the decay time
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, CurveE::LINEAR },   // velocity on the filter envelope
    { "mod_attack",   "Mod Attack",    "ms",   0.5f,  8000.0f, 10.0f, CurveE::LOG },
    { "mod_decay",    "Mod Decay",     "ms",   5.0f,  8000.0f, 500.0f, CurveE::LOG },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod_release",  "Mod Release",   "ms",   5.0f,  8000.0f, 300.0f, CurveE::LOG },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.5f, CurveE::LOG },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 5.0f, CurveE::LOG },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.07f, CurveE::LOG },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.2f, CurveE::LOG },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "vco",        "VCO",          "",      0.0f,     5.0f,   0.0f, CurveE::CHOICE, VcoNames },   // Vco.h: 0 the voice's own
    { "sync",       "Sync",         "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },   // VCO 2 hard-synced to VCO 1
    { "osc2_pitch", "Osc 2 Pitch",  "st",  -12.0f,    24.0f,   0.0f, CurveE::LINEAR },   // VCO 2's interval
    { "cross_mod",  "Cross Mod",    "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },   // VCO 1 on VCO 2's frequency
};

/** The tape keyboard: a first setting of the machine (PLAN 5.4 wants it measured on recordings). */
const ParamDesc TapeParams[tape::COUNT] = {
    { "set",     "Tapes",        "",      0.0f,     2.0f,    0.0f, CurveE::CHOICE, TapeSetNames },
    { "vowel",   "Vowel",        "",      0.0f,     1.0f,    0.2f, CurveE::LINEAR },   // aah .. ooh
    { "wow",     "Wow",          "ct",    0.0f,    30.0f,    6.0f, CurveE::LINEAR },
    { "flutter", "Flutter",      "ct",    0.0f,    10.0f,    2.0f, CurveE::LINEAR },
    { "sag",     "Motor Load",   "ct",    0.0f,     5.0f,    1.0f, CurveE::LINEAR },   // per key beyond the first
    { "tone",    "Tone",         "Hz", 1500.0f, 16000.0f, 7000.0f, CurveE::LOG },
    { "age",     "Age",          "",      0.0f,     1.0f,    0.5f, CurveE::LINEAR },
    { "level",   "Level",        "dB",  -60.0f,     6.0f,   -1.0f, CurveE::LINEAR },   // 25.09.2026: pads 6..12 dB under the sequence, not 17
    { "pan",     "Pan",          "",     -1.0f,     1.0f,   -0.1f, CurveE::LINEAR },
    { "echo",    "Echo Send",    "",      0.0f,     1.0f,    0.1f, CurveE::LINEAR },
    { "reverb",  "Reverb Send",  "",      0.0f,     1.0f,    0.5f, CurveE::LINEAR },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 150.0f, CurveE::LOG },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // 0 near .. 1 the horizon (the addon's macro)
    { "spread",   "Spread",   "",   0.0f,   1.0f,   0.7f, CurveE::LINEAR },   // allpass decorrelation of the mono keyboard
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, CurveE::LINEAR },   // into the effect hall (send D)
    { "amp_attack",   "Swell",         "ms",   1.0f,  8000.0f, 1.0f, CurveE::LOG },   // at 1 ms: the pressure pad alone
    { "amp_decay",    "Decay",         "ms",   5.0f,  8000.0f, 1500.0f, CurveE::LOG },
    { "amp_sustain",  "Sustain Level", "",     0.0f,     1.0f, 1.0f, CurveE::LINEAR },
    { "amp_release",  "Release",       "ms",  10.0f,  3000.0f, 70.0f, CurveE::LOG },   // the fall after the key
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 5.0f, CurveE::LOG },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 0.3f, CurveE::LOG },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
};

/** The drone: the lead's table, set dark and slow, with a two-second release and much hall. */
const ParamDesc DroneParams[lead::COUNT] = {
    { "wave",         "Wave",          "",       0.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "detune",       "Detune",        "ct",     0.0f,    30.0f,    0.0f, CurveE::LINEAR },   // the foundation beats not (the addon's 3)
    { "pw",           "Pulse Width",   "",       0.05f,    0.5f,    0.5f, CurveE::LINEAR },
    { "drift",        "Drift",         "ct",     0.0f,    15.0f,    4.0f, CurveE::LINEAR },
    { "drive",        "Drive",         "dB",     0.0f,    24.0f,    4.0f, CurveE::LINEAR },
    { "cutoff",       "Cutoff",        "Hz",    30.0f, 16000.0f, 320.0f, CurveE::LOG },
    { "resonance",    "Resonance",     "",       0.0f,     1.0f,    0.3f, CurveE::LINEAR },
    { "env_amount",   "Env Amount",    "oct",    0.0f,     6.0f,    0.5f, CurveE::LINEAR },
    { "decay",        "Filter Decay",  "ms",    15.0f,  2000.0f,  900.0f, CurveE::LOG },
    { "keytrack",     "Key Track",     "",       0.0f,     1.0f,    0.3f, CurveE::LINEAR },
    { "accent",       "Accent",        "",       0.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "amp_decay",    "Amp Release",   "ms",     5.0f,  2000.0f,  2000.0f, CurveE::LOG },
    { "glide",        "Glide",         "ms",     1.0f,   500.0f,   300.0f, CurveE::LOG },
    { "level",        "Level",         "dB",   -60.0f,     6.0f,   -10.0f, CurveE::LINEAR },
    { "pan",          "Pan",           "",      -1.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "echo",         "Echo Send",     "",       0.0f,     1.0f,    0.1f, CurveE::LINEAR },
    { "vibrato",      "Vibrato",       "ct",     0.0f,   100.0f,   0.0f, CurveE::LINEAR },
    { "vibrato_rate", "Vibrato Rate",  "Hz",     1.0f,    10.0f,    5.0f, CurveE::LINEAR },
    { "reverb",       "Reverb Send",   "",       0.0f,     1.0f,    0.55f, CurveE::LINEAR },
    { "auto_pan",     "Auto Pan",      "",       0.0f,     1.0f,    0.35f, CurveE::LINEAR },   // the drone wanders (7.3)
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 40.0f, CurveE::LOG },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, CurveE::LINEAR },   // into the effect hall (send D)
    { "filter",      "Filter",       "",      0.0f,     9.0f,    0.0f, CurveE::CHOICE, FilterNames },   // Filters.h
    { "filter_mode", "Filter Mode",  "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // SEM morph, Xpander response, Polivoks band pass, comb sign
    { "filter_fm",   "Filter FM",    "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },   // oscillator 1 on the cutoff at audio rate
    { "amp_attack",   "Amp Attack",    "ms",   0.5f,  8000.0f, 2.0f, CurveE::LOG },
    { "amp_decay2",   "Amp Decay",     "ms",   5.0f,  8000.0f, 50.0f, CurveE::LOG },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, CurveE::LINEAR },
    { "filt_attack",  "Filter Attack", "ms",   0.5f,  8000.0f, 1.5f, CurveE::LOG },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "filt_release", "Filter Release", "ms",  5.0f,  8000.0f, 180.0f, CurveE::LOG },
    { "filt_link",    "Release = Decay", "",   0.0f,     1.0f, 1.0f, CurveE::TOGGLE },   // the Minimoog habit: the release takes the decay time
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, CurveE::LINEAR },   // velocity on the filter envelope
    { "mod_attack",   "Mod Attack",    "ms",   0.5f,  8000.0f, 10.0f, CurveE::LOG },
    { "mod_decay",    "Mod Decay",     "ms",   5.0f,  8000.0f, 500.0f, CurveE::LOG },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod_release",  "Mod Release",   "ms",   5.0f,  8000.0f, 300.0f, CurveE::LOG },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.5f, CurveE::LOG },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 5.0f, CurveE::LOG },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.07f, CurveE::LOG },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.2f, CurveE::LOG },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    11.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "vco",        "VCO",          "",      0.0f,     5.0f,   0.0f, CurveE::CHOICE, VcoNames },   // Vco.h: 0 the voice's own
    { "sync",       "Sync",         "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },   // VCO 2 hard-synced to VCO 1
    { "osc2_pitch", "Osc 2 Pitch",  "st",  -12.0f,    24.0f,   0.0f, CurveE::LINEAR },   // VCO 2's interval
    { "cross_mod",  "Cross Mod",    "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },   // VCO 1 on VCO 2's frequency
};

/** The atmosphere: all layers off until a piece or a hand brings them in. */
const ParamDesc AtmosParams[atmos::COUNT] = {
    { "wind",        "Wind",         "dB",  -60.0f,     0.0f,  -60.0f, CurveE::LINEAR },
    { "wind_tone",   "Wind Tone",    "Hz",  150.0f,  4000.0f,  700.0f, CurveE::LOG },
    { "sweeps",      "Sweeps",       "/min",  0.0f,     6.0f,    0.0f, CurveE::LINEAR },
    { "sweep_level", "Sweep Level",  "dB",  -60.0f,     0.0f,  -18.0f, CurveE::LINEAR },
    { "bleeps",      "Bleeps",       "/min",  0.0f,    12.0f,    0.0f, CurveE::LINEAR },
    { "bleep_level", "Bleep Level",  "dB",  -60.0f,     0.0f,  -22.0f, CurveE::LINEAR },
    { "level",       "Level",        "dB",  -60.0f,     6.0f,    0.0f, CurveE::LINEAR },   // 25.09.2026: the horizon layer, not buried
    { "echo",        "Echo Send",    "",      0.0f,     1.0f,    0.2f, CurveE::LINEAR },
    { "reverb",      "Reverb Send",  "",      0.0f,     1.0f,    0.6f, CurveE::LINEAR },
    { "grains",        "Grains",        "dB",  -60.0f,     0.0f,  -60.0f, CurveE::LINEAR },   // the granular cloud, off at -60
    { "grain_density", "Grain Density", "/s",    1.0f,    40.0f,   12.0f, CurveE::LOG },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 120.0f, CurveE::LOG },   // the strip's high pass (production guide 4.2)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, CurveE::LINEAR },   // into the effect hall (send D)
};

/** The string machine: slow in, slow out, the ensemble deep. */
const ParamDesc StringsParams[strings::COUNT] = {
    { "attack",   "Crescendo",    "s",    0.005f,  3.0f,   0.35f, CurveE::LOG },
    { "release",  "Release",      "s",    0.05f,   6.0f,   1.2f, CurveE::LOG },   // the machines' "sustain" slider
    { "feet",     "Octave Balance", "",   0.0f,    1.0f,   0.4f, CurveE::LINEAR },   // 0 the low footages .. 1 the high ones
    { "tone",     "Tone",         "Hz", 800.0f, 12000.0f, 4500.0f, CurveE::LOG },
    { "ensemble", "Ensemble",     "",     0.0f,    1.0f,   0.8f, CurveE::LINEAR },
    { "level",    "Level",        "dB", -60.0f,    6.0f, -10.0f, CurveE::LINEAR },
    { "pan",      "Pan",          "",    -1.0f,    1.0f,   0.1f, CurveE::LINEAR },
    { "echo",     "Echo Send",    "",     0.0f,    1.0f,   0.05f, CurveE::LINEAR },
    { "reverb",   "Reverb Send",  "",     0.0f,    1.0f,   0.45f, CurveE::LINEAR },
    // After Waldorf's Streichfett (StringMachine.h): Violins, Violas, Cellos, Basses, Full, Hollow, Brass, Organ.
    { "registration",  "Registration",  "",   0.0f,  7.0f,  0.0f, CurveE::LINEAR, RegistrationNames },
    { "animate",       "Animate",       "",   0.0f,  1.0f,  0.3f, CurveE::LINEAR },
    { "animate_rate",  "Animate Rate",  "Hz", 0.01f, 1.0f,  0.05f, CurveE::LOG },
    { "ensemble_type", "Ensemble Type", "",   0.0f,  2.0f,  0.0f, CurveE::CHOICE, EnsembleTypeNames },
    { "phaser",        "Phaser",        "",   0.0f,  1.0f,  0.0f, CurveE::LINEAR },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 200.0f, CurveE::LOG },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, CurveE::LINEAR },   // into the effect hall (send D)
    { "amp_decay",    "Decay",         "s",    0.05f,   20.0f, 2.0f, CurveE::LOG },
    { "amp_sustain",  "Sustain Level", "",     0.0f,     1.0f, 1.0f, CurveE::LINEAR },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 5.5f, CurveE::LOG },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 0.15f, CurveE::LOG },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     2.0f, 0.0f, CurveE::CHOICE, ShortModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    4.0f, 0.0f, CurveE::CHOICE, ShortModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
};

/** The springs: a short, bright-ish tank under the echo, quiet by default. */
const ParamDesc SpringParams[spring::COUNT] = {
    { "decay",  "Decay",  "s",    0.3f,    8.0f,   2.2f, CurveE::LOG },
    { "tone",   "Tone",   "Hz", 1500.0f, 9000.0f, 4500.0f, CurveE::LOG },
    { "return", "Return", "dB",  -60.0f,   12.0f,  -6.0f, CurveE::LINEAR },
};

/** The drum kit: an electronic kit of the eighties, sitting back in the mix. */
const ParamDesc DrumsParams[drums::COUNT] = {
    { "kick_hz", "Kick Pitch",  "Hz",  35.0f,  80.0f,  50.0f, CurveE::LINEAR },
    { "decay",   "Decay",       "",     0.3f,   2.0f,   1.0f, CurveE::LOG },
    { "tone",    "Tone",        "",     0.0f,   1.0f,   0.5f, CurveE::LINEAR },
    { "level",   "Level",       "dB", -60.0f,   6.0f,  -9.0f, CurveE::LINEAR },
    { "echo",    "Echo Send",   "",     0.0f,   1.0f,   0.05f, CurveE::LINEAR },
    { "reverb",  "Reverb Send", "",     0.0f,   1.0f,   0.2f, CurveE::LINEAR },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 25.0f, CurveE::LOG },   // the strip's high pass (production guide 4.2)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, CurveE::LINEAR },   // into the early reflections (send A)
};

/// 01.10.2026: what a MIDI keyboard plays (perform::keys, Engine::queueLive), Replace or Layer.
const char* const KeyboardPartNames[] = { "Off", "Lead", "Drone", "Poly", "Tape Keys", "Strings", "Drums", "Row 1", "Row 2", "Row 3",
                                           "Row 4", "Row 5", "Row 6", "Row 7", "Row 8", "By channel" };
const char* const KeyboardModeNames[] = { "Replace", "Layer" };   ///< perform.keyboard_mode
const char* const KeyboardSplitNames[] = { "C1", "C2", "C3", "C4", "C5" };   ///< perform.keyboard_split: 36 .. 84
const char* const KeyboardVelocityNames[] = { "As Played", "Soft", "Hard", "Fixed" };   ///< perform.keyboard_velocity

/** The performer's controls (live only). */
const ParamDesc PerformParams[perform::COUNT] = {
    { "filter",    "Filter",     "oct", -2.0f,  2.0f, 0.0f, CurveE::LINEAR },
    { "transpose", "Transpose",  "st", -12.0f, 12.0f, 0.0f, CurveE::INT },
    { "hold",      "Hold Moves", "",     0.0f,  1.0f, 0.0f, CurveE::TOGGLE },
    { "throw",     "Echo Throw", "",     0.0f,  1.0f, 0.0f, CurveE::LINEAR },
    // 01.10.2026: a MIDI keyboard plays a voice (Engine::queueLive); the composer can be switched off. Live play only.
    { "keyboard_part", "Keyboard Plays", "", 0.0f, 15.0f, 0.0f, CurveE::CHOICE, KeyboardPartNames },
    { "keyboard_mode", "Keyboard Mode",  "", 0.0f, 1.0f, 0.0f, CurveE::CHOICE, KeyboardModeNames },
    { "composer",      "Composer",       "", 0.0f, 1.0f, 1.0f, CurveE::TOGGLE },
    // The keyboard's options (02.10.2026), all off by default: a split with a second voice under it, Scale Lock, the
    // velocity curve. The lower keys choose among the voices (not by channel).
    { "keyboard_lower",    "Lower Keys Play", "", 0.0f, 14.0f, 0.0f, CurveE::CHOICE, KeyboardPartNames },
    { "keyboard_split",    "Split At",        "", 0.0f, 4.0f, 2.0f, CurveE::CHOICE, KeyboardSplitNames },
    { "keyboard_scale",    "Scale Lock",      "", 0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "keyboard_velocity", "Velocity Curve",  "", 0.0f, 3.0f, 0.0f, CurveE::CHOICE, KeyboardVelocityNames },
};

/** The OSC cues (Cue.h). */
const ParamDesc CueParams[cue::COUNT] = {
    { "enabled", "OSC Cues", "",  0.0f,     1.0f,    0.0f, CurveE::TOGGLE },
    { "port",    "OSC Port", "",  1024.0f, 65535.0f, 9000.0f, CurveE::INT },
};

/** A style of the user's own (Style tab): the defaults are the Cosmic profile's numbers. */
const ParamDesc CustomParams[custom::COUNT] = {
    { "use",          "Use Custom Style", "",     0.0f,   1.0f,   0.0f, CurveE::TOGGLE },
    { "bpm_low",      "Tempo Low",        "BPM", 60.0f, 160.0f, 112.0f, CurveE::LINEAR },
    { "bpm_high",     "Tempo High",       "BPM", 60.0f, 160.0f, 126.0f, CurveE::LINEAR },
    { "minutes_low",  "Piece Min Low",    "min",  4.0f,  40.0f,  12.0f, CurveE::LINEAR },
    { "minutes_high", "Piece Min High",   "min",  4.0f,  40.0f,  22.0f, CurveE::LINEAR },
    { "phases_low",   "Phases Low",       "",     1.0f,   4.0f,   1.0f, CurveE::INT },
    { "phases_high",  "Phases High",      "",     1.0f,   4.0f,   2.0f, CurveE::INT },
    { "intro",        "Atmosphere Share", "",     0.0f,   0.3f,   0.08f, CurveE::LINEAR },
    { "coda",         "Coda Share",       "",     0.0f,   0.3f,   0.10f, CurveE::LINEAR },
    { "new_tempo",    "New Tempo Chance", "",     0.0f,   1.0f,   0.3f, CurveE::LINEAR },
    { "new_key",      "New Key Chance",   "",     0.0f,   1.0f,   0.3f, CurveE::LINEAR },
    { "peak_rows",    "Rows at Peak",     "",     1.0f,   7.0f,   3.0f, CurveE::INT },
    { "mutation",     "Mutation",         "",     0.0f,   1.0f,   0.2f, CurveE::LINEAR },
    { "tape",         "Tape Keys Chance", "",     0.0f,   1.0f,   0.9f, CurveE::LINEAR },
    { "strings",      "Strings Chance",   "",     0.0f,   1.0f,   0.5f, CurveE::LINEAR },
    { "lead",         "Lead Chance",      "",     0.0f,   1.0f,   0.6f, CurveE::LINEAR },
    { "bleeps",       "Bleeps Chance",    "",     0.0f,   1.0f,   0.4f, CurveE::LINEAR },
    { "drums",        "Drums Chance",     "",     0.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "lead_density", "Lead Density",     "",     0.1f,   1.0f,   0.5f, CurveE::LINEAR },
    { "hand_move",    "Move Length",      "s",    2.0f,  30.0f,   8.0f, CurveE::LOG },
    { "hand_rest",    "Rest Between Moves", "s",    4.0f,  40.0f,  14.0f, CurveE::LOG },
    { "darkness",     "Darkness",         "",    -0.5f,   0.5f,  -0.05f, CurveE::LINEAR },
    { "hall",         "Hall",             "s",    1.0f,  15.0f,   6.0f, CurveE::LOG },
    { "level",        "Level",            "dB", -12.0f,  12.0f,   4.8f, CurveE::LINEAR },
};

/** The hall: long and dark, as the style's spaces are (a first setting, to be judged by ear). */
const ParamDesc ReverbParams[reverb::COUNT] = {
    { "size",     "Size",       "",     0.3f,     3.0f,    1.8f, CurveE::LINEAR },
    { "decay",    "Decay",      "s",    0.3f,    20.0f,    5.5f, CurveE::LOG },
    { "damping",  "Damping",    "",     0.0f,     1.0f,    0.45f, CurveE::LINEAR },
    { "predelay", "Pre-Delay",  "ms",   0.0f,   200.0f,   25.0f, CurveE::LINEAR },
    { "lowcut",   "Low Cut",    "Hz",  40.0f,   500.0f,  250.0f, CurveE::LOG },   // the return's EQ (production guide 5.2)
    { "highcut",  "High Cut",   "Hz", 1000.0f, 20000.0f, 6500.0f, CurveE::LOG },
    { "return",   "Return",     "dB",  -60.0f,   12.0f,    4.0f, CurveE::LINEAR },   // the FDN returns quietly: +4 dB puts the tail ~10 dB under the dry mix
    { "type",     "Room",       "",     0.0f,     1.0f,    0.0f, CurveE::CHOICE, ReverbTypeNames },   // the hall or the plate (Plate.h)
    { "duck",     "Duck",       "dB",   0.0f,    12.0f,    2.0f, CurveE::LINEAR },   // under the rows' notes
};

/** @brief The tape echo (and the BBD on the same send). */
const ParamDesc EchoParams[echo::COUNT] = {
    { "time",      "Time",        "",       0.0f,    6.0f,    2.0f, CurveE::CHOICE, EchoTimeNames },
    { "feedback",  "Feedback",    "",       0.0f,    1.1f,    0.45f, CurveE::LINEAR },   // above 1: runaway, held by the tape
    { "tone",      "Tone",        "Hz",   800.0f, 12000.0f, 3500.0f, CurveE::LOG },      // loop low pass
    { "wow",       "Wow",         "ms",     0.0f,    4.0f,    0.2f, CurveE::LINEAR },   // little: the sequence's delay stays in time
    { "flutter",   "Flutter",     "ms",     0.0f,    0.5f,    0.02f, CurveE::LINEAR },
    { "drive",     "Tape Drive",  "dB",     0.0f,   18.0f,    4.0f, CurveE::LINEAR },
    { "pingpong",  "Ping-Pong",   "",       0.0f,    1.0f,    1.0f, CurveE::TOGGLE },
    { "return",    "Return",      "dB",   -60.0f,    6.0f,   -4.0f, CurveE::LINEAR },
    { "type",      "Echo",        "",       0.0f,    1.0f,    0.0f, CurveE::CHOICE, EchoTypeNames },   // the tape or the BBD (Bbd.h)
    { "low_cut",   "Low Cut",     "Hz",    20.0f, 1000.0f,  200.0f, CurveE::LOG },   // in the loop: each repeat thinner
    { "duck",      "Duck",        "dB",     0.0f,   12.0f,    3.0f, CurveE::LINEAR }, // under the rows' notes
};

/**
 * The rows start different from each other: a bass row of sixteen sixteenths an octave down, and
 * counter rows of lengths that do not divide sixteen, so that switching one on makes it drift against
 * the first (PLAN 2.2). Only the first is on.
 */
const char* const DefaultRows =
    "row1.active=1 row1.length=16 row1.division=1/16 row1.octave=-1 row1.pan=0\n"
    "row2.length=13 row2.division=1/16 row2.octave=0 row2.pan=-0.4\n"
    "row3.length=12 row3.division=1/8 row3.octave=1 row3.pan=0.4\n"
    "row4.length=7 row4.division=1/16 row4.octave=1 row4.pan=-0.7\n"
    "row5.length=5 row5.division=1/8 row5.octave=0 row5.pan=0.7\n"
    "row6.length=9 row6.division=1/16 row6.octave=2 row6.pan=-0.2\n"
    "row7.length=24 row7.division=1/8 row7.octave=-1 row7.pan=0.2\n"
    "row8.length=32 row8.division=1/4 row8.octave=0 row8.pan=0\n";

/** @brief The mix bus and the master. */
const ParamDesc MasterParams[master::COUNT] = {
    { "level",    "Level",    "dB",  -60.0f, 6.0f,  0.0f, CurveE::LINEAR },
    // How much the bus compressor works: 0 off, 1 a ratio of 1.5 from -16 dB. The music keeps its dynamics.
    { "compress", "Compress", "",      0.0f, 1.0f,  0.5f, CurveE::LINEAR },
    { "ceiling",  "Ceiling",  "dBTP", -6.0f, 0.0f, -1.0f, CurveE::LINEAR },
    { "width",    "Width",    "dB",   -6.0f, 6.0f,  4.5f, CurveE::LINEAR },   // the side above 300 Hz; under 100 Hz mono
    { "mono",     "Mono",     "",      0.0f, 1.0f,  0.0f, CurveE::TOGGLE },   // a mono check for listening
    { "cascade",  "Cascade Duck", "dB", 0.0f, 6.0f, 2.5f, CurveE::LINEAR },   // rows -> pads -> atmosphere, 300 Hz .. 5 kHz
    { "motion",   "Motion",   "",      0.0f, 1.0f,  1.0f, CurveE::LINEAR },   // slow movements with irrational periods
    { "sub_solo", "Sub Solo", "",      0.0f, 1.0f,  0.0f, CurveE::TOGGLE },   // an 80 Hz low pass on the sum, for listening
    { "clip",        "Soft Clip",    "dB", 0.0f, 2.0f,  0.75f, CurveE::LINEAR },   // what a soft clipper may take off the peaks
    { "sub_ceiling", "Sub Ceiling",  "dBFS", -18.0f, 0.0f, -6.0f, CurveE::LINEAR }, // a limiter on the band under 80 Hz
    { "tame",     "Resonance Tamer", "", 0.0f, 1.0f,  0.6f, CurveE::LINEAR },   // the rows' bus: what sticks out 6 dB over the rest
};

/** @brief One module: its prefix, table, how many instances exist, and optionally their names. */
struct ModuleSpec {
    const char* m_prefix;   ///< the key prefix ("row", "voice", "reverb" ...)
    const ParamDesc* m_descs;   ///< its parameters
    int m_count;   ///< how many
    int m_instances;   ///< how many instances
    const char* const* m_instanceNames = nullptr;   ///< prefixes of the instances instead of prefix + number
};

/** The second echo (Engine: its own tape echo, a little wow, no springs). */
const ParamDesc Echo2Params[echo2::COUNT] = {
    { "time",      "Echo 2 Time",     "",       0.0f,    6.0f,    1.0f, CurveE::CHOICE, EchoTimeNames },
    { "feedback",  "Echo 2 Feedback", "",       0.0f,    1.0f,    0.3f, CurveE::LINEAR },
    { "tone",      "Echo 2 Tone",     "Hz",   800.0f, 12000.0f, 5000.0f, CurveE::LOG },
    { "pingpong",  "Echo 2 Ping-Pong", "",      0.0f,    1.0f,    1.0f, CurveE::TOGGLE },
    { "return",    "Echo 2 Return",   "dB",   -60.0f,    6.0f,   -6.0f, CurveE::LINEAR },
    { "low_cut",   "Echo 2 Low Cut",  "Hz",  20.0f, 1000.0f,  300.0f, CurveE::LOG },
};

/** The blend room (Engine: a second plate, short; its return feeds the hall). */
const ParamDesc BlendParams[blend::COUNT] = {
    { "decay",     "Blend Decay",     "s",     0.3f,     4.0f,    1.2f, CurveE::LOG },
    { "predelay",  "Blend Pre-Delay", "ms",    0.0f,    60.0f,    8.0f, CurveE::LINEAR },
    { "low_cut",   "Blend Low Cut",   "Hz",   60.0f,   800.0f,  240.0f, CurveE::LOG },
    { "high_cut",  "Blend High Cut",  "Hz", 1500.0f, 16000.0f, 7000.0f, CurveE::LOG },
    { "damping",   "Blend Damping",   "",      0.0f,     1.0f,    0.4f, CurveE::LINEAR },
    { "return",    "Blend Return",    "dB",  -60.0f,     6.0f,   -2.0f, CurveE::LINEAR },
    { "into_hall", "Into Hall",       "",      0.0f,     0.5f,   0.15f, CurveE::LINEAR },   // the serial far space
};

/** The early reflections (send A) and the effect hall (send D) of the production guide (fx/Rooms.h). */
const ParamDesc EarlyParams[early::COUNT] = {
    { "size",      "Early Size",      "ms",   10.0f,    80.0f,   35.0f, CurveE::LINEAR },
    { "low_cut",   "Early Low Cut",   "Hz",   60.0f,   800.0f,  200.0f, CurveE::LOG },
    { "high_cut",  "Early High Cut",  "Hz", 2000.0f, 16000.0f, 8000.0f, CurveE::LOG },
    { "return",    "Early Return",    "dB",  -60.0f,     6.0f,   -4.0f, CurveE::LINEAR },
};
/** @brief The effect hall (send D). */
const ParamDesc ShimmerParams[shimmer::COUNT] = {
    { "decay",     "Shimmer Decay",   "s",     2.0f,    20.0f,    8.0f, CurveE::LOG },
    { "amount",    "Shimmer",         "",      0.0f,     0.6f,   0.35f, CurveE::LINEAR },   // the octave fed back
    { "low_cut",   "Shimmer Low Cut", "Hz",  100.0f,  1500.0f,  400.0f, CurveE::LOG },
    { "high_cut",  "Shimmer High Cut","Hz", 1500.0f, 12000.0f, 4000.0f, CurveE::LOG },
    { "return",    "Shimmer Return",  "dB",  -60.0f,     6.0f,   -8.0f, CurveE::LINEAR },
};

/** The pad synth (Poly.h): a wavetable (PWM by default: the analog pad), its slow scan, two detuned oscillators spread
 *  across the field, a 12 dB low pass, a slow envelope and the ensemble. */
const ParamDesc PolyParams[poly::COUNT] = {
    { "table",      "Wavetable",    "",      0.0f, static_cast<float>(WavetableCount - 1), 1.0f, CurveE::CHOICE, WavetableNames },
    { "position",   "Position",     "",      0.0f,     1.0f,   0.3f, CurveE::LINEAR },   // across the table's frames
    { "scan",       "Scan",         "",      0.0f,     1.0f,   0.35f, CurveE::LINEAR },  // the slow sine on the position
    { "scan_rate",  "Scan Rate",    "Hz",    0.005f,   1.0f,   0.06f, CurveE::LOG },
    { "detune",     "Detune",       "ct",    0.0f,    30.0f,   8.0f, CurveE::LINEAR },   // the two oscillators apart
    { "spread",     "Spread",       "",      0.0f,     1.0f,   0.6f, CurveE::LINEAR },   // ... and across the field
    { "drift",      "Drift",        "ct",    0.0f,    12.0f,   3.0f, CurveE::LINEAR },
    { "cutoff",     "Cutoff",       "Hz",  100.0f, 12000.0f, 2200.0f, CurveE::LOG },
    { "resonance",  "Resonance",    "",      0.0f,     1.0f,   0.15f, CurveE::LINEAR },
    { "env_amount", "Env Amount",   "oct",   0.0f,     4.0f,   1.0f, CurveE::LINEAR },
    { "attack",     "Attack",       "s",     0.01f,    8.0f,   1.5f, CurveE::LOG },
    { "release",    "Release",      "s",     0.1f,    10.0f,   4.0f, CurveE::LOG },
    { "chorus",     "Ensemble",     "",      0.0f,     1.0f,   0.5f, CurveE::LINEAR },
    { "level",      "Level",        "dB",  -60.0f,     6.0f,  -4.0f, CurveE::LINEAR },   // measured: its stem 8-10 dB under the main sequence, beside the strings
    { "pan",        "Pan",          "",     -1.0f,     1.0f,  -0.1f, CurveE::LINEAR },
    { "echo",       "Echo Send",    "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "reverb",     "Reverb Send",  "",      0.0f,     1.0f,   0.5f, CurveE::LINEAR },
    { "low_cut",    "Low Cut",      "Hz",   10.0f,   500.0f, 180.0f, CurveE::LOG },    // the production guide's 4.2: pads from 120-200 Hz
    { "distance",   "Distance",     "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "blend",      "Blend Send",   "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "early",      "Early Send",   "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "shimmer",    "Shimmer Send", "",      0.0f,     1.0f,   0.1f, CurveE::LINEAR },
    { "filter",     "Filter",       "",      0.0f,     9.0f,   3.0f, CurveE::CHOICE, FilterNames },   // the SEM by default
    { "filter_mode","Filter Mode",  "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "amp_decay",    "Amp Decay",     "s",    0.01f,   20.0f, 1.0f, CurveE::LOG },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, CurveE::LINEAR },
    { "filt_link",    "Filter Follows Amp", "", 0.0f,     1.0f, 1.0f, CurveE::TOGGLE },   // the filter follows the amplitude envelope
    { "filt_attack",  "Filter Attack", "s",    0.01f,   20.0f, 2.0f, CurveE::LOG },
    { "filt_decay",   "Filter Decay",  "s",    0.01f,   20.0f, 3.0f, CurveE::LOG },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.4f, CurveE::LINEAR },
    { "filt_release", "Filter Release", "s",   0.01f,   20.0f, 4.0f, CurveE::LOG },
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod_attack",   "Mod Attack",    "s",    0.01f,   20.0f, 1.0f, CurveE::LOG },
    { "mod_decay",    "Mod Decay",     "s",    0.01f,   20.0f, 2.0f, CurveE::LOG },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.5f, CurveE::LINEAR },
    { "mod_release",  "Mod Release",   "s",    0.01f,   20.0f, 3.0f, CurveE::LOG },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.2f, CurveE::LOG },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 4.5f, CurveE::LOG },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.05f, CurveE::LOG },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.11f, CurveE::LOG },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, CurveE::TOGGLE },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, CurveE::LINEAR },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, CurveE::CHOICE, ModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    10.0f, 0.0f, CurveE::CHOICE, ModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, CurveE::LINEAR },
};

/** @brief The modules, in the order of Module. */
const ModuleSpec Modules[static_cast<int>(ModuleE::COUNT)] = {
    { "compose", ComposeParams, compose::COUNT, 1 },
    { "row",     RowParamDescs,     row::COUNT,     Rows },
    { "master",  MasterParams,  master::COUNT,  1 },
    { "voice",   VoiceParams,   voice::COUNT,   Rows },
    { "echo",    EchoParams,    echo::COUNT,    1 },
    { "lead",    LeadParams,    lead::COUNT,    1 },
    { "reverb",  ReverbParams,  reverb::COUNT,  1 },
    { "tape",    TapeParams,    tape::COUNT,    1 },
    { "drone",   DroneParams,   lead::COUNT,    1 },
    { "atmos",   AtmosParams,   atmos::COUNT,   1 },
    { "strings", StringsParams, strings::COUNT, 1 },
    { "spring",  SpringParams,  spring::COUNT,  1 },
    { "drums",   DrumsParams,   drums::COUNT,   1 },
    { "perform", PerformParams, perform::COUNT, 1 },
    { "cue",     CueParams,     cue::COUNT,     1 },
    { "custom",  CustomParams,  custom::COUNT,  1 },
    { "delay",   Echo2Params,   echo2::COUNT,   1 },
    { "blend",   BlendParams,   blend::COUNT,   1 },   // the blend room
    { "early",   EarlyParams,   early::COUNT,   1 },   // send A
    { "shimmer", ShimmerParams, shimmer::COUNT, 1 },
    { "poly",    PolyParams,    poly::COUNT,    1 },   // the pad synth   // send D   // the second echo (a prefix without a digit: "row1" is an instance)
};

/** @brief Whether a curve takes whole steps (Int, Choice, Toggle). */
bool isDiscrete(CurveE c) { return c == CurveE::INT || c == CurveE::CHOICE || c == CurveE::TOGGLE; }

/** @brief @p s without spaces, tabs and carriage returns at either end. */
std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

} // namespace

ParamStore::ParamStore()
{
    for (auto& row : m_bases) for (int& b : row) b = -1;
    int total = 0;
    for (const ModuleSpec& m : Modules) total += m.m_count * m.m_instances;
    m_entries.reserve(static_cast<size_t>(total));
    for (int mi = 0; mi < static_cast<int>(ModuleE::COUNT); ++mi) {
        const ModuleSpec& m = Modules[mi];
        for (int inst = 0; inst < m.m_instances && inst < MaxInstances; ++inst) {
            m_bases[mi][inst] = static_cast<int>(m_entries.size());
            std::string prefix = m.m_instanceNames != nullptr ? std::string(m.m_instanceNames[inst]) : std::string(m.m_prefix);
            if (m.m_instances > 1 && m.m_instanceNames == nullptr) prefix += std::to_string(inst + 1);
            for (int p = 0; p < m.m_count; ++p) {
                Entry e{ &m.m_descs[p], prefix + "." + m.m_descs[p].m_key, static_cast<ModuleE>(mi), inst };
                m_index.emplace(e.m_key, static_cast<int>(m_entries.size()));
                m_entries.push_back(std::move(e));
            }
        }
    }
    m_values = std::make_unique<std::atomic<float>[]>(m_entries.size());
    // Defaults: the descriptors', then the instance-specific ones of the rows on top.
    m_defaults.resize(m_entries.size());
    for (int i = 0; i < count(); ++i) {
        m_defaults[static_cast<size_t>(i)] = desc(i).m_defValue;
        m_values[static_cast<size_t>(i)].store(desc(i).m_defValue, std::memory_order_relaxed);
    }
    parseText(DefaultRows);
    parseText(VoiceDefaults);
    for (int i = 0; i < count(); ++i) m_defaults[static_cast<size_t>(i)] = get(i);
}

int ParamStore::base(ModuleE m, int instance) const
{
    const int mi = static_cast<int>(m);
    if (mi < 0 || mi >= static_cast<int>(ModuleE::COUNT) || instance < 0 || instance >= MaxInstances) return -1;
    return m_bases[mi][instance];
}

int ParamStore::find(std::string_view key) const
{
    const auto it = m_index.find(std::string(key));
    return it == m_index.end() ? -1 : it->second;
}

int ParamStore::getInt(int id) const
{
    return static_cast<int>(std::lround(get(id)));
}

void ParamStore::set(int id, float value)
{
    if (id < 0 || id >= count()) return;
    const ParamDesc& d = desc(id);
    if (!(value == value)) value = m_defaults.empty() ? d.m_defValue : m_defaults[static_cast<size_t>(id)];   // NaN
    float v = value < d.m_minValue ? d.m_minValue : (value > d.m_maxValue ? d.m_maxValue : value);
    if (isDiscrete(d.m_curve)) v = std::round(v);
    m_values[static_cast<size_t>(id)].store(v, std::memory_order_relaxed);
}

float ParamStore::toNormalised(int id, float value) const
{
    const ParamDesc& d = desc(id);
    if (d.m_maxValue <= d.m_minValue) return 0.0f;
    float n;
    if (d.m_curve == CurveE::LOG) n = std::log(value / d.m_minValue) / std::log(d.m_maxValue / d.m_minValue);
    else n = (value - d.m_minValue) / (d.m_maxValue - d.m_minValue);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

float ParamStore::fromNormalised(int id, float norm) const
{
    const ParamDesc& d = desc(id);
    const float n = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    float v;
    if (d.m_curve == CurveE::LOG) v = d.m_minValue * std::pow(d.m_maxValue / d.m_minValue, n);
    else v = d.m_minValue + n * (d.m_maxValue - d.m_minValue);
    if (isDiscrete(d.m_curve)) v = std::round(v);
    return v;
}

void ParamStore::resetDefaults()
{
    for (int i = 0; i < count(); ++i) m_values[static_cast<size_t>(i)].store(m_defaults[static_cast<size_t>(i)], std::memory_order_relaxed);
}

int ParamStore::moduleCount(ModuleE m)
{
    const int mi = static_cast<int>(m);
    return mi >= 0 && mi < static_cast<int>(ModuleE::COUNT) ? Modules[mi].m_count : 0;
}

void ParamStore::readModule(ModuleE m, int instance, float* out) const
{
    const int b = base(m, instance);
    if (b < 0) return;
    const int n = moduleCount(m);
    for (int i = 0; i < n; ++i) out[i] = get(b + i);
}

void ParamStore::copyValuesFrom(const ParamStore& other)
{
    const int n = count() < other.count() ? count() : other.count();
    for (int i = 0; i < n; ++i) m_values[static_cast<size_t>(i)].store(other.get(i), std::memory_order_relaxed);
}

namespace {
/** @brief Lower case without spaces, for matching choice names ("Harmonic Minor" = "harmonicminor"). */
std::string foldName(std::string_view s)
{
    std::string out;
    for (char ch : s) {
        if (ch == ' ' || ch == '_' || ch == '-') continue;
        out += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    return out;
}
} // namespace

bool ParamStore::parseText(std::string_view text, std::string* error)
{
    // Split into assignments. Newlines and ';' always separate; whitespace separates only where the
    // next word contains '=' -- so a choice name with a space ("compose.scale=Harmonic Minor") stays
    // one value while "a=1 b=2" is still two assignments. '#' starts a comment to the end of the line.
    std::vector<std::string> items;
    size_t pos = 0;
    bool newItem = true;
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch == '\n' || ch == ';' || ch == '\r') { newItem = true; ++pos; continue; }
        if (ch == ' ' || ch == '\t') { ++pos; continue; }
        if (ch == '#') { while (pos < text.size() && text[pos] != '\n') ++pos; continue; }
        size_t end = pos;
        while (end < text.size() && text[end] != '\n' && text[end] != ';' && text[end] != '\r' && text[end] != ' ' && text[end] != '\t') ++end;
        const std::string_view word = text.substr(pos, end - pos);
        pos = end;
        if (newItem || word.find('=') != std::string_view::npos || items.empty()) items.emplace_back(word);
        else { items.back() += ' '; items.back() += word; }
        newItem = false;
    }

    bool ok = true;
    for (const std::string& item : items) {
        const std::string_view tok = trim(item);
        const size_t eq = tok.find('=');
        if (eq == std::string_view::npos) {
            if (error && ok) *error = "missing '=' in \"" + std::string(tok) + "\"";
            ok = false;
            continue;
        }
        const std::string_view k = trim(tok.substr(0, eq)), v = trim(tok.substr(eq + 1));
        const int id = find(k);
        if (id < 0) {
            if (error && ok) *error = "unknown parameter \"" + std::string(k) + "\"";
            ok = false;
            continue;
        }
        const ParamDesc& d = desc(id);
        bool matched = false;
        if (d.m_choices != nullptr || d.m_curve == CurveE::TOGGLE) {
            const std::string fv = foldName(v);
            if (d.m_curve == CurveE::TOGGLE && (fv == "on" || fv == "off")) { set(id, fv == "on" ? 1.0f : 0.0f); matched = true; }
            for (int c = 0; !matched && d.m_choices != nullptr && c <= static_cast<int>(d.m_maxValue); ++c) {
                if (fv == foldName(d.m_choices[c])) { set(id, static_cast<float>(c)); matched = true; }
            }
        }
        if (!matched) {
            const std::string vs(v);
            char* stop = nullptr;
            const double x = std::strtod(vs.c_str(), &stop);
            if (vs.empty() || stop == nullptr || *stop != 0) {
                if (error && ok) *error = "bad value \"" + vs + "\" for " + std::string(k);
                ok = false;
                continue;
            }
            set(id, static_cast<float>(x));
        }
    }
    return ok;
}

std::string ParamStore::toText(bool onlyChanged) const
{
    std::string out;
    char buf[64];
    for (int i = 0; i < count(); ++i) {
        const float v = get(i);
        if (onlyChanged && v == m_defaults[static_cast<size_t>(i)]) continue;
        // %.9g round-trips every float exactly.
        std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
        out += key(i);
        out += '=';
        out += buf;
        out += '\n';
    }
    return out;
}

std::string morphText(const ParamDesc& d, float v)
{
    const int last = static_cast<int>(d.m_maxValue);
    const float c = std::clamp(v, d.m_minValue, d.m_maxValue);
    const int lo = std::clamp(static_cast<int>(std::floor(c)), 0, last);
    const float frac = c - static_cast<float>(lo);
    if (frac < 0.1f || lo == last) return d.m_choices[lo];
    if (frac > 0.9f) return d.m_choices[lo + 1];
    return std::string(d.m_choices[lo]) + "/" + d.m_choices[lo + 1];
}

std::string ParamStore::format(int id) const
{
    const ParamDesc& d = desc(id);
    const float v = get(id);
    if (d.m_curve == CurveE::CHOICE && d.m_choices != nullptr) return d.m_choices[getInt(id)];
    if (d.m_curve == CurveE::TOGGLE) return v >= 0.5f ? "On" : "Off";
    if (d.m_curve == CurveE::LINEAR && d.m_choices != nullptr) return morphText(d, v);
    char buf[64];
    if (d.m_curve == CurveE::INT) std::snprintf(buf, sizeof(buf), "%d", getInt(id));
    else if (std::fabs(v) >= 100.0f) std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(v));
    else if (std::fabs(v) >= 10.0f) std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    else std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    std::string s = buf;
    if (d.m_unit != nullptr && d.m_unit[0] != 0) { s += ' '; s += d.m_unit; }
    return s;
}

} // namespace eph
