/**
 * @file TapeKeys.h
 * @brief The tape keyboard (PLAN 5.4): a Mellotron made without recordings.
 *
 * A Mellotron is a tape machine per key: pressing a key presses a strip of tape against a capstan that
 * turns all the time, and a head reads about eight seconds of a recorded instrument until the tape runs
 * out. What it sounds like is the recording *and* the machine; both are modelled here (the name on the
 * panel is "Tape Keys": "Mellotron" is a trade name).
 *
 * **The sources**, one per key, synthesised:
 * - **Choir**: an ensemble of singers per key. Each singer is a glottal-like source -- a band-limited
 *   saw through a two-pole low pass at 1.1 kHz, the -12 dB/octave tilt of glottal flow (a cheap stand-in
 *   for the LF model of Fant, Liljencrants and Lin 1985, PLAN 5.4) -- with its own detune, vibrato rate
 *   and depth, and slow pitch and level wander (jitter and shimmer). The singers of a key are summed and
 *   pass one formant bank of five band passes on an "aah" that can lean towards "ooh" (the vowel knob),
 *   with formant frequencies and bandwidths after Klatt (1980) for a mixed choir.
 * - **Strings**: five saws detuned against each other through a two-pole low pass at 1.1 kHz, with a
 *   little of its band pass added as the body's resonance.
 * - **Flute**: a sine with a little second and third harmonic, breath noise band-passed around the pitch
 *   and a chiff at the onset.
 *
 * **The machine**:
 * - Every key has its own tape: a detune of a few cents, a level, a tone and an onset lag of its own,
 *   fixed per key and seed (an instrument has one set of tapes; they do not change between notes).
 * - One capstan for all keys: wow (a slow sine with a drift) and flutter (a fast small sine) move the
 *   pitch of every sounding key together.
 * - Motor load: every key pressed slows the capstan a little, so a full chord sits lower than a single
 *   note (`sag` cents per key beyond the first).
 * - The pressure pad's onset: a short thump of noise and a rise over some tens of milliseconds.
 * - The tape end: after `kTapeSeconds` the sound fades out over a third of a second, whatever the key
 *   does. A player re-strikes; the chord writer does too (Pads.h).
 * - The heads and the electronics: a low pass (tone) per key, gentle tanh saturation on the sum, and a
 *   hiss while any key is down.
 * **The player's envelope and modulation** (26.09.2026): over the machine's own envelope a swell (a volume pedal),
 * a decay to a sustain level, and the release after the key, which is the machine's fall (70 ms by default); two
 * LFOs and four slots (Modulation.h) on the whole keyboard -- pitch, the heads' tone, the level, the pan -- as a
 * player's hand on the machine would reach them.
 * The magnitudes are first settings; PLAN 5.4 wants them measured on Mellotron recordings (statistics
 * only), which has not happened yet.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/synth/Oscillator.h"
#include "eph/synth/Modulation.h"
#include <cstdint>

namespace eph {

constexpr int kTapeKeys = 8;          ///< keys that can sound at once
constexpr int kSingers = 6;           ///< voices per choir key at most (the Quest plays fewer: setSingers)
constexpr double kTapeSeconds = 8.0;  ///< length of a tape

/** @brief Which recordings are on the tapes. */
enum class TapeSet : int { Choir = 0, Strings, Flute, Count };

/** @brief Settings for the following samples. */
struct TapeSettings {
    TapeSet set = TapeSet::Choir;   ///< the tapes of the next keys pressed
    float vowel = 0.0f;         ///< 0 "aah" .. 1 "ooh" (choir)
    float wowCents = 6.0f;      ///< capstan wow depth
    float flutterCents = 2.0f;  ///< capstan flutter depth
    float sagCents = 1.0f;      ///< pitch drop per key pressed beyond the first
    float toneHz = 7000.0f;     ///< head and electronics low pass
    float age = 0.5f;           ///< 0..1: spread of the tapes, hiss, onset thump
    // 26.09.2026 (compared bit for bit: 4-byte members only).
    float swellMs = 1.0f;       ///< the swell over the pressure pad's rise; at 1 ms and below, none
    float decayMs = 1500.0f;    ///< the decay towards the sustain level
    float sustain = 1.0f;       ///< the sustain level (1: no decay)
    float releaseMs = 70.0f;    ///< the fall after the key (the machine's)
    ModSettings mod;            ///< two LFOs and four slots (the destinations mapped by shortModDest)
};

