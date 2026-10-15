/**
 * @file Params.h
 * @brief The parameter system: descriptor tables per module, instantiated in blocks.
 *
 * A module declares its parameters once as a table of descriptors and may exist several times --
 * the eight rows of the sequencer rack are one module in eight instances. A parameter's id is the
 * base index of its module instance plus its index in the table; its text key is "\<prefix\>.\<key\>"
 * or "\<prefix\>\<instance\>.\<key\>" ("compose.bpm", "row3.length").
 *
 * From the same tables come the host parameters, OSC addresses, the preset text form, the manual and
 * the `--list` output of eph_render. The composer never writes parameters; it reads a snapshot, and
 * what it plays on a knob it writes into the score as a gesture (Score.h).
 *
 * Values are stored as std::atomic\<float\> in their real range (Hz, ms, dB), so the audio thread can
 * read what another thread wrote without a lock.
 *
 * @note The store (ParamStore) is copied from Phosphene `Core/include/phos/Params.h` at 9a2f615
 *       (24.09.2026); the module tables are Ephemeris's own. Rule kept from Phosphene: a table is only
 *       ever appended to, never reordered, because the indices sit in presets, `.ephset` files and the
 *       plugin's state.
 * @author Rene Weller
 */
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eph
{

/** @brief How a parameter maps between its real value and the normalised 0..1 of a knob. */
enum class CurveE : uint8_t
{
    LINEAR,   ///< proportional
    LOG,      ///< logarithmic; minimum must be > 0
    INT,      ///< integer steps, linear
    CHOICE,   ///< integer index into a list of names
    TOGGLE,   ///< 0 or 1
};

/** @brief Static description of one parameter. */
struct ParamDesc
{
    const char        *m_key;                 ///< identifier inside the module, snake_case
    const char        *m_name;                ///< display name (English)
    const char        *m_unit;                ///< unit for display, may be empty
    float              m_minValue;            ///< lowest real value
    float              m_maxValue;            ///< highest real value
    float              m_defValue;            ///< default real value
    CurveE             m_curve;               ///< mapping to the knob
    const char *const *m_choices = nullptr;   ///< names for Curve::Choice (maxValue + 1 entries); on Curve::Linear
                                              ///< the names of the whole numbers a morph passes (strings.registration)
};

/**
 * @brief The text of a morph between named whole numbers: the nearest name, or "Violins/Violas" between two.
 * @param[in] d  the parameter's description (its names)
 * @param[in] v  the value
 * @return the nearest name, or two names joined by a slash
 */
std::string morphText( const ParamDesc &d, float v );

/** @brief The modules that own parameters. Appended to, never reordered. */
enum class ModuleE : int
{
    COMPOSE = 0,
    ROW,
    MASTER,
    /** Phase 1: the modular voice of each row, and the tape echo. */
    VOICE,
    ECHO,
    /** Phase 2: the lead voice. */
    LEAD,
    /** Phase 3: the hall. */
    REVERB,
    /** Phase 3: the tape keyboard. */
    TAPE,
    /** Phase 3: the drone (the lead's table) and the atmosphere. */
    DRONE,
    ATMOS,
    /** Phase 3: the string machine. */
    STRINGS,
    /** Phase 3: the springs of the tape echo. */
    SPRING,
    /** Phase 4: the drum kit. */
    DRUMS,
    /** Phase 5: the performer's hands on the running piece (PLAN 8.1, Perform). */
    PERFORM,
    /** Phase 5: the score cues for a visualiser (PLAN 8.3, Cue.h). */
    CUE,
    /** Phase 5: a style profile of the user's own (the Style tab; Style.h, customProfile). */
    CUSTOM,
    /** 25.09.2026: a second echo, its own time for the counter rows (the style guide's 4.4, 4.5). */
    ECHO2,
    /** 25.09.2026: the blend room, a short plate before the hall (the addon's serial far space). */
    BLEND,
    /** 25.09.2026: the early reflections (send A) and the effect hall (send D) of the production guide. */
    EARLY,
    SHIMMER,
    /** 25.09.2026: the pad synth, a polyphonic wavetable synth (Poly.h). */
    POLY,
    COUNT
};

constexpr int Rows = 8;   ///< instances of the row module (the rows of the rack) and of the voice module

/** @brief Parameters of the composer (read as a snapshot when a piece is planned). */
namespace compose
{
enum : int
{
    BPM,
    KEY,
    SCALE,
    STYLE,
    PIECE_MINUTES,
    // Phase 4: the profile draws the tempo (off: compose.bpm), and a concert's length (0: one piece).
    STYLE_TEMPO,
    CONCERT_MINUTES,
    // Phase 5: a concert that wanders from its style to another (0: none, else the style + 1), and the
    // strength of the arc of tension over a whole concert (0: none).
    MORPH_TO,
    CONCERT_ARC,
    // 25.09.2026: a concert as an album (the style guide's 6.5): interludes, the darkest piece in the middle.
    ALBUM,
    // 25.09.2026: a concert as a night set (composeNightSet): the styles mixed along waves of energy, the
    // pieces overlapping as a DJ mixes them.
    NIGHT_SET,
    // 25.09.2026: the composer chooses a factory preset for every synth in every piece (off: your knobs).
    PICK_SOUNDS,
    COUNT
};
}
/** @brief Parameters of one row of the sequencer rack (module Row, "row1" .. "row8"). */
namespace row
{
enum : int
{
    ACTIVE,
    LENGTH,
    DIVISION,
    DIRECTION,
    OCTAVE,
    TRANSPOSE,
    MUTATION,
    GATE,
    LEVEL,
    PAN,
    // Phase 1: the send into the tape echo.
    ECHO_SEND,
    // Phase 2: what the row does -- plays notes, or transposes the rows that play (PLAN 5.1).
    MODE,
    // Phase 3: the send into the hall.
    REVERB_SEND,
    // 25.09.2026: the send into the second echo; the strip's low cut (the production guide's 4.2).
    ECHO2_SEND,
    LOW_CUT,
    // 25.09.2026 (the addon's distance macro): 0 near .. 1 the horizon, level, highs and hall together.
    DISTANCE,
    // 25.09.2026: the send into the blend room; the transient punch (the production guide's 7.5).
    BLEND_SEND,
    PUNCH,
    EARLY_SEND,
    // 25.09.2026: the row's filter sweep, a slow sine on its cutoff (octaves; its own period of 39 .. 79 s).
    SWEEP,
    COUNT
};
}
/** @brief Parameters of the master section. */
namespace master
{
enum : int
{
    LEVEL,
    // Phase 3: a gentle bus compressor and a true-peak limiter as protection (PLAN 5.8).
    COMPRESS,
    CEILING,
    // 25.09.2026 (the production guide's 6.3, 6.4): the side above 300 Hz in dB (under 100 Hz it is always
    // mono), and a mono switch for listening.
    WIDTH,
    MONO,
    // 25.09.2026 (the addon): the rows duck the pads, the pads the atmosphere, in the band 300 Hz .. 5 kHz (dB);
    // the slow movements of pads, rooms and atmosphere (0 still .. 1); a sub solo (80 Hz low pass) for listening.
    CASCADE,
    MOTION,
    SUB_SOLO,
    // 25.09.2026 (the production guide's 7.4, the addon's 5): a soft clipper before the limiter (dB it may take
    // off the peaks), and a limiter on the band under 80 Hz (its ceiling in dBFS).
    CLIP,
    SUB_CEILING,
    // 25.09.2026: the resonance suppressor on the rows' bus (0 off .. 1 a third of what sticks out stays).
    TAME,
    COUNT
};
}
/**
 * @brief Parameters of one modular voice (module Voice, "voice1" .. "voice8", one per row; PLAN 5.2).
 *
 * Cutoff, resonance, filter decay and the envelope amount are the knobs a player's hand moves; the
 * gestures of the score (Score.h) are offsets on exactly these.
 */
namespace voice
{
enum : int
{
    WAVE,
    DETUNE,
    PULSE_WIDTH,
    DRIFT,
    DRIVE,
    CUTOFF,
    RESONANCE,
    ENV_AMOUNT,
    DECAY,
    KEY_TRACK,
    ACCENT,
    AMP_DECAY,
    GLIDE,
    // 25.09.2026: a wavetable instead of the analog oscillators (0: analog), where in it, and how far the
    // row's modulation lane moves that place from note to note (Stürtzer's Iridium sequences). The rows only:
    // the lead's and the drone's tables go on differently from here.
    TABLE,
    TABLE_POS,
    TABLE_MOD,
    // 26.09.2026: the filter model (Filters.h), its mode, and filter FM from oscillator 1.
    FILTER,
    FILTER_MODE,
    FILTER_FM,
    // 26.09.2026, modulation (ModVoice.h, Modulation.h): the envelopes in full, a modulation envelope, four LFOs,
    // eight slots of a modulation matrix.
    AMP_ATTACK,
    AMP_DECAY2,
    AMP_SUSTAIN,
    FILT_ATTACK,
    FILT_SUSTAIN,
    FILT_RELEASE,
    FILT_LINK,
    ENV_VELOCITY,
    MOD_ATTACK,
    MOD_DECAY,
    MOD_SUSTAIN,
    MOD_RELEASE,
    LFO1_RATE,
    LFO1_SHAPE,
    LFO1_SYNC,
    LFO1_RETRIG,
    LFO1_FADE,
    LFO2_RATE,
    LFO2_SHAPE,
    LFO2_SYNC,
    LFO2_RETRIG,
    LFO2_FADE,
    LFO3_RATE,
    LFO3_SHAPE,
    LFO3_SYNC,
    LFO3_RETRIG,
    LFO3_FADE,
    LFO4_RATE,
    LFO4_SHAPE,
    LFO4_SYNC,
    LFO4_RETRIG,
    LFO4_FADE,
    MOD1_SRC,
    MOD1_DST,
    MOD1_AMT,
    MOD2_SRC,
    MOD2_DST,
    MOD2_AMT,
    MOD3_SRC,
    MOD3_DST,
    MOD3_AMT,
    MOD4_SRC,
    MOD4_DST,
    MOD4_AMT,
    MOD5_SRC,
    MOD5_DST,
    MOD5_AMT,
    MOD6_SRC,
    MOD6_DST,
    MOD6_AMT,
    MOD7_SRC,
    MOD7_DST,
    MOD7_AMT,
    MOD8_SRC,
    MOD8_DST,
    MOD8_AMT,
    // 26.09.2026, the classic VCOs (Vco.h): the model, VCO 2 hard-synced to VCO 1, VCO 2's interval, VCO 1 on
    // VCO 2's frequency.
    VCO,
    SYNC,
    OSC2_PITCH,
    CROSS_MOD,
    COUNT
};
}
/**
 * @brief Parameters of the lead (module Lead, PLAN 5.3): the voice's table, then its place in the mix
 *        and the vibrato a player brings in with the wheel.
 */
namespace lead
{
enum : int
{
    WAVE,
    DETUNE,
    PULSE_WIDTH,
    DRIFT,
    DRIVE,
    CUTOFF,
    RESONANCE,
    ENV_AMOUNT,
    DECAY,
    KEY_TRACK,
    ACCENT,
    AMP_DECAY,
    GLIDE,
    LEVEL,
    PAN,
    ECHO_SEND,
    VIBRATO,
    VIBRATO_RATE,
    // Phase 3: the send into the hall.
    REVERB_SEND,
    // 25.09.2026: a slow sine (0.05 Hz) on the pan, the wandering drone of the style guide's 7.3.
    AUTO_PAN,
    LOW_CUT,
    DISTANCE,
    BLEND_SEND,
    EARLY_SEND,
    SHIMMER_SEND,
    // 26.09.2026: the filter model (Filters.h), its mode, and filter FM from oscillator 1.
    FILTER,
    FILTER_MODE,
    FILTER_FM,
    // 26.09.2026, modulation (ModVoice.h, Modulation.h): the envelopes in full, a modulation envelope, four LFOs,
    // eight slots of a modulation matrix.
    AMP_ATTACK,
    AMP_DECAY2,
    AMP_SUSTAIN,
    FILT_ATTACK,
    FILT_SUSTAIN,
    FILT_RELEASE,
    FILT_LINK,
    ENV_VELOCITY,
    MOD_ATTACK,
    MOD_DECAY,
    MOD_SUSTAIN,
    MOD_RELEASE,
    LFO1_RATE,
    LFO1_SHAPE,
    LFO1_SYNC,
    LFO1_RETRIG,
    LFO1_FADE,
    LFO2_RATE,
    LFO2_SHAPE,
    LFO2_SYNC,
    LFO2_RETRIG,
    LFO2_FADE,
    LFO3_RATE,
    LFO3_SHAPE,
    LFO3_SYNC,
    LFO3_RETRIG,
    LFO3_FADE,
    LFO4_RATE,
    LFO4_SHAPE,
    LFO4_SYNC,
    LFO4_RETRIG,
    LFO4_FADE,
    MOD1_SRC,
    MOD1_DST,
    MOD1_AMT,
    MOD2_SRC,
    MOD2_DST,
    MOD2_AMT,
    MOD3_SRC,
    MOD3_DST,
    MOD3_AMT,
    MOD4_SRC,
    MOD4_DST,
    MOD4_AMT,
    MOD5_SRC,
    MOD5_DST,
    MOD5_AMT,
    MOD6_SRC,
    MOD6_DST,
    MOD6_AMT,
    MOD7_SRC,
    MOD7_DST,
    MOD7_AMT,
    MOD8_SRC,
    MOD8_DST,
    MOD8_AMT,
    // 26.09.2026, the classic VCOs (Vco.h): the model, VCO 2 hard-synced to VCO 1, VCO 2's interval, VCO 1 on
    // VCO 2's frequency.
    VCO,
    SYNC,
    OSC2_PITCH,
    CROSS_MOD,
    COUNT
};
}
/** @brief Parameters of the pad synth (module Poly, 25.09.2026; Poly.h, Wavetable.h). */
namespace poly
{
enum : int
{
    TABLE,
    POSITION,
    SCAN,
    SCAN_RATE,
    DETUNE,
    SPREAD,
    DRIFT,
    CUTOFF,
    RESONANCE,
    ENV_AMOUNT,
    ATTACK,
    RELEASE,
    CHORUS,
    LEVEL,
    PAN,
    ECHO_SEND,
    REVERB_SEND,
    LOW_CUT,
    DISTANCE,
    BLEND_SEND,
    EARLY_SEND,
    SHIMMER_SEND,
    // 26.09.2026: the filter model (Filters.h) and its mode.
    FILTER,
    FILTER_MODE,
    // 26.09.2026, modulation (Poly.h, Modulation.h): the amplitude envelope's decay and sustain (Attack and
    // Release above), the filter's own envelope (or the amplitude's: FiltLink), a modulation envelope, four
    // LFOs, eight slots of a modulation matrix.
    AMP_DECAY,
    AMP_SUSTAIN,
    FILT_LINK,
    FILT_ATTACK,
    FILT_DECAY,
    FILT_SUSTAIN,
    FILT_RELEASE,
    ENV_VELOCITY,
    MOD_ATTACK,
    MOD_DECAY,
    MOD_SUSTAIN,
    MOD_RELEASE,
    LFO1_RATE,
    LFO1_SHAPE,
    LFO1_SYNC,
    LFO1_RETRIG,
    LFO1_FADE,
    LFO2_RATE,
    LFO2_SHAPE,
    LFO2_SYNC,
    LFO2_RETRIG,
    LFO2_FADE,
    LFO3_RATE,
    LFO3_SHAPE,
    LFO3_SYNC,
    LFO3_RETRIG,
    LFO3_FADE,
    LFO4_RATE,
    LFO4_SHAPE,
    LFO4_SYNC,
    LFO4_RETRIG,
    LFO4_FADE,
    MOD1_SRC,
    MOD1_DST,
    MOD1_AMT,
    MOD2_SRC,
    MOD2_DST,
    MOD2_AMT,
    MOD3_SRC,
    MOD3_DST,
    MOD3_AMT,
    MOD4_SRC,
    MOD4_DST,
    MOD4_AMT,
    MOD5_SRC,
    MOD5_DST,
    MOD5_AMT,
    MOD6_SRC,
    MOD6_DST,
    MOD6_AMT,
    MOD7_SRC,
    MOD7_DST,
    MOD7_AMT,
    MOD8_SRC,
    MOD8_DST,
    MOD8_AMT,
    COUNT
};
}
/** @brief The LFOs' shapes, tempo divisions, and the modulation matrix's sources and destinations (Modulation.h). */
extern const char *const LfoShapeNames[];
extern const char *const LfoSyncNames[];
extern const char *const ModSourceNames[];
extern const char *const ModDestNames[];
/** @brief The smaller matrix of the tape keys and the strings: two LFOs; pitch, tone, level, pan (kShortModDest). */
extern const char *const ShortModSourceNames[];
extern const char *const ShortModDestNames[];
/** @brief The names of the VCO models (Vco.h, VcoModel order). */
extern const char *const VcoNames[];
/** @brief The names of the filter models (Filters.h, FilterModel order). */
extern const char *const FilterNames[];
/** @brief Parameters of the tape keyboard (module Tape; PLAN 5.4, TapeKeys.h). */
namespace tape
{
enum : int
{
    SET,
    VOWEL,
    WOW,
    FLUTTER,
    SAG,
    TONE,
    AGE,
    LEVEL,
    PAN,
    ECHO_SEND,
    REVERB_SEND,
    // 25.09.2026: the strip's low cut; the distance macro; the allpass spread of the mono keyboard.
    LOW_CUT,
    DISTANCE,
    SPREAD,
    BLEND_SEND,
    EARLY_SEND,
    SHIMMER_SEND,
    // 26.09.2026: an envelope over the machine's (the swell at its minimum is the pressure pad's own rise, the
    // release is its fall), two LFOs and four slots (Modulation.h: pitch, tone, level, pan).
    AMP_ATTACK,
    AMP_DECAY,
    AMP_SUSTAIN,
    AMP_RELEASE,
    LFO1_RATE,
    LFO1_SHAPE,
    LFO1_SYNC,
    LFO1_RETRIG,
    LFO1_FADE,
    LFO2_RATE,
    LFO2_SHAPE,
    LFO2_SYNC,
    LFO2_RETRIG,
    LFO2_FADE,
    MOD1_SRC,
    MOD1_DST,
    MOD1_AMT,
    MOD2_SRC,
    MOD2_DST,
    MOD2_AMT,
    MOD3_SRC,
    MOD3_DST,
    MOD3_AMT,
    MOD4_SRC,
    MOD4_DST,
    MOD4_AMT,
    COUNT
};
}
extern const char *const TapeSetNames[];   ///< names of tape.set
/** @brief The drone's parameters are the lead's table with other defaults (module Drone). */
namespace drone = lead;
/** @brief Parameters of the string machine (module Strings; StringMachine.h). */
namespace strings
{
enum : int
{
    ATTACK,
    RELEASE,
    FEET,
    TONE,
    ENSEMBLE,
    LEVEL,
    PAN,
    ECHO_SEND,
    REVERB_SEND,
    // 25.09.2026, after Waldorf's Streichfett: the registration, its animation, the ensemble's type, the phaser.
    REGISTRATION,
    ANIMATE,
    ANIMATE_RATE,
    ENSEMBLE_TYPE,
    PHASER,
    // the strip's low cut, the distance macro, the send into the blend room
    LOW_CUT,
    DISTANCE,
    BLEND_SEND,
    EARLY_SEND,
    SHIMMER_SEND,
    // 26.09.2026: the envelope's decay and sustain (Attack and Release above), two LFOs and four slots
    // (Modulation.h: pitch, tone, level, pan).
    AMP_DECAY,
    AMP_SUSTAIN,
    LFO1_RATE,
    LFO1_SHAPE,
    LFO1_SYNC,
    LFO1_RETRIG,
    LFO1_FADE,
    LFO2_RATE,
    LFO2_SHAPE,
    LFO2_SYNC,
    LFO2_RETRIG,
    LFO2_FADE,
    MOD1_SRC,
    MOD1_DST,
    MOD1_AMT,
    MOD2_SRC,
    MOD2_DST,
    MOD2_AMT,
    MOD3_SRC,
    MOD3_DST,
    MOD3_AMT,
    MOD4_SRC,
    MOD4_DST,
    MOD4_AMT,
    COUNT
};
}
/** @brief Parameters of the springs (module Spring; Spring.h): fed from the echo's send. */
namespace spring
{
enum : int
{
    DECAY,
    TONE,
    RETURN,
    COUNT
};
}
/** @brief Parameters of the drum kit (module Drums; Drums.h). */
namespace drums
{
enum : int
{
    KICK_HZ,
    DECAY,
    TONE,
    LEVEL,
    ECHO_SEND,
    REVERB_SEND,
    /** 25.09.2026 */ LOW_CUT,
    BLEND_SEND,
    EARLY_SEND,
    COUNT
};
}
/**
 * @brief The performer's controls (module Perform; PLAN 8.1). They act on the piece as it plays and are
 *        neutral at their defaults, so a render without a performer is the composed piece to the bit:
 *  - Filter: the hand on the rows' filters, in octaves up or down from where the score has them.
 *  - Transpose: the transposition key, in semitones, for every tonal note that starts from now on.
 *  - Hold: the hands let go of the knobs, the score's gestures stand where they are.
 *  - Throw: the echo throw, 0..1: every source's echo send and the echo's feedback go up together.
 */
namespace perform
{
enum : int
{
    FILTER,
    TRANSPOSE,
    HOLD,
    THROW,
    KEYBOARD_PART,   ///< what a MIDI keyboard plays (perform::keys, 01.10.2026); off: a key transposes
    KEYBOARD_MODE,   ///< 0 Replace: the played voice's generated notes are left out; 1 Layer: it plays over them
    COMPOSER,        ///< on: the composer's notes play; off: only what the keyboard plays
    KEYBOARD_LOWER,   ///< the keys below KeyboardSplit play this voice instead (perform::keys; Off: no split; 02.10.2026)
    KEYBOARD_SPLIT,   ///< the split key: C1 (36) .. C5 (84), 12 a step
    KEYBOARD_SCALE,   ///< Scale Lock: a played key goes to the nearest note of the piece's key and scale (not the drums)
    KEYBOARD_VELOCITY,   ///< the velocity curve: as played, soft, hard, fixed (frame::shapeVelocity)
    COUNT
};
/**
 * @brief The keyboard's targets (perform.keyboard_part, 01.10.2026, Engine::queueLive): the lead, the drone, the poly
 *        synth, the tape keys, the strings, the drums (General MIDI's drum map), a row, or by channel (1 .. 8 the rows,
 *        9 the lead, 10 the drums -- General MIDI's drum channel --, 11 the drone, 12 the poly synth, 13 the tape keys,
 *        14 the strings).
 */
namespace keys
{
enum : int
{
    OFF,
    LEAD,
    DRONE,
    POLY,
    TAPE_KEYS,
    STRINGS,
    DRUMS,
    ROW1,
    ROW2,
    ROW3,
    ROW4,
    ROW5,
    ROW6,
    ROW7,
    ROW8,
    BY_CHANNEL,
    COUNT
};
}
}   // namespace perform
/**
 * @brief A style profile of the user's own (module Custom; Style.h, customProfile): with Use on, the composer
 *        takes these numbers instead of compose.style's; the counter rows' lengths, the transposer, the tape set
 *        and the drum patterns still come from compose.style (Cosmic and Drift have none: there Drums Chance does
 *        nothing). The defaults are the Cosmic profile's.
 */
namespace custom
{
enum : int
{
    USE,
    BPM_LOW,
    BPM_HIGH,
    MINUTES_LOW,
    MINUTES_HIGH,
    PHASES_LOW,
    PHASES_HIGH,
    INTRO,
    CODA,
    NEW_TEMPO,
    NEW_KEY,
    PEAK_ROWS,
    MUTATION,
    TAPE,
    STRINGS,
    LEAD,
    BLEEPS,
    DRUMS,
    LEAD_DENSITY,
    HAND_MOVE,
    HAND_REST,
    DARKNESS,
    HALL,
    LEVEL,
    COUNT
};
}
/** @brief The score cues over OSC (module Cue; Cue.h): on or off, and the UDP port (the host: EPH_CUE_HOST, else this machine). */
namespace cue
{
enum : int
{
    ENABLED,
    PORT,
    COUNT
};
}
/** @brief Parameters of the atmosphere (module Atmos; Atmos.h). */
namespace atmos
{
enum : int
{
    WIND,
    WIND_TONE,
    SWEEPS,
    SWEEP_LEVEL,
    BLEEPS,
    BLEEP_LEVEL,
    LEVEL,
    ECHO_SEND,
    REVERB_SEND,
    // 25.09.2026: the granular cloud (Atmos.h).
    GRAINS,
    GRAIN_DENSITY,
    LOW_CUT,
    SHIMMER_SEND,
    COUNT
};
}
/** @brief Parameters of the hall (module Reverb; PLAN 5.8): Phosphene's eight-line FDN. */
namespace reverb
{
enum : int
{
    SIZE,
    DECAY,
    DAMPING,
    PRE_DELAY,
    LOW_CUT,
    HIGH_CUT,
    RETURN,
    // Phase 5: the room: the hall (Reverb.h) or the plate (Plate.h), on the same send.
    TYPE,
    // 25.09.2026: the return ducked by the rows (the production guide 5.2).
    DUCK,
    COUNT
};
}
static_assert( static_cast<int>( lead::GLIDE ) == static_cast<int>( voice::GLIDE ),
               "the first parameters of the lead are those of the voice, in the same order" );
/** @brief Parameters of the tape echo (module Echo; PLAN 5.8). */
/** @brief Parameters of the blend room (Module::Blend, keys "blend.*"): a short plate whose return feeds the hall a little. */
/** @brief The early reflections (Module::Early, keys "early.*") and the effect hall (Module::Shimmer, "shimmer.*"). */
namespace early
{
enum : int
{
    SIZE,
    LOW_CUT,
    HIGH_CUT,
    RETURN,
    COUNT
};
}
namespace shimmer
{
enum : int
{
    DECAY,
    AMOUNT,
    LOW_CUT,
    HIGH_CUT,
    RETURN,
    COUNT
};
}
namespace blend
{
enum : int
{
    DECAY,
    PRE_DELAY,
    LOW_CUT,
    HIGH_CUT,
    DAMPING,
    RETURN,
    INTO_HALL,
    COUNT
};
}
/** @brief Parameters of the second echo (Module::Echo2, keys "delay.*"): a clean tape echo with its own time and return. */
namespace echo2
{
enum : int
{
    TIME,
    FEEDBACK,
    TONE,
    PING_PONG,
    RETURN,
    LOW_CUT,
    COUNT
};
}
namespace echo
{
enum : int
{
    TIME,
    FEEDBACK,
    TONE,
    WOW,
    FLUTTER,
    DRIVE,
    PING_PONG,
    RETURN,
    // Phase 5: the tape echo (TapeEcho.h) or the bucket-brigade delay (Bbd.h), on the same send.
    TYPE,
    // 25.09.2026: the repeats' low cut in the loop; the return ducked by the rows (the production guide 5.4).
    LOW_CUT,
    DUCK,
    COUNT
};
}
/** @brief The echo times, in the order of echo.time. */
enum class EchoTimeE : int
{
    SIXTEENTH = 0,
    EIGHTH,
    EIGHTH_D,
    QUARTER,
    QUARTER_D,
    HALF,
    /** 25.09.2026: the quarter triplet of the style guide's 4.4. */
    QUARTER_T,
    COUNT
};
/**
 * @brief Length of an echo time in beats.
 * @param[in] t  the echo time
 * @return its length in beats
 */
double echoTimeBeats( EchoTimeE t );
extern const char *const EchoTimeNames[];   ///< names of echo.time

/** @brief The five style profiles of PLAN 2.8, in the order of compose.style. */
enum class StyleE : int
{
    COSMIC = 0,
    DOOM,
    MELODIC,
    MODERN,
    DRIFT,
    COUNT
};
/** @brief The step divisions a row can run on, in the order of row.division. */
enum class RowDivisionE : int
{
    QUARTER = 0,
    EIGHTH,
    EIGHTH_T,
    SIXTEENTH,
    SIXTEENTH_T,
    THIRTY_SECOND,
    /** Phase 2: the slow divisions of a transposer row. */
    BAR1,
    BARS2,
    BARS4,
    COUNT
};
/**
 * @brief Length of a row division in quarter-note beats.
 * @param[in] d  the row division
 * @return its length in quarter-note beats
 */
double rowDivisionBeats( RowDivisionE d );
/** @brief What a row does, in the order of row.mode. */
enum class RowModeE : int
{
    NOTES = 0,
    TRANSPOSER,
    COUNT
};
/** @brief How a row walks its steps, in the order of row.direction. */
enum class RowDirectionE : int
{
    FORWARD = 0,
    BACKWARD,
    PENDULUM,
    RANDOM_WALK,
    COUNT
};

extern const char *const KeyNames[12];          ///< names of compose.key, C .. B
extern const char *const ScaleNames[];          ///< names of compose.scale
extern const char *const StyleNames[];          ///< names of compose.style
extern const char *const MorphNames[];          ///< names of compose.morph_to: "None", then the styles
extern const char *const ReverbTypeNames[];     ///< names of reverb.type: "Hall", "Plate"
extern const char *const EchoTypeNames[];       ///< names of echo.type: "Tape", "BBD"
extern const char *const EnsembleTypeNames[];   ///< names of strings.ensemble_type: "Solina", "Chorus", "Wide"
extern const char *const RegistrationNames[];   ///< names of strings.registration's whole numbers: "Violins" .. "Organ"
extern const char *const RowDivisionNames[];    ///< names of row.division
extern const char *const RowDirectionNames[];   ///< names of row.direction
extern const char *const RowModeNames[];        ///< names of row.mode

/**
 * @brief All parameter values of one engine, lock-free readable from the audio thread.
 *
 * Construction builds the registry from the module tables. Not copyable (atomics); use
 * copyValuesFrom() for snapshots.
 */
class ParamStore
{
public:
    ParamStore();
    ParamStore( const ParamStore & ) = delete;
    ParamStore &operator=( const ParamStore & ) = delete;

    /**
     * @brief Number of parameters.
     * @return the number of parameters
     */
    int count() const { return static_cast<int>( m_entries.size() ); }
    /**
     * @brief First id of a module instance; -1 if it does not exist.
     * @param[in] m         the module
     * @param[in] instance  its instance
     * @return the first id of the module instance, -1 if it does not exist
     */
    int base( ModuleE m, int instance = 0 ) const;
    /**
     * @brief Id of parameter @p index of a module instance; -1 if it does not exist.
     * @param[in] m         the module
     * @param[in] instance  its instance
     * @param[in] index     the parameter's index within the module
     * @return the parameter's id, -1 if it does not exist
     */
    int id( ModuleE m, int instance, int index ) const
    {
        const int b = base( m, instance );
        return b < 0 ? -1 : b + index;
    }
    /**
     * @brief Descriptor of @p id.
     * @param[in] id  the parameter
     * @return its descriptor
     */
    const ParamDesc &desc( int id ) const { return *m_entries[static_cast<size_t>( id )].m_desc; }
    /**
     * @brief Full text key of @p id ("row3.length").
     * @param[in] id  the parameter
     * @return its full text key
     */
    const std::string &key( int id ) const { return m_entries[static_cast<size_t>( id )].m_key; }
    /**
     * @brief Id for a text key, or -1.
     * @param[in] key  the text key
     * @return its id, -1 when there is none
     */
    int find( std::string_view key ) const;

    /**
     * @brief Current real value.
     * @param[in] id  the parameter
     * @return its current real value
     */
    float get( int id ) const { return m_values[static_cast<size_t>( id )].load( std::memory_order_relaxed ); }
    /**
     * @brief Current value rounded to an integer (Int, Choice, Toggle).
     * @param[in] id  the parameter
     * @return its current value rounded to an integer
     */
    int getInt( int id ) const;
    /**
     * @brief Current value as a switch.
     * @param[in] id  the parameter
     * @return its current value as a switch
     */
    bool getBool( int id ) const { return get( id ) >= 0.5f; }
    /**
     * @brief Sets a real value, clamped to the range (and rounded for discrete curves).
     * @param[in] id     the parameter
     * @param[in] value  the real value
     */
    void set( int id, float value );
    /**
     * @brief Sets from a normalised 0..1 position.
     * @param[in] id    the parameter
     * @param[in] norm  the position, 0..1
     */
    void setNormalised( int id, float norm ) { set( id, fromNormalised( id, norm ) ); }

    /**
     * @brief Real value to normalised 0..1.
     * @param[in] id     the parameter
     * @param[in] value  a real value
     * @return its normalised position, 0..1
     */
    float toNormalised( int id, float value ) const;
    /**
     * @brief Normalised 0..1 to real value.
     * @param[in] id    the parameter
     * @param[in] norm  a normalised position, 0..1
     * @return the real value there
     */
    float fromNormalised( int id, float norm ) const;

    /** @brief All parameters back to their defaults. */
    void resetDefaults();
    /**
     * @brief Default of @p id: the descriptor's, or the instance's own where instances differ (the rows).
     * @param[in] id  the parameter
     * @return its default value
     */
    float defaultValue( int id ) const { return m_defaults[static_cast<size_t>( id )]; }
    /**
     * @brief Copies every value from another store (for snapshots on another thread).
     * @param[in] other  the store whose values are copied
     */
    void copyValuesFrom( const ParamStore &other );
    /**
     * @brief Copies the current values of one module instance into @p out, indexed like its table.
     * @param[in]     m         module
     * @param[in]     instance  instance index
     * @param[in,out] out       at least as many floats as the module has parameters
     */
    void readModule( ModuleE m, int instance, float *out ) const;
    /**
     * @brief Number of parameters of a module.
     * @param[in] m  the module
     * @return how many parameters an instance of it has
     */
    static int moduleCount( ModuleE m );

    /**
     * @brief Applies "key=value" assignments separated by whitespace, newlines or ';'.
     *
     * Choice parameters accept their name ("compose.key=F#") or index. Lines starting with '#' are
     * comments.
     * @param[in]  text   the assignments
     * @param[out] error  receives a message for the first bad assignment, may be null
     * @return false if any assignment failed (the good ones are still applied)
     */
    bool parseText( std::string_view text, std::string *error = nullptr );
    /**
     * @brief The text form, one "key=value" per line.
     * @param[in] onlyChanged  leave out parameters at their default
     * @return the text form
     */
    std::string toText( bool onlyChanged ) const;
    /**
     * @brief A value formatted for display ("330 Hz", "F#").
     * @param[in] id  the parameter
     * @return its value formatted for display
     */
    std::string format( int id ) const;

private:
    /** @brief One parameter of the store: its description, its key, its module and instance. */
    struct Entry
    {
        const ParamDesc *m_desc;       ///< its description
        std::string      m_key;        ///< its key ("row1.length", "voice2.cutoff")
        ModuleE          m_module;     ///< its module
        int              m_instance;   ///< its instance
    };
    std::vector<Entry>                    m_entries;           ///< the parameters, by id
    std::unique_ptr<std::atomic<float>[]> m_values;            ///< their values, real units (atomic: any thread)
    std::vector<float>                    m_defaults;          ///< their defaults
    std::unordered_map<std::string, int>  m_index;             ///< key -> id
    static constexpr int                  MaxInstances = 16;   ///< the most instances a module can have
    int m_bases[static_cast<int>( ModuleE::COUNT )]
               [MaxInstances] = {};   ///< per module and instance: the id of its first parameter, -1 none
};

}   // namespace eph
