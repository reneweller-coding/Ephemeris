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

const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const kScaleNames[] = { "Aeolian", "Dorian", "Phrygian", "Harmonic Minor", "Minor Pentatonic",
                                    "Mixolydian", "Lydian", "Locrian" };
const char* const kShortModSourceNames[] = { "Off", "LFO 1", "LFO 2" };
const char* const kShortModDestNames[] = { "Off", "Pitch", "Tone", "Level", "Pan" };
const char* const kLfoShapeNames[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random" };
const char* const kLfoSyncNames[] = { "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T" };
const char* const kModSourceNames[] = { "Off", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Mod Env", "Filter Env", "Velocity", "Mod Lane" };
const char* const kModDestNames[] = { "Off", "Pitch", "Pulse Width", "Wave", "Table Position", "Cutoff", "Resonance",
                                      "Filter Mode", "Filter FM", "Level", "Pan", "Osc 2 Pitch" };
const char* const kVcoNames[] = { "Analog", "Moog 921", "Prophet-5", "Oberheim SEM", "ARP 2600", "E-mu Modular" };
const char* const kFilterNames[] = { "Moog Ladder", "Prophet (SSM2040/CEM3320)", "Juno (IR3109)", "Oberheim SEM",
                                    "Xpander (pole mix)", "Diode Ladder (303)", "Korg35 (MS-20)", "Polivoks", "EDP Wasp", "Comb" };
const char* const kStyleNames[] = { "Cosmic", "Doom", "Melodic", "Modern", "Drift" };
const char* const kMorphNames[] = { "None", "Cosmic", "Doom", "Melodic", "Modern", "Drift" };
const char* const kReverbTypeNames[] = { "Hall", "Plate" };
const char* const kEchoTypeNames[] = { "Tape", "BBD" };
const char* const kEnsembleTypeNames[] = { "Solina", "Chorus", "Wide" };
const char* const kRegistrationNames[] = { "Violins", "Violas", "Cellos", "Basses", "Full", "Hollow", "Brass", "Organ" };
const char* const kRowDivisionNames[] = { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1 Bar", "2 Bars", "4 Bars" };
const char* const kRowDirectionNames[] = { "Forward", "Backward", "Pendulum", "Random Walk" };
const char* const kRowModeNames[] = { "Notes", "Transposer" };
const char* const kEchoTimeNames[] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "1/4T" };
const char* const kTapeSetNames[] = { "Choir", "Strings", "Flute" };

double echoTimeBeats(EchoTime t)
{
    static const double kBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 2.0 / 3.0 };
    const int i = static_cast<int>(t);
    return i >= 0 && i < static_cast<int>(EchoTime::Count) ? kBeats[i] : 0.75;
}

double rowDivisionBeats(RowDivision d)
{
    switch (d) {
    case RowDivision::Quarter:      return 1.0;
    case RowDivision::Eighth:       return 0.5;
    case RowDivision::EighthT:      return 1.0 / 3.0;
    case RowDivision::Sixteenth:    return 0.25;
    case RowDivision::SixteenthT:   return 1.0 / 6.0;
    case RowDivision::ThirtySecond: return 0.125;
    case RowDivision::Bar1:         return 4.0;
    case RowDivision::Bars2:        return 8.0;
    case RowDivision::Bars4:        return 16.0;
    default:                        return 0.25;
    }
}

namespace {

const ParamDesc kComposeParams[compose::Count] = {
    { "bpm",           "Tempo",         "BPM",  60.0f, 160.0f, 118.0f, Curve::Linear },
    { "key",           "Key",           "",      0.0f,  11.0f,   9.0f, Curve::Choice, kKeyNames },
    { "scale",         "Scale",         "",      0.0f,   7.0f,   0.0f, Curve::Choice, kScaleNames },
    { "style",         "Style",         "",      0.0f,   4.0f,   0.0f, Curve::Choice, kStyleNames },
    { "piece_minutes", "Piece Length",  "min",   4.0f,  40.0f,  16.0f, Curve::Linear },
    { "style_tempo",   "Style Tempo",   "",      0.0f,   1.0f,   1.0f, Curve::Toggle },
    { "concert_minutes", "Concert Length", "min", 0.0f, 720.0f,  0.0f, Curve::Linear },   // up to a night of twelve hours
    { "morph_to",      "Morph To",      "",      0.0f,   5.0f,   0.0f, Curve::Choice, kMorphNames },
    { "concert_arc",   "Concert Arc",   "",      0.0f,   1.0f,   0.0f, Curve::Linear },
    { "album",         "Album Form",    "",      0.0f,   1.0f,   0.0f, Curve::Toggle },
    { "night_set",     "Night Set",     "",      0.0f,   1.0f,   0.0f, Curve::Toggle },
    { "pick_sounds",   "Composer's Sounds", "",  0.0f,   1.0f,   1.0f, Curve::Toggle },   // a preset per synth and piece
};

const ParamDesc kRowParams[row::Count] = {
    { "active",    "Active",     "",      0.0f,  1.0f,  0.0f, Curve::Toggle },
    { "length",    "Length",     "steps", 1.0f, 32.0f, 16.0f, Curve::Int },
    { "division",  "Division",   "",      0.0f,  8.0f,  3.0f, Curve::Choice, kRowDivisionNames },
    { "direction", "Direction",  "",      0.0f,  3.0f,  0.0f, Curve::Choice, kRowDirectionNames },
    { "octave",    "Octave",     "",     -3.0f,  3.0f,  0.0f, Curve::Int },
    { "transpose", "Transpose",  "st",  -12.0f, 12.0f,  0.0f, Curve::Int },
    // Chance per cycle that one step changes (the Turing machine's shift register, PLAN 5.1).
    { "mutation",  "Mutation",   "",      0.0f,  1.0f,  0.0f, Curve::Linear },
    { "gate",      "Gate",       "%",     5.0f, 100.0f, 50.0f, Curve::Linear },
    { "level",     "Level",      "dB",  -60.0f,  6.0f, -6.0f, Curve::Linear },
    { "pan",       "Pan",        "",     -1.0f,  1.0f,  0.0f, Curve::Linear },
    { "echo",      "Echo Send",  "",      0.0f,  1.0f,  0.3f, Curve::Linear },
    { "mode",      "Mode",       "",      0.0f,  1.0f,  0.0f, Curve::Choice, kRowModeNames },
    { "reverb",    "Reverb Send", "",     0.0f,  1.0f,  0.3f, Curve::Linear },
    { "echo2",     "Echo 2 Send", "",     0.0f,  1.0f,  0.0f, Curve::Linear },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 30.0f, Curve::Log },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, Curve::Linear },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the blend room (the addon's send B)
    { "punch",    "Punch",    "",    0.0f,   1.0f,   0.0f, Curve::Linear },   // the attacks lifted (a transient shaper)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the early reflections (send A)
    { "sweep",    "Filter Sweep", "oct", 0.0f, 2.0f, 0.0f, Curve::Linear },   // the slow Berlin-School sweep of the cutoff
};

/**
 * The modular voice (PLAN 5.2). Ranges after the instruments the style is played on: a ladder from 30 Hz
 * to 16 kHz, a filter envelope of up to six octaves, decays from a tick of 15 ms to the two-second
 * swell a sequence turns into when the hand opens the decay.
 */
const ParamDesc kVoiceParams[voice::Count] = {
    { "wave",       "Wave",         "",       0.0f,     1.0f,    0.0f, Curve::Linear },   // saw .. pulse
    { "detune",     "Detune",       "ct",     0.0f,    30.0f,    7.0f, Curve::Linear },   // second VCO
    { "pw",         "Pulse Width",  "",       0.05f,    0.5f,    0.5f, Curve::Linear },
    { "drift",      "Drift",        "ct",     0.0f,    15.0f,    3.0f, Curve::Linear },   // stationary spread per VCO
    { "drive",      "Drive",        "dB",     0.0f,    24.0f,    6.0f, Curve::Linear },
    { "cutoff",     "Cutoff",       "Hz",    30.0f, 16000.0f,  600.0f, Curve::Log },
    { "resonance",  "Resonance",    "",       0.0f,     1.0f,    0.35f, Curve::Linear },
    { "env_amount", "Env Amount",   "oct",    0.0f,     6.0f,    2.5f, Curve::Linear },
    { "decay",      "Filter Decay", "ms",    15.0f,  2000.0f,  180.0f, Curve::Log },
    { "keytrack",   "Key Track",    "",       0.0f,     1.0f,    0.5f, Curve::Linear },
    { "accent",     "Accent",       "",       0.0f,     1.0f,    0.5f, Curve::Linear },
    { "amp_decay",  "Amp Release",  "ms",     5.0f,  2000.0f,   60.0f, Curve::Log },
    { "glide",      "Glide",        "ms",     1.0f,   500.0f,   60.0f, Curve::Log },
    { "table",      "Wavetable",    "",       0.0f, static_cast<float>(kWavetableCount), 0.0f, Curve::Choice, kVoiceTableNames },   // 0: analog
    { "table_pos",  "Table Position", "",     0.0f,     1.0f,    0.0f, Curve::Linear },
    { "table_mod",  "Table Mod",    "",       0.0f,     1.0f,    0.4f, Curve::Linear },   // the modulation lane on the position
    { "filter",      "Filter",       "",      0.0f,     9.0f,    0.0f, Curve::Choice, kFilterNames },   // Filters.h
    { "filter_mode", "Filter Mode",  "",      0.0f,     1.0f,    0.0f, Curve::Linear },   // SEM morph, Xpander response, Polivoks band pass, comb sign
    { "filter_fm",   "Filter FM",    "",      0.0f,     1.0f,    0.0f, Curve::Linear },   // oscillator 1 on the cutoff at audio rate
    { "amp_attack",   "Amp Attack",    "ms",   0.5f,  8000.0f, 2.0f, Curve::Log },
    { "amp_decay2",   "Amp Decay",     "ms",   5.0f,  8000.0f, 50.0f, Curve::Log },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, Curve::Linear },
    { "filt_attack",  "Filter Attack", "ms",   0.5f,  8000.0f, 1.5f, Curve::Log },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.0f, Curve::Linear },
    { "filt_release", "Filter Release", "ms",  5.0f,  8000.0f, 180.0f, Curve::Log },
    { "filt_link",    "Release = Decay", "",   0.0f,     1.0f, 1.0f, Curve::Toggle },   // the Minimoog habit: the release takes the decay time
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, Curve::Linear },   // velocity on the filter envelope
    { "mod_attack",   "Mod Attack",    "ms",   0.5f,  8000.0f, 10.0f, Curve::Log },
    { "mod_decay",    "Mod Decay",     "ms",   5.0f,  8000.0f, 500.0f, Curve::Log },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod_release",  "Mod Release",   "ms",   5.0f,  8000.0f, 300.0f, Curve::Log },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.5f, Curve::Log },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 5.0f, Curve::Log },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.07f, Curve::Log },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.2f, Curve::Log },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "vco",        "VCO",          "",      0.0f,     5.0f,   0.0f, Curve::Choice, kVcoNames },   // Vco.h: 0 the voice's own
    { "sync",       "Sync",         "",      0.0f,     1.0f,   0.0f, Curve::Toggle },   // VCO 2 hard-synced to VCO 1
    { "osc2_pitch", "Osc 2 Pitch",  "st",  -12.0f,    24.0f,   0.0f, Curve::Linear },   // VCO 2's interval
    { "cross_mod",  "Cross Mod",    "",      0.0f,     1.0f,   0.0f, Curve::Linear },   // VCO 1 on VCO 2's frequency
};