/** @brief The tape keyboard: eight keys, one output (mono). */
class TapeKeys {
public:
    /** @brief Sample rate and seed (the seed fixes the tapes). */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Silences everything. */
    void reset();
    /** @brief Takes the settings; call at every 32-sample cell. */
    void set(const TapeSettings& s);
    /**
     * @brief How many of a choir key's singers sound (1 .. kSingers; the Quest's quality level plays 3). The
     *        sum is scaled by 1 / sqrt(6 n), which keeps the level of a choir of random phases the same for any
     *        n and is exactly 1/6 for all six, so the desktop's sound does not move by a bit.
     */
    void setSingers(int n) { singers_ = n < 1 ? 1 : (n > kSingers ? kSingers : n); }
    /** @brief Presses a key; if all keys sound, the oldest is taken. */
    void noteOn(int pitch, float velocity, int id);
    /** @brief Releases the key started with @p id. */
    void noteOff(int id);
    /** @brief Whether anything sounds. */
    bool active() const;
    /** @brief Renders @p n samples into @p out (overwritten). */
    void process(float* out, int n);
    /** @brief The capstan's speed factor for @p keysDown keys pressed (for the tests). */
    double speedFor(int keysDown) const;
    /** @brief The piece's beat at the next sample and beats per sample (the synced LFOs); call at every cell. */
    void setClock(double beat, double beatsPerSample) { beat0_ = beat; bps_ = beatsPerSample; clockAt_ = count_; }
    /** @brief The matrix's pan offset (the engine adds it to the strip's pan). */
    float panMod() const { return mod_.offset(mo_, ModDest::Pan); }

private:
    struct Singer {
        VaOscillator osc;
        Svf tilt;
        double detune = 0.0, vibHz = 5.5, vibDepth = 20.0, vibPhase = 0.0;
        double wander = 0.0, wanderTarget = 0.0, level = 1.0;
    };
    struct Key {
        bool on = false, held = false;
        int pitch = 60, id = -1;
        float velocity = 0.8f;
        double age = 0.0;          ///< seconds since the key went down
        double released = -1.0;    ///< seconds since release, -1 while held
        double rise = 0.0;         ///< the pressure pad's rise, 0 .. 1 (a one-pole towards 1 after the lag)
        double riseCoef = 0.0;     ///< its coefficient per sample
        double fall = 1.0;         ///< the release, 1 .. 0 (a one-pole towards 0 after the key is let go)
        double lag = 0.0;          ///< this tape's onset lag, seconds
        double cents = 0.0;        ///< this tape's detune
        float gain = 1.0f;
        Svf tone;
        Singer singers[kSingers];
        Svf formant[5];
        double phase = 0.0;        ///< flute
        Svf breath;
        uint32_t order = 0;        ///< for taking the oldest key
        TapeSet set = TapeSet::Choir;   ///< the tape set the key was pressed on: a switch takes the next press
        double decay = 1.0;        ///< the player's envelope: what is left of the decay towards the sustain level
    };
    float render(Key& k, double speedCents);
    void startKey(Key& k, int pitch, float velocity, int id);
    double sr_ = 48000.0;
    uint64_t seed_ = 1;
    TapeSettings s_;
    Key keys_[kTapeKeys];
    Rng rng_;
    uint32_t counter_ = 0;
    double wowPhase_ = 0.0, flutterPhase_ = 0.0, drift_ = 0.0, driftTarget_ = 0.0;
    int64_t count_ = 0;
    float thump_ = 0.0f;
    double fallCoef_ = 0.0;       ///< the release's factor per sample (70 ms time constant)
    float formantGain_[5] = {};
    int singers_ = kSingers;       ///< setSingers()   ///< the choir's formant levels at the current vowel
    double decayCoef_ = 1.0;       ///< the player's decay per sample
    Modulator mod_;                ///< the keyboard's LFOs and matrix
    float mo_[kModDests] = {};     ///< its last sums
    double beat0_ = 0.0, bps_ = 0.0;   ///< the clock (setClock)
    int64_t clockAt_ = 0;
    double beatAt(int64_t at) const { return beat0_ + static_cast<double>(at - clockAt_) * bps_; }
};

} // namespace eph