/**
 * 24.09.2026: the user heard the first study as dull; the cutoffs and envelope depths of the three
 * voices that play first are raised by about two thirds of an octave.
 */
const char* const kVoiceDefaults =
    "voice1.cutoff=650 voice1.resonance=0.4 voice1.env_amount=3.5 voice1.decay=190\n"
    "voice2.wave=0.6 voice2.pw=0.3 voice2.cutoff=1300 voice2.resonance=0.3 voice2.decay=140 voice2.env_amount=2.5\n"
    "voice3.cutoff=1800 voice3.decay=240 voice3.env_amount=2\n";

/** The lead: a brighter, longer, sung version of the voice, with glide and a vibrato that comes in late. */
const ParamDesc kLeadParams[lead::Count] = {
    { "wave",         "Wave",          "",       0.0f,     1.0f,    0.25f, Curve::Linear },
    { "detune",       "Detune",        "ct",     0.0f,    30.0f,    4.0f, Curve::Linear },
    { "pw",           "Pulse Width",   "",       0.05f,    0.5f,    0.45f, Curve::Linear },
    { "drift",        "Drift",         "ct",     0.0f,    15.0f,    2.0f, Curve::Linear },
    { "drive",        "Drive",         "dB",     0.0f,    24.0f,    8.0f, Curve::Linear },
    { "cutoff",       "Cutoff",        "Hz",    30.0f, 16000.0f, 1500.0f, Curve::Log },
    { "resonance",    "Resonance",     "",       0.0f,     1.0f,    0.25f, Curve::Linear },
    { "env_amount",   "Env Amount",    "oct",    0.0f,     6.0f,    2.0f, Curve::Linear },
    { "decay",        "Filter Decay",  "ms",    15.0f,  2000.0f,  450.0f, Curve::Log },
    { "keytrack",     "Key Track",     "",       0.0f,     1.0f,    0.6f, Curve::Linear },
    { "accent",       "Accent",        "",       0.0f,     1.0f,    0.3f, Curve::Linear },
    { "amp_decay",    "Amp Release",   "ms",     5.0f,  2000.0f,  300.0f, Curve::Log },
    { "glide",        "Glide",         "ms",     1.0f,   500.0f,   80.0f, Curve::Log },
    { "level",        "Level",         "dB",   -60.0f,     6.0f,   -9.0f, Curve::Linear },
    { "pan",          "Pan",           "",      -1.0f,     1.0f,    0.15f, Curve::Linear },
    { "echo",         "Echo Send",     "",       0.0f,     1.0f,    0.45f, Curve::Linear },
    { "vibrato",      "Vibrato",       "ct",     0.0f,   100.0f,   18.0f, Curve::Linear },
    { "vibrato_rate", "Vibrato Rate",  "Hz",     1.0f,    10.0f,    5.2f, Curve::Linear },
    { "reverb",       "Reverb Send",   "",       0.0f,     1.0f,    0.4f, Curve::Linear },
    { "auto_pan",     "Auto Pan",      "",       0.0f,     1.0f,    0.0f, Curve::Linear },   // depth of a 0.05 Hz sine on the pan
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 150.0f, Curve::Log },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, Curve::Linear },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, Curve::Linear },   // into the effect hall (send D)
    { "filter",      "Filter",       "",      0.0f,     9.0f,    0.0f, Curve::Choice, kFilterNames },   // Filters.h
    { "filter_mode", "Filter Mode",  "",      0.0f,     1.0f,    0.0f, Curve::Linear },   // SEM morph, Xpander response, Polivoks band pass, comb sign
    { "filter_fm",   "Filter FM",    "",      0.0f,     1.0f,    0.0f, Curve::Linear },   // oscillator 1 on the cutoff at audio rate
    { "amp_attack",   "Amp Attack",    "ms",   0.5f,  8000.0f, 2.0f, Curve::Log },
    { "amp_decay2",   "Amp Decay",     "ms",   5.0f,  8000.0f, 50.0f, Curve::Log },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, Curve::Linear },
    { "filt_attack",  "Filter Attack", "ms",   0.5f,  8000.0f, 1.5f, Curve::Log },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.0f, Curve::Linear },
    { "filt_release", "Filter Release", "ms",  5.0f,  8000.0f, 180.0f, Curve::Log },
    { "filt_link",    "Release = Decay", "",   0.0f,     1.0f, 1.0f, Curve::Toggle },   // the Minimoog habit: the release takes the decay time
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, Curve::Linear },   // velocity on the filter envelope
    { "mod_attack",   "Mod Attack",    "ms",   0.5f,  8000.0f, 10.0f, Curve::Log },
    { "mod_decay",    "Mod Decay",     "ms",   5.0f,  8000.0f, 500.0f, Curve::Log },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod_release",  "Mod Release",   "ms",   5.0f,  8000.0f, 300.0f, Curve::Log },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.5f, Curve::Log },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 5.0f, Curve::Log },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.07f, Curve::Log },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.2f, Curve::Log },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "vco",        "VCO",          "",      0.0f,     5.0f,   0.0f, Curve::Choice, kVcoNames },   // Vco.h: 0 the voice's own
    { "sync",       "Sync",         "",      0.0f,     1.0f,   0.0f, Curve::Toggle },   // VCO 2 hard-synced to VCO 1
    { "osc2_pitch", "Osc 2 Pitch",  "st",  -12.0f,    24.0f,   0.0f, Curve::Linear },   // VCO 2's interval
    { "cross_mod",  "Cross Mod",    "",      0.0f,     1.0f,   0.0f, Curve::Linear },   // VCO 1 on VCO 2's frequency
};

/** The tape keyboard: a first setting of the machine (PLAN 5.4 wants it measured on recordings). */
const ParamDesc kTapeParams[tape::Count] = {
    { "set",     "Tapes",        "",      0.0f,     2.0f,    0.0f, Curve::Choice, kTapeSetNames },
    { "vowel",   "Vowel",        "",      0.0f,     1.0f,    0.2f, Curve::Linear },   // aah .. ooh
    { "wow",     "Wow",          "ct",    0.0f,    30.0f,    6.0f, Curve::Linear },
    { "flutter", "Flutter",      "ct",    0.0f,    10.0f,    2.0f, Curve::Linear },
    { "sag",     "Motor Load",   "ct",    0.0f,     5.0f,    1.0f, Curve::Linear },   // per key beyond the first
    { "tone",    "Tone",         "Hz", 1500.0f, 16000.0f, 7000.0f, Curve::Log },
    { "age",     "Age",          "",      0.0f,     1.0f,    0.5f, Curve::Linear },
    { "level",   "Level",        "dB",  -60.0f,     6.0f,   -1.0f, Curve::Linear },   // 25.09.2026: pads 6..12 dB under the sequence, not 17
    { "pan",     "Pan",          "",     -1.0f,     1.0f,   -0.1f, Curve::Linear },
    { "echo",    "Echo Send",    "",      0.0f,     1.0f,    0.1f, Curve::Linear },
    { "reverb",  "Reverb Send",  "",      0.0f,     1.0f,    0.5f, Curve::Linear },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 150.0f, Curve::Log },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, Curve::Linear },   // 0 near .. 1 the horizon (the addon's macro)
    { "spread",   "Spread",   "",   0.0f,   1.0f,   0.7f, Curve::Linear },   // allpass decorrelation of the mono keyboard
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, Curve::Linear },   // into the effect hall (send D)
    { "amp_attack",   "Swell",         "ms",   1.0f,  8000.0f, 1.0f, Curve::Log },   // at 1 ms: the pressure pad alone
    { "amp_decay",    "Decay",         "ms",   5.0f,  8000.0f, 1500.0f, Curve::Log },
    { "amp_sustain",  "Sustain Level", "",     0.0f,     1.0f, 1.0f, Curve::Linear },
    { "amp_release",  "Release",       "ms",  10.0f,  3000.0f, 70.0f, Curve::Log },   // the fall after the key
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 5.0f, Curve::Log },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 0.3f, Curve::Log },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
};

/** The drone: the lead's table, set dark and slow, with a two-second release and much hall. */
const ParamDesc kDroneParams[lead::Count] = {
    { "wave",         "Wave",          "",       0.0f,     1.0f,    0.0f, Curve::Linear },
    { "detune",       "Detune",        "ct",     0.0f,    30.0f,    0.0f, Curve::Linear },   // the foundation beats not (the addon's 3)
    { "pw",           "Pulse Width",   "",       0.05f,    0.5f,    0.5f, Curve::Linear },
    { "drift",        "Drift",         "ct",     0.0f,    15.0f,    4.0f, Curve::Linear },
    { "drive",        "Drive",         "dB",     0.0f,    24.0f,    4.0f, Curve::Linear },
    { "cutoff",       "Cutoff",        "Hz",    30.0f, 16000.0f, 320.0f, Curve::Log },
    { "resonance",    "Resonance",     "",       0.0f,     1.0f,    0.3f, Curve::Linear },
    { "env_amount",   "Env Amount",    "oct",    0.0f,     6.0f,    0.5f, Curve::Linear },
    { "decay",        "Filter Decay",  "ms",    15.0f,  2000.0f,  900.0f, Curve::Log },
    { "keytrack",     "Key Track",     "",       0.0f,     1.0f,    0.3f, Curve::Linear },
    { "accent",       "Accent",        "",       0.0f,     1.0f,    0.0f, Curve::Linear },
    { "amp_decay",    "Amp Release",   "ms",     5.0f,  2000.0f,  2000.0f, Curve::Log },
    { "glide",        "Glide",         "ms",     1.0f,   500.0f,   300.0f, Curve::Log },
    { "level",        "Level",         "dB",   -60.0f,     6.0f,   -10.0f, Curve::Linear },
    { "pan",          "Pan",           "",      -1.0f,     1.0f,    0.0f, Curve::Linear },
    { "echo",         "Echo Send",     "",       0.0f,     1.0f,    0.1f, Curve::Linear },
    { "vibrato",      "Vibrato",       "ct",     0.0f,   100.0f,   0.0f, Curve::Linear },
    { "vibrato_rate", "Vibrato Rate",  "Hz",     1.0f,    10.0f,    5.0f, Curve::Linear },
    { "reverb",       "Reverb Send",   "",       0.0f,     1.0f,    0.55f, Curve::Linear },
    { "auto_pan",     "Auto Pan",      "",       0.0f,     1.0f,    0.35f, Curve::Linear },   // the drone wanders (7.3)
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 40.0f, Curve::Log },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, Curve::Linear },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, Curve::Linear },   // into the effect hall (send D)
    { "filter",      "Filter",       "",      0.0f,     9.0f,    0.0f, Curve::Choice, kFilterNames },   // Filters.h
    { "filter_mode", "Filter Mode",  "",      0.0f,     1.0f,    0.0f, Curve::Linear },   // SEM morph, Xpander response, Polivoks band pass, comb sign
    { "filter_fm",   "Filter FM",    "",      0.0f,     1.0f,    0.0f, Curve::Linear },   // oscillator 1 on the cutoff at audio rate
    { "amp_attack",   "Amp Attack",    "ms",   0.5f,  8000.0f, 2.0f, Curve::Log },
    { "amp_decay2",   "Amp Decay",     "ms",   5.0f,  8000.0f, 50.0f, Curve::Log },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, Curve::Linear },
    { "filt_attack",  "Filter Attack", "ms",   0.5f,  8000.0f, 1.5f, Curve::Log },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.0f, Curve::Linear },
    { "filt_release", "Filter Release", "ms",  5.0f,  8000.0f, 180.0f, Curve::Log },
    { "filt_link",    "Release = Decay", "",   0.0f,     1.0f, 1.0f, Curve::Toggle },   // the Minimoog habit: the release takes the decay time
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, Curve::Linear },   // velocity on the filter envelope
    { "mod_attack",   "Mod Attack",    "ms",   0.5f,  8000.0f, 10.0f, Curve::Log },
    { "mod_decay",    "Mod Decay",     "ms",   5.0f,  8000.0f, 500.0f, Curve::Log },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod_release",  "Mod Release",   "ms",   5.0f,  8000.0f, 300.0f, Curve::Log },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.5f, Curve::Log },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 5.0f, Curve::Log },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.07f, Curve::Log },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.2f, Curve::Log },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    11.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "vco",        "VCO",          "",      0.0f,     5.0f,   0.0f, Curve::Choice, kVcoNames },   // Vco.h: 0 the voice's own
    { "sync",       "Sync",         "",      0.0f,     1.0f,   0.0f, Curve::Toggle },   // VCO 2 hard-synced to VCO 1
    { "osc2_pitch", "Osc 2 Pitch",  "st",  -12.0f,    24.0f,   0.0f, Curve::Linear },   // VCO 2's interval
    { "cross_mod",  "Cross Mod",    "",      0.0f,     1.0f,   0.0f, Curve::Linear },   // VCO 1 on VCO 2's frequency
};

/** The atmosphere: all layers off until a piece or a hand brings them in. */
const ParamDesc kAtmosParams[atmos::Count] = {
    { "wind",        "Wind",         "dB",  -60.0f,     0.0f,  -60.0f, Curve::Linear },
    { "wind_tone",   "Wind Tone",    "Hz",  150.0f,  4000.0f,  700.0f, Curve::Log },
    { "sweeps",      "Sweeps",       "/min",  0.0f,     6.0f,    0.0f, Curve::Linear },
    { "sweep_level", "Sweep Level",  "dB",  -60.0f,     0.0f,  -18.0f, Curve::Linear },
    { "bleeps",      "Bleeps",       "/min",  0.0f,    12.0f,    0.0f, Curve::Linear },
    { "bleep_level", "Bleep Level",  "dB",  -60.0f,     0.0f,  -22.0f, Curve::Linear },
    { "level",       "Level",        "dB",  -60.0f,     6.0f,    0.0f, Curve::Linear },   // 25.09.2026: the horizon layer, not buried
    { "echo",        "Echo Send",    "",      0.0f,     1.0f,    0.2f, Curve::Linear },
    { "reverb",      "Reverb Send",  "",      0.0f,     1.0f,    0.6f, Curve::Linear },
    { "grains",        "Grains",        "dB",  -60.0f,     0.0f,  -60.0f, Curve::Linear },   // the granular cloud, off at -60
    { "grain_density", "Grain Density", "/s",    1.0f,    40.0f,   12.0f, Curve::Log },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 120.0f, Curve::Log },   // the strip's high pass (production guide 4.2)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, Curve::Linear },   // into the effect hall (send D)
};

/** The string machine: slow in, slow out, the ensemble deep. */
const ParamDesc kStringsParams[strings::Count] = {
    { "attack",   "Crescendo",    "s",    0.005f,  3.0f,   0.35f, Curve::Log },
    { "release",  "Release",      "s",    0.05f,   6.0f,   1.2f, Curve::Log },   // the machines' "sustain" slider
    { "feet",     "Octave Balance", "",   0.0f,    1.0f,   0.4f, Curve::Linear },   // 0 the low footages .. 1 the high ones
    { "tone",     "Tone",         "Hz", 800.0f, 12000.0f, 4500.0f, Curve::Log },
    { "ensemble", "Ensemble",     "",     0.0f,    1.0f,   0.8f, Curve::Linear },
    { "level",    "Level",        "dB", -60.0f,    6.0f, -10.0f, Curve::Linear },
    { "pan",      "Pan",          "",    -1.0f,    1.0f,   0.1f, Curve::Linear },
    { "echo",     "Echo Send",    "",     0.0f,    1.0f,   0.05f, Curve::Linear },
    { "reverb",   "Reverb Send",  "",     0.0f,    1.0f,   0.45f, Curve::Linear },
    // After Waldorf's Streichfett (StringMachine.h): Violins, Violas, Cellos, Basses, Full, Hollow, Brass, Organ.
    { "registration",  "Registration",  "",   0.0f,  7.0f,  0.0f, Curve::Linear, kRegistrationNames },
    { "animate",       "Animate",       "",   0.0f,  1.0f,  0.3f, Curve::Linear },
    { "animate_rate",  "Animate Rate",  "Hz", 0.01f, 1.0f,  0.05f, Curve::Log },
    { "ensemble_type", "Ensemble Type", "",   0.0f,  2.0f,  0.0f, Curve::Choice, kEnsembleTypeNames },
    { "phaser",        "Phaser",        "",   0.0f,  1.0f,  0.0f, Curve::Linear },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 200.0f, Curve::Log },   // the strip's high pass (production guide 4.2)
    { "distance", "Distance", "",   0.0f,   1.0f,   0.0f, Curve::Linear },   // 0 near .. 1 the horizon (the addon's macro)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the early reflections (send A)
    { "shimmer",  "Shimmer Send", "", 0.0f,  1.0f,   0.0f, Curve::Linear },   // into the effect hall (send D)
    { "amp_decay",    "Decay",         "s",    0.05f,   20.0f, 2.0f, Curve::Log },
    { "amp_sustain",  "Sustain Level", "",     0.0f,     1.0f, 1.0f, Curve::Linear },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 5.5f, Curve::Log },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 0.15f, Curve::Log },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     2.0f, 0.0f, Curve::Choice, kShortModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    4.0f, 0.0f, Curve::Choice, kShortModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
};

/** The springs: a short, bright-ish tank under the echo, quiet by default. */
const ParamDesc kSpringParams[spring::Count] = {
    { "decay",  "Decay",  "s",    0.3f,    8.0f,   2.2f, Curve::Log },
    { "tone",   "Tone",   "Hz", 1500.0f, 9000.0f, 4500.0f, Curve::Log },
    { "return", "Return", "dB",  -60.0f,   12.0f,  -6.0f, Curve::Linear },
};

/** The drum kit: an electronic kit of the eighties, sitting back in the mix. */
const ParamDesc kDrumsParams[drums::Count] = {
    { "kick_hz", "Kick Pitch",  "Hz",  35.0f,  80.0f,  50.0f, Curve::Linear },
    { "decay",   "Decay",       "",     0.3f,   2.0f,   1.0f, Curve::Log },
    { "tone",    "Tone",        "",     0.0f,   1.0f,   0.5f, Curve::Linear },
    { "level",   "Level",       "dB", -60.0f,   6.0f,  -9.0f, Curve::Linear },
    { "echo",    "Echo Send",   "",     0.0f,   1.0f,   0.05f, Curve::Linear },
    { "reverb",  "Reverb Send", "",     0.0f,   1.0f,   0.2f, Curve::Linear },
    { "low_cut",  "Low Cut", "Hz", 10.0f, 500.0f, 25.0f, Curve::Log },   // the strip's high pass (production guide 4.2)
    { "blend",    "Blend Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the blend room (the addon's send B)
    { "early",    "Early Send", "",  0.0f,   1.0f,   0.0f, Curve::Linear },   // into the early reflections (send A)
};

const ParamDesc kPerformParams[perform::Count] = {
    { "filter",    "Filter",     "oct", -2.0f,  2.0f, 0.0f, Curve::Linear },
    { "transpose", "Transpose",  "st", -12.0f, 12.0f, 0.0f, Curve::Int },
    { "hold",      "Hold Moves", "",     0.0f,  1.0f, 0.0f, Curve::Toggle },
    { "throw",     "Echo Throw", "",     0.0f,  1.0f, 0.0f, Curve::Linear },
};

const ParamDesc kCueParams[cue::Count] = {
    { "enabled", "OSC Cues", "",  0.0f,     1.0f,    0.0f, Curve::Toggle },
    { "port",    "OSC Port", "",  1024.0f, 65535.0f, 9000.0f, Curve::Int },
};

/** A style of the user's own (Style tab): the defaults are the Cosmic profile's numbers. */
const ParamDesc kCustomParams[custom::Count] = {
    { "use",          "Use Custom Style", "",     0.0f,   1.0f,   0.0f, Curve::Toggle },
    { "bpm_low",      "Tempo Low",        "BPM", 60.0f, 160.0f, 112.0f, Curve::Linear },
    { "bpm_high",     "Tempo High",       "BPM", 60.0f, 160.0f, 126.0f, Curve::Linear },
    { "minutes_low",  "Piece Min Low",    "min",  4.0f,  40.0f,  12.0f, Curve::Linear },
    { "minutes_high", "Piece Min High",   "min",  4.0f,  40.0f,  22.0f, Curve::Linear },
    { "phases_low",   "Phases Low",       "",     1.0f,   4.0f,   1.0f, Curve::Int },
    { "phases_high",  "Phases High",      "",     1.0f,   4.0f,   2.0f, Curve::Int },
    { "intro",        "Atmosphere Share", "",     0.0f,   0.3f,   0.08f, Curve::Linear },
    { "coda",         "Coda Share",       "",     0.0f,   0.3f,   0.10f, Curve::Linear },
    { "new_tempo",    "New Tempo Chance", "",     0.0f,   1.0f,   0.3f, Curve::Linear },
    { "new_key",      "New Key Chance",   "",     0.0f,   1.0f,   0.3f, Curve::Linear },
    { "peak_rows",    "Rows at Peak",     "",     1.0f,   7.0f,   3.0f, Curve::Int },
    { "mutation",     "Mutation",         "",     0.0f,   1.0f,   0.2f, Curve::Linear },
    { "tape",         "Tape Keys Chance", "",     0.0f,   1.0f,   0.9f, Curve::Linear },
    { "strings",      "Strings Chance",   "",     0.0f,   1.0f,   0.5f, Curve::Linear },
    { "lead",         "Lead Chance",      "",     0.0f,   1.0f,   0.6f, Curve::Linear },
    { "bleeps",       "Bleeps Chance",    "",     0.0f,   1.0f,   0.4f, Curve::Linear },
    { "drums",        "Drums Chance",     "",     0.0f,   1.0f,   0.0f, Curve::Linear },
    { "lead_density", "Lead Density",     "",     0.1f,   1.0f,   0.5f, Curve::Linear },
    { "hand_move",    "Move Length",      "s",    2.0f,  30.0f,   8.0f, Curve::Log },
    { "hand_rest",    "Rest Between Moves", "s",    4.0f,  40.0f,  14.0f, Curve::Log },
    { "darkness",     "Darkness",         "",    -0.5f,   0.5f,  -0.05f, Curve::Linear },
    { "hall",         "Hall",             "s",    1.0f,  15.0f,   6.0f, Curve::Log },
    { "level",        "Level",            "dB", -12.0f,  12.0f,   4.8f, Curve::Linear },
};

/** The hall: long and dark, as the style's spaces are (a first setting, to be judged by ear). */
const ParamDesc kReverbParams[reverb::Count] = {
    { "size",     "Size",       "",     0.3f,     3.0f,    1.8f, Curve::Linear },
    { "decay",    "Decay",      "s",    0.3f,    20.0f,    5.5f, Curve::Log },
    { "damping",  "Damping",    "",     0.0f,     1.0f,    0.45f, Curve::Linear },
    { "predelay", "Pre-Delay",  "ms",   0.0f,   200.0f,   25.0f, Curve::Linear },
    { "lowcut",   "Low Cut",    "Hz",  40.0f,   500.0f,  250.0f, Curve::Log },   // the return's EQ (production guide 5.2)
    { "highcut",  "High Cut",   "Hz", 1000.0f, 20000.0f, 6500.0f, Curve::Log },
    { "return",   "Return",     "dB",  -60.0f,   12.0f,    4.0f, Curve::Linear },   // the FDN returns quietly: +4 dB puts the tail ~10 dB under the dry mix
    { "type",     "Room",       "",     0.0f,     1.0f,    0.0f, Curve::Choice, kReverbTypeNames },   // the hall or the plate (Plate.h)
    { "duck",     "Duck",       "dB",   0.0f,    12.0f,    2.0f, Curve::Linear },   // under the rows' notes
};

const ParamDesc kEchoParams[echo::Count] = {
    { "time",      "Time",        "",       0.0f,    6.0f,    2.0f, Curve::Choice, kEchoTimeNames },
    { "feedback",  "Feedback",    "",       0.0f,    1.1f,    0.45f, Curve::Linear },   // above 1: runaway, held by the tape
    { "tone",      "Tone",        "Hz",   800.0f, 12000.0f, 3500.0f, Curve::Log },      // loop low pass
    { "wow",       "Wow",         "ms",     0.0f,    4.0f,    0.2f, Curve::Linear },   // little: the sequence's delay stays in time
    { "flutter",   "Flutter",     "ms",     0.0f,    0.5f,    0.02f, Curve::Linear },
    { "drive",     "Tape Drive",  "dB",     0.0f,   18.0f,    4.0f, Curve::Linear },
    { "pingpong",  "Ping-Pong",   "",       0.0f,    1.0f,    1.0f, Curve::Toggle },
    { "return",    "Return",      "dB",   -60.0f,    6.0f,   -4.0f, Curve::Linear },
    { "type",      "Echo",        "",       0.0f,    1.0f,    0.0f, Curve::Choice, kEchoTypeNames },   // the tape or the BBD (Bbd.h)
    { "low_cut",   "Low Cut",     "Hz",    20.0f, 1000.0f,  200.0f, Curve::Log },   // in the loop: each repeat thinner
    { "duck",      "Duck",        "dB",     0.0f,   12.0f,    3.0f, Curve::Linear }, // under the rows' notes
};

/**
 * The rows start different from each other: a bass row of sixteen sixteenths an octave down, and
 * counter rows of lengths that do not divide sixteen, so that switching one on makes it drift against
 * the first (PLAN 2.2). Only the first is on.
 */
const char* const kDefaultRows =
    "row1.active=1 row1.length=16 row1.division=1/16 row1.octave=-1 row1.pan=0\n"
    "row2.length=13 row2.division=1/16 row2.octave=0 row2.pan=-0.4\n"
    "row3.length=12 row3.division=1/8 row3.octave=1 row3.pan=0.4\n"
    "row4.length=7 row4.division=1/16 row4.octave=1 row4.pan=-0.7\n"
    "row5.length=5 row5.division=1/8 row5.octave=0 row5.pan=0.7\n"
    "row6.length=9 row6.division=1/16 row6.octave=2 row6.pan=-0.2\n"
    "row7.length=24 row7.division=1/8 row7.octave=-1 row7.pan=0.2\n"
    "row8.length=32 row8.division=1/4 row8.octave=0 row8.pan=0\n";

const ParamDesc kMasterParams[master::Count] = {
    { "level",    "Level",    "dB",  -60.0f, 6.0f,  0.0f, Curve::Linear },
    // How much the bus compressor works: 0 off, 1 a ratio of 1.5 from -16 dB. The music keeps its dynamics.
    { "compress", "Compress", "",      0.0f, 1.0f,  0.5f, Curve::Linear },
    { "ceiling",  "Ceiling",  "dBTP", -6.0f, 0.0f, -1.0f, Curve::Linear },
    { "width",    "Width",    "dB",   -6.0f, 6.0f,  4.5f, Curve::Linear },   // the side above 300 Hz; under 100 Hz mono
    { "mono",     "Mono",     "",      0.0f, 1.0f,  0.0f, Curve::Toggle },   // a mono check for listening
    { "cascade",  "Cascade Duck", "dB", 0.0f, 6.0f, 2.5f, Curve::Linear },   // rows -> pads -> atmosphere, 300 Hz .. 5 kHz
    { "motion",   "Motion",   "",      0.0f, 1.0f,  1.0f, Curve::Linear },   // slow movements with irrational periods
    { "sub_solo", "Sub Solo", "",      0.0f, 1.0f,  0.0f, Curve::Toggle },   // an 80 Hz low pass on the sum, for listening
    { "clip",        "Soft Clip",    "dB", 0.0f, 2.0f,  0.75f, Curve::Linear },   // what a soft clipper may take off the peaks
    { "sub_ceiling", "Sub Ceiling",  "dBFS", -18.0f, 0.0f, -6.0f, Curve::Linear }, // a limiter on the band under 80 Hz
    { "tame",     "Resonance Tamer", "", 0.0f, 1.0f,  0.6f, Curve::Linear },   // the rows' bus: what sticks out 6 dB over the rest
};

/** @brief One module: its prefix, table, how many instances exist, and optionally their names. */
struct ModuleSpec {
    const char* prefix;
    const ParamDesc* descs;
    int count;
    int instances;
    const char* const* instanceNames = nullptr;   ///< prefixes of the instances instead of prefix + number
};

/** The second echo (Engine: its own tape echo, a little wow, no springs). */
const ParamDesc kEcho2Params[echo2::Count] = {
    { "time",      "Echo 2 Time",     "",       0.0f,    6.0f,    1.0f, Curve::Choice, kEchoTimeNames },
    { "feedback",  "Echo 2 Feedback", "",       0.0f,    1.0f,    0.3f, Curve::Linear },
    { "tone",      "Echo 2 Tone",     "Hz",   800.0f, 12000.0f, 5000.0f, Curve::Log },
    { "pingpong",  "Echo 2 Ping-Pong", "",      0.0f,    1.0f,    1.0f, Curve::Toggle },
    { "return",    "Echo 2 Return",   "dB",   -60.0f,    6.0f,   -6.0f, Curve::Linear },
    { "low_cut",   "Echo 2 Low Cut",  "Hz",  20.0f, 1000.0f,  300.0f, Curve::Log },
};

/** The blend room (Engine: a second plate, short; its return feeds the hall). */
const ParamDesc kBlendParams[blend::Count] = {
    { "decay",     "Blend Decay",     "s",     0.3f,     4.0f,    1.2f, Curve::Log },
    { "predelay",  "Blend Pre-Delay", "ms",    0.0f,    60.0f,    8.0f, Curve::Linear },
    { "low_cut",   "Blend Low Cut",   "Hz",   60.0f,   800.0f,  240.0f, Curve::Log },
    { "high_cut",  "Blend High Cut",  "Hz", 1500.0f, 16000.0f, 7000.0f, Curve::Log },
    { "damping",   "Blend Damping",   "",      0.0f,     1.0f,    0.4f, Curve::Linear },
    { "return",    "Blend Return",    "dB",  -60.0f,     6.0f,   -2.0f, Curve::Linear },
    { "into_hall", "Into Hall",       "",      0.0f,     0.5f,   0.15f, Curve::Linear },   // the serial far space
};

/** The early reflections (send A) and the effect hall (send D) of the production guide (fx/Rooms.h). */
const ParamDesc kEarlyParams[early::Count] = {
    { "size",      "Early Size",      "ms",   10.0f,    80.0f,   35.0f, Curve::Linear },
    { "low_cut",   "Early Low Cut",   "Hz",   60.0f,   800.0f,  200.0f, Curve::Log },
    { "high_cut",  "Early High Cut",  "Hz", 2000.0f, 16000.0f, 8000.0f, Curve::Log },
    { "return",    "Early Return",    "dB",  -60.0f,     6.0f,   -4.0f, Curve::Linear },
};
const ParamDesc kShimmerParams[shimmer::Count] = {
    { "decay",     "Shimmer Decay",   "s",     2.0f,    20.0f,    8.0f, Curve::Log },
    { "amount",    "Shimmer",         "",      0.0f,     0.6f,   0.35f, Curve::Linear },   // the octave fed back
    { "low_cut",   "Shimmer Low Cut", "Hz",  100.0f,  1500.0f,  400.0f, Curve::Log },
    { "high_cut",  "Shimmer High Cut","Hz", 1500.0f, 12000.0f, 4000.0f, Curve::Log },
    { "return",    "Shimmer Return",  "dB",  -60.0f,     6.0f,   -8.0f, Curve::Linear },
};

/** The pad synth (Poly.h): a wavetable (PWM by default: the analog pad), its slow scan, two detuned oscillators spread
 *  across the field, a 12 dB low pass, a slow envelope and the ensemble. */
const ParamDesc kPolyParams[poly::Count] = {
    { "table",      "Wavetable",    "",      0.0f, static_cast<float>(kWavetableCount - 1), 1.0f, Curve::Choice, kWavetableNames },
    { "position",   "Position",     "",      0.0f,     1.0f,   0.3f, Curve::Linear },   // across the table's frames
    { "scan",       "Scan",         "",      0.0f,     1.0f,   0.35f, Curve::Linear },  // the slow sine on the position
    { "scan_rate",  "Scan Rate",    "Hz",    0.005f,   1.0f,   0.06f, Curve::Log },
    { "detune",     "Detune",       "ct",    0.0f,    30.0f,   8.0f, Curve::Linear },   // the two oscillators apart
    { "spread",     "Spread",       "",      0.0f,     1.0f,   0.6f, Curve::Linear },   // ... and across the field
    { "drift",      "Drift",        "ct",    0.0f,    12.0f,   3.0f, Curve::Linear },
    { "cutoff",     "Cutoff",       "Hz",  100.0f, 12000.0f, 2200.0f, Curve::Log },
    { "resonance",  "Resonance",    "",      0.0f,     1.0f,   0.15f, Curve::Linear },
    { "env_amount", "Env Amount",   "oct",   0.0f,     4.0f,   1.0f, Curve::Linear },
    { "attack",     "Attack",       "s",     0.01f,    8.0f,   1.5f, Curve::Log },
    { "release",    "Release",      "s",     0.1f,    10.0f,   4.0f, Curve::Log },
    { "chorus",     "Ensemble",     "",      0.0f,     1.0f,   0.5f, Curve::Linear },
    { "level",      "Level",        "dB",  -60.0f,     6.0f,  -4.0f, Curve::Linear },   // measured: its stem 8-10 dB under the main sequence, beside the strings
    { "pan",        "Pan",          "",     -1.0f,     1.0f,  -0.1f, Curve::Linear },
    { "echo",       "Echo Send",    "",      0.0f,     1.0f,   0.0f, Curve::Linear },
    { "reverb",     "Reverb Send",  "",      0.0f,     1.0f,   0.5f, Curve::Linear },
    { "low_cut",    "Low Cut",      "Hz",   10.0f,   500.0f, 180.0f, Curve::Log },    // the production guide's 4.2: pads from 120-200 Hz
    { "distance",   "Distance",     "",      0.0f,     1.0f,   0.0f, Curve::Linear },
    { "blend",      "Blend Send",   "",      0.0f,     1.0f,   0.0f, Curve::Linear },
    { "early",      "Early Send",   "",      0.0f,     1.0f,   0.0f, Curve::Linear },
    { "shimmer",    "Shimmer Send", "",      0.0f,     1.0f,   0.1f, Curve::Linear },
    { "filter",     "Filter",       "",      0.0f,     9.0f,   3.0f, Curve::Choice, kFilterNames },   // the SEM by default
    { "filter_mode","Filter Mode",  "",      0.0f,     1.0f,   0.0f, Curve::Linear },
    { "amp_decay",    "Amp Decay",     "s",    0.01f,   20.0f, 1.0f, Curve::Log },
    { "amp_sustain",  "Amp Sustain",   "",     0.0f,     1.0f, 1.0f, Curve::Linear },
    { "filt_link",    "Filter Follows Amp", "", 0.0f,     1.0f, 1.0f, Curve::Toggle },   // the filter follows the amplitude envelope
    { "filt_attack",  "Filter Attack", "s",    0.01f,   20.0f, 2.0f, Curve::Log },
    { "filt_decay",   "Filter Decay",  "s",    0.01f,   20.0f, 3.0f, Curve::Log },
    { "filt_sustain", "Filter Sustain", "",    0.0f,     1.0f, 0.4f, Curve::Linear },
    { "filt_release", "Filter Release", "s",   0.01f,   20.0f, 4.0f, Curve::Log },
    { "env_velocity", "Env Velocity",  "",     0.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod_attack",   "Mod Attack",    "s",    0.01f,   20.0f, 1.0f, Curve::Log },
    { "mod_decay",    "Mod Decay",     "s",    0.01f,   20.0f, 2.0f, Curve::Log },
    { "mod_sustain",  "Mod Sustain",   "",     0.0f,     1.0f, 0.5f, Curve::Linear },
    { "mod_release",  "Mod Release",   "s",    0.01f,   20.0f, 3.0f, Curve::Log },
    { "lfo1_rate",    "LFO 1 Rate",    "Hz",   0.01f,   30.0f, 0.2f, Curve::Log },
    { "lfo1_shape",   "LFO 1 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo1_sync",    "LFO 1 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo1_retrig",  "LFO 1 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo1_fade",    "LFO 1 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo2_rate",    "LFO 2 Rate",    "Hz",   0.01f,   30.0f, 4.5f, Curve::Log },
    { "lfo2_shape",   "LFO 2 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo2_sync",    "LFO 2 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo2_retrig",  "LFO 2 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo2_fade",    "LFO 2 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo3_rate",    "LFO 3 Rate",    "Hz",   0.01f,   30.0f, 0.05f, Curve::Log },
    { "lfo3_shape",   "LFO 3 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo3_sync",    "LFO 3 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo3_retrig",  "LFO 3 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo3_fade",    "LFO 3 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "lfo4_rate",    "LFO 4 Rate",    "Hz",   0.01f,   30.0f, 0.11f, Curve::Log },
    { "lfo4_shape",   "LFO 4 Shape",   "",     0.0f,     6.0f, 0.0f, Curve::Choice, kLfoShapeNames },
    { "lfo4_sync",    "LFO 4 Sync",    "",     0.0f,     9.0f, 0.0f, Curve::Choice, kLfoSyncNames },
    { "lfo4_retrig",  "LFO 4 Retrig",  "",     0.0f,     1.0f, 0.0f, Curve::Toggle },
    { "lfo4_fade",    "LFO 4 Fade",    "s",    0.0f,    10.0f, 0.0f, Curve::Linear },
    { "mod1_src",     "Mod 1 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod1_dst",     "Mod 1 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod1_amt",     "Mod 1 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod2_src",     "Mod 2 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod2_dst",     "Mod 2 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod2_amt",     "Mod 2 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod3_src",     "Mod 3 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod3_dst",     "Mod 3 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod3_amt",     "Mod 3 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod4_src",     "Mod 4 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod4_dst",     "Mod 4 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod4_amt",     "Mod 4 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod5_src",     "Mod 5 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod5_dst",     "Mod 5 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod5_amt",     "Mod 5 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod6_src",     "Mod 6 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod6_dst",     "Mod 6 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod6_amt",     "Mod 6 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod7_src",     "Mod 7 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod7_dst",     "Mod 7 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod7_amt",     "Mod 7 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
    { "mod8_src",     "Mod 8 Source",  "",     0.0f,     8.0f, 0.0f, Curve::Choice, kModSourceNames },
    { "mod8_dst",     "Mod 8 Target",  "",     0.0f,    10.0f, 0.0f, Curve::Choice, kModDestNames },
    { "mod8_amt",     "Mod 8 Amount",  "",    -1.0f,     1.0f, 0.0f, Curve::Linear },
};

const ModuleSpec kModules[static_cast<int>(Module::Count)] = {
    { "compose", kComposeParams, compose::Count, 1 },
    { "row",     kRowParams,     row::Count,     kRows },
    { "master",  kMasterParams,  master::Count,  1 },
    { "voice",   kVoiceParams,   voice::Count,   kRows },
    { "echo",    kEchoParams,    echo::Count,    1 },
    { "lead",    kLeadParams,    lead::Count,    1 },
    { "reverb",  kReverbParams,  reverb::Count,  1 },
    { "tape",    kTapeParams,    tape::Count,    1 },
    { "drone",   kDroneParams,   lead::Count,    1 },
    { "atmos",   kAtmosParams,   atmos::Count,   1 },
    { "strings", kStringsParams, strings::Count, 1 },
    { "spring",  kSpringParams,  spring::Count,  1 },
    { "drums",   kDrumsParams,   drums::Count,   1 },
    { "perform", kPerformParams, perform::Count, 1 },
    { "cue",     kCueParams,     cue::Count,     1 },
    { "custom",  kCustomParams,  custom::Count,  1 },
    { "delay",   kEcho2Params,   echo2::Count,   1 },
    { "blend",   kBlendParams,   blend::Count,   1 },   // the blend room
    { "early",   kEarlyParams,   early::Count,   1 },   // send A
    { "shimmer", kShimmerParams, shimmer::Count, 1 },
    { "poly",    kPolyParams,    poly::Count,    1 },   // the pad synth   // send D   // the second echo (a prefix without a digit: "row1" is an instance)
};

bool isDiscrete(Curve c) { return c == Curve::Int || c == Curve::Choice || c == Curve::Toggle; }

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

} // namespace

ParamStore::ParamStore()
{
    for (auto& row : bases_) for (int& b : row) b = -1;
    int total = 0;
    for (const ModuleSpec& m : kModules) total += m.count * m.instances;
    entries_.reserve(static_cast<size_t>(total));
    for (int mi = 0; mi < static_cast<int>(Module::Count); ++mi) {
        const ModuleSpec& m = kModules[mi];
        for (int inst = 0; inst < m.instances && inst < kMaxInstances; ++inst) {
            bases_[mi][inst] = static_cast<int>(entries_.size());
            std::string prefix = m.instanceNames != nullptr ? std::string(m.instanceNames[inst]) : std::string(m.prefix);
            if (m.instances > 1 && m.instanceNames == nullptr) prefix += std::to_string(inst + 1);
            for (int p = 0; p < m.count; ++p) {
                Entry e{ &m.descs[p], prefix + "." + m.descs[p].key, static_cast<Module>(mi), inst };
                index_.emplace(e.key, static_cast<int>(entries_.size()));
                entries_.push_back(std::move(e));
            }
        }
    }
    values_ = std::make_unique<std::atomic<float>[]>(entries_.size());
    // Defaults: the descriptors', then the instance-specific ones of the rows on top.
    defaults_.resize(entries_.size());
    for (int i = 0; i < count(); ++i) {
        defaults_[static_cast<size_t>(i)] = desc(i).defValue;
        values_[static_cast<size_t>(i)].store(desc(i).defValue, std::memory_order_relaxed);
    }
    parseText(kDefaultRows);
    parseText(kVoiceDefaults);
    for (int i = 0; i < count(); ++i) defaults_[static_cast<size_t>(i)] = get(i);
}

int ParamStore::base(Module m, int instance) const
{
    const int mi = static_cast<int>(m);
    if (mi < 0 || mi >= static_cast<int>(Module::Count) || instance < 0 || instance >= kMaxInstances) return -1;
    return bases_[mi][instance];
}

int ParamStore::find(std::string_view key) const
{
    const auto it = index_.find(std::string(key));
    return it == index_.end() ? -1 : it->second;
}

int ParamStore::getInt(int id) const
{
    return static_cast<int>(std::lround(get(id)));
}

void ParamStore::set(int id, float value)
{
    if (id < 0 || id >= count()) return;
    const ParamDesc& d = desc(id);
    if (!(value == value)) value = defaults_.empty() ? d.defValue : defaults_[static_cast<size_t>(id)];   // NaN
    float v = value < d.minValue ? d.minValue : (value > d.maxValue ? d.maxValue : value);
    if (isDiscrete(d.curve)) v = std::round(v);
    values_[static_cast<size_t>(id)].store(v, std::memory_order_relaxed);
}

float ParamStore::toNormalised(int id, float value) const
{
    const ParamDesc& d = desc(id);
    if (d.maxValue <= d.minValue) return 0.0f;
    float n;
    if (d.curve == Curve::Log) n = std::log(value / d.minValue) / std::log(d.maxValue / d.minValue);
    else n = (value - d.minValue) / (d.maxValue - d.minValue);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

float ParamStore::fromNormalised(int id, float norm) const
{
    const ParamDesc& d = desc(id);
    const float n = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    float v;
    if (d.curve == Curve::Log) v = d.minValue * std::pow(d.maxValue / d.minValue, n);
    else v = d.minValue + n * (d.maxValue - d.minValue);
    if (isDiscrete(d.curve)) v = std::round(v);
    return v;
}

void ParamStore::resetDefaults()
{
    for (int i = 0; i < count(); ++i) values_[static_cast<size_t>(i)].store(defaults_[static_cast<size_t>(i)], std::memory_order_relaxed);
}

int ParamStore::moduleCount(Module m)
{
    const int mi = static_cast<int>(m);
    return mi >= 0 && mi < static_cast<int>(Module::Count) ? kModules[mi].count : 0;
}

void ParamStore::readModule(Module m, int instance, float* out) const
{
    const int b = base(m, instance);
    if (b < 0) return;
    const int n = moduleCount(m);
    for (int i = 0; i < n; ++i) out[i] = get(b + i);
}

void ParamStore::copyValuesFrom(const ParamStore& other)
{
    const int n = count() < other.count() ? count() : other.count();
    for (int i = 0; i < n; ++i) values_[static_cast<size_t>(i)].store(other.get(i), std::memory_order_relaxed);
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
        if (d.choices != nullptr || d.curve == Curve::Toggle) {
            const std::string fv = foldName(v);
            if (d.curve == Curve::Toggle && (fv == "on" || fv == "off")) { set(id, fv == "on" ? 1.0f : 0.0f); matched = true; }
            for (int c = 0; !matched && d.choices != nullptr && c <= static_cast<int>(d.maxValue); ++c) {
                if (fv == foldName(d.choices[c])) { set(id, static_cast<float>(c)); matched = true; }
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
        if (onlyChanged && v == defaults_[static_cast<size_t>(i)]) continue;
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
    const int last = static_cast<int>(d.maxValue);
    const float c = std::clamp(v, d.minValue, d.maxValue);
    const int lo = std::clamp(static_cast<int>(std::floor(c)), 0, last);
    const float frac = c - static_cast<float>(lo);
    if (frac < 0.1f || lo == last) return d.choices[lo];
    if (frac > 0.9f) return d.choices[lo + 1];
    return std::string(d.choices[lo]) + "/" + d.choices[lo + 1];
}

std::string ParamStore::format(int id) const
{
    const ParamDesc& d = desc(id);
    const float v = get(id);
    if (d.curve == Curve::Choice && d.choices != nullptr) return d.choices[getInt(id)];
    if (d.curve == Curve::Toggle) return v >= 0.5f ? "On" : "Off";
    if (d.curve == Curve::Linear && d.choices != nullptr) return morphText(d, v);
    char buf[64];
    if (d.curve == Curve::Int) std::snprintf(buf, sizeof(buf), "%d", getInt(id));
    else if (std::fabs(v) >= 100.0f) std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(v));
    else if (std::fabs(v) >= 10.0f) std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    else std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    std::string s = buf;
    if (d.unit != nullptr && d.unit[0] != 0) { s += ' '; s += d.unit; }
    return s;
}

} // namespace eph
