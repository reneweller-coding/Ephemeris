/**
 * @file Dsp.h
 * @brief DSP primitives: random numbers, sine table, envelope, state-variable filter, smoother.
 *
 * Header-only, allocation-free, no framework.
 *
 * @note Copied from Noctuary `Core/include/ambient/Dsp.h` at commit b60a2fe (15.09.2026).
 *       Namespace changed to `phos`, comments rewritten in Doxygen form, the drone-specific
 *       wavefolder left behind. Everything else is arithmetically unchanged.
 * @note Copied from Phosphene `Core/include/phos/Dsp.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#if defined(_M_X64) || defined(__x86_64__)
  #include <xmmintrin.h>
#elif defined(__aarch64__)
  #include <fenv.h>
#endif

namespace eph {

constexpr float kPi    = 3.14159265358979f;   ///< pi as float
constexpr float kTwoPi = 6.28318530717959f;   ///< 2 pi as float

/** @brief Clamps @p v into [@p lo, @p hi]. */
template <class T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** @brief Decibels to a linear amplitude factor. */
inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }

/** @brief MIDI note number to frequency in Hz (A4 = 69 = 440 Hz). */
/** @brief The pitch class 0..11 of a note or interval, for negative values too. (Added in Ephemeris.) */
inline int pitchClass(int n) { return ((n % 12) + 12) % 12; }

/** @brief The frequency of MIDI note @p note (A4 = 440 Hz), Hz. */
inline double midiToHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

/**
 * @brief xorshift64 generator: deterministic per seed, one instance per owner.
 *
 * Never shared across threads. Derived streams come from fork(), so that one set seed can drive
 * every module without the modules disturbing each other's sequences.
 */
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;   ///< state, never zero

    /** @brief Seeds the generator; any value including 0 is valid. */
    void seed(uint64_t v) { s = v * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull; if (s == 0) s = 1; next(); next(); }
    /** @brief Next raw 64-bit value. */
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    /** @brief Uniform float in [0, 1). */
    float uniform() { return static_cast<float>((next() >> 40) * (1.0 / 16777216.0)); }
    /** @brief Uniform float in [-1, 1). */
    float bipolar() { return uniform() * 2.0f - 1.0f; }
    /** @brief Uniform integer in [0, n); 0 when n <= 0. */
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint64_t>(n)); }
    /** @brief A seed for a derived, independent stream. */
    uint64_t fork() { return next(); }
    /**
     * @brief A standard normal value from twelve uniforms (Irwin-Hall): variance 1, bounded at +-6, cheap.
     *        Plenty for drifts and scatter. (Added in Ephemeris.)
     */
    double gaussian() { double n = -6.0; for (int i = 0; i < 12; ++i) n += static_cast<double>(uniform()); return n; }
};

/**
 * @brief Mixes two 64-bit values into one well-distributed seed (splitmix64 finaliser).
 *
 * Used to derive per-bar and per-phrase seeds from the set seed, so that recomposing bar 37 gives
 * bar 37 again regardless of what was composed before it.
 */
inline uint64_t mixSeed(uint64_t a, uint64_t b)
{
    uint64_t z = a + 0x9E3779B97F4A7C15ull * (b + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/**
 * @brief Sine lookup on a [0, 1) phase: 4096 entries, linear interpolation.
 *
 * Interpolation error is about 3e-7 of full scale (-130 dB). Two guard entries: a phase of
 * exactly 1.0 lands on index N and reads N + 1.
 */
struct SineTable {
    static constexpr int N = 4096;   ///< table length
    float v[N + 2];                  ///< sin(2 pi i / N), with two guard entries
    SineTable() { for (int i = 0; i <= N + 1; ++i) v[i] = std::sin(kTwoPi * static_cast<float>(i) / static_cast<float>(N)); }
};

/** @brief The one shared sine table. */
inline const SineTable& sineTable() { static const SineTable t; return t; }

/**
 * @brief sin(2 pi phase) from the table.
 * @param phase01 phase in [0, 1); values outside are wrapped.
 */
inline float sin01(double phase01)
{
    double x = phase01 * SineTable::N;
    int i = static_cast<int>(x);
    if (static_cast<unsigned>(i) > static_cast<unsigned>(SineTable::N)) {
        const double p = phase01 - std::floor(phase01);
        x = p * SineTable::N;
        i = static_cast<int>(x);
        if (static_cast<unsigned>(i) > static_cast<unsigned>(SineTable::N)) return 0.0f;
    }
    const float f = static_cast<float>(x - i);
    const float* t = sineTable().v;
    return t[i] + f * (t[i + 1] - t[i]);
}

/**
 * @brief ADSR with exponential segments, as in an analogue envelope generator.
 *
 * The attack is a one-pole towards 1.3 that stops at 1.0, which gives the convex attack of a
 * charging capacitor; decay and release are one-poles towards their targets.
 */
class Envelope {
public:
    /** @brief Envelope stage. */
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    /** @brief Sets the sample rate the times refer to. */
    void setSampleRate(double sr) { sr_ = sr; }
    /**
     * @brief Sets the segment times.
     * @param attackS  seconds from 0 to 1
     * @param decayS   seconds for the decay to settle near the sustain
     * @param sustain  sustain level 0..1
     * @param releaseS seconds to -43 dB (silent at about 1.8 times that)
     */
    void setTimes(float attackS, float decayS, float sustain, float releaseS)
    {
        aCoef_ = coef(attackS, 1.466f);
        dCoef_ = coef(decayS, 3.0f);
        rCoef_ = coef(releaseS, 5.0f);
        sus_   = sustain;
    }
    /** @brief Takes over another envelope's segment times (the same sample rate assumed): setTimes without the exp. */
    void copyTimes(const Envelope& o) { aCoef_ = o.aCoef_; dCoef_ = o.dCoef_; rCoef_ = o.rCoef_; sus_ = o.sus_; }
    /** @brief Starts the attack from the current level (no reset to zero, so no click). */
    void noteOn()  { stage_ = Stage::Attack; }
    /**
     * @brief Advances an attack that has just begun by a fraction of a sample, in closed form.
     *
     * The attack is a one-pole towards 1.3, so after s samples the distance to 1.3 has shrunk by
     * (1 - a)^s -- for any real s. Used for sub-sample note onsets. (Added in Phosphene.)
     */
    void advanceAttack(double samples)
    {
        if (stage_ != Stage::Attack || samples <= 0.0) return;
        level_ = static_cast<float>(1.3 - (1.3 - level_) * std::pow(1.0 - static_cast<double>(aCoef_), samples));
        if (level_ >= 1.0f) { level_ = 1.0f; stage_ = Stage::Decay; }
    }
    /** @brief Enters the release unless idle. */
    void noteOff() { if (stage_ != Stage::Idle) stage_ = Stage::Release; }
    /** @brief Silences immediately. */
    void kill()    { stage_ = Stage::Idle; level_ = 0.0f; }
    bool  isActive() const    { return stage_ != Stage::Idle; }        ///< not idle
    bool  isReleasing() const { return stage_ == Stage::Release; }     ///< in release
    float level() const       { return level_; }                       ///< current level
    Stage stage() const       { return stage_; }                       ///< current stage

    /** @brief Advances one sample and returns the level. */
    float process()
    {
        switch (stage_) {
        case Stage::Attack:
            level_ += (1.3f - level_) * aCoef_;
            if (level_ >= 1.0f) { level_ = 1.0f; stage_ = Stage::Decay; }
            break;
        case Stage::Decay:
            level_ += (sus_ - level_) * dCoef_;
            if (std::fabs(level_ - sus_) < 1e-4f) { level_ = sus_; stage_ = Stage::Sustain; }
            if (sus_ < 1e-4f && level_ < 1e-4f) { level_ = 0.0f; stage_ = Stage::Idle; }
            break;
        case Stage::Sustain:
            level_ = sus_;
            if (sus_ < 1e-4f) { level_ = 0.0f; stage_ = Stage::Idle; }
            break;
        case Stage::Release:
            level_ -= level_ * rCoef_;
            if (level_ < 1e-4f) { level_ = 0.0f; stage_ = Stage::Idle; }
            break;
        default: break;
        }
        return level_;
    }

private:
    /** @brief The one-pole coefficient of a stage of @p seconds, @p k time constants long. */
    float coef(float seconds, float k) const
    {
        const float tau = std::max(seconds, 0.0005f) / k;
        return 1.0f - std::exp(-1.0f / (tau * static_cast<float>(sr_)));
    }
    double sr_ = 48000.0;   ///< the sample rate, Hz
    float aCoef_ = 0.01f;   ///< the attack's coefficient
    float dCoef_ = 0.001f;   ///< the decay's coefficient
    float rCoef_ = 0.0005f;   ///< the release's coefficient
    float sus_ = 1.0f;   ///< the sustain level
    float level_ = 0.0f;   ///< the level as it stands
    Stage stage_ = Stage::Idle;   ///< where it is
};

/**
 * @brief Topology-preserving state-variable filter (Simper's trapezoidal SVF).
 *
 * Zero-delay feedback, stable under per-sample coefficient changes, all three outputs at once.
 */
struct Svf {
    float ic1 = 0;     ///< first integrator state
    float ic2 = 0;     ///< second integrator state
    float a1 = 0;      ///< coefficient 1 / (1 + g (g + k))
    float a2 = 0;      ///< coefficient g a1
    float a3 = 0;      ///< coefficient g a2
    float k = 1.0f;    ///< damping (2 = no resonance)

    /** @brief Cutoff and a 0..1 resonance (maps to damping 2 .. 0.1). */
    void set(float cutoffHz, float resonance, float sr) { setK(cutoffHz, 2.0f - 1.9f * clampv(resonance, 0.0f, 1.0f), sr); }
    /** @brief Cutoff and quality factor. */
    void setQ(float cutoffHz, float q, float sr) { setK(cutoffHz, 1.0f / std::max(q, 0.05f), sr); }
    /** @brief Takes another filter's coefficients, keeping this filter's state. */
    void copyCoefficients(const Svf& o) { a1 = o.a1; a2 = o.a2; a3 = o.a3; k = o.k; }
    /** @brief Cutoff and damping directly. */
    void setK(float cutoffHz, float damping, float sr)
    {
        const float fc = clampv(cutoffHz, 10.0f, sr * 0.45f);
        setG(std::tan(kPi * fc / sr), damping);
    }
    /** @brief Prewarped gain g = tan(pi fc / fs) and damping directly. */
    void setG(float g, float damping)
    {
        k = damping;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    /** @brief One sample, all outputs. The band pass has unity gain at the cutoff after scaling by k. */
    inline void tick(float in, float& lp, float& bp, float& hp)
    {
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = in - k * v1 - v2;
    }
    /** @brief One sample, low-pass output only. */
    inline float lp(float in)
    {
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;
    }
    /** @brief Clears the states. */
    void reset() { ic1 = ic2 = 0.0f; }
};

/** @brief One-pole exponential smoother. */
struct Smoother {
    float value = 0;        ///< current value
    float coef = 0.001f;    ///< per-sample coefficient
    /** @brief Time constant in seconds. */
    void setTime(float seconds, double sr) { coef = 1.0f - std::exp(-1.0f / (std::max(seconds, 1e-4f) * static_cast<float>(sr))); }
    /** @brief One step towards @p target. */
    inline float next(float target) { value += (target - value) * coef; return value; }
    /** @brief Jumps to @p v. */
    void snap(float v) { value = v; }
};

/**
 * @brief One-pole DC blocker, y[n] = x[n] - x[n-1] + R y[n-1].
 */
struct DcBlocker {
    float x1 = 0.0f;     ///< previous input
    float y1 = 0.0f;     ///< previous output
    float r = 0.9995f;   ///< pole radius
    /** @brief Sets the corner frequency. */
    void prepare(double sr, float hz = 5.0f) { r = 1.0f - static_cast<float>(kTwoPi * hz / sr); reset(); }
    /** @brief One sample. */
    inline float process(float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    /** @brief Clears the states. */
    void reset() { x1 = y1 = 0.0f; }
};

/**
 * @brief Fourth-order Butterworth low pass: the upper end of the programme.
 *
 * **Why a programme needs an upper end at all.** Three costs, all of them paid above 20 kHz where
 * nothing can be heard (Ashihara, "Hearing thresholds for pure tones above 16 kHz", J. Acoust. Soc.
 * Am. 122(3), 2007: the threshold rises past 90 dB SPL above 20 kHz, i.e. out of reach at any sane
 * playback level):
 *  - **Headroom.** The true-peak estimator of ITU-R BS.1770-4 is a band-limited reconstruction --
 *    ours to 0.45 fs = 21.6 kHz (Dynamics.h). Content above that does not merely hide from it, it
 *    throws it off: measured on an eight-minute render, the meter read -0.98 dBTP where the exact
 *    peak was -0.075, and the same render cut at 21.6 kHz read -0.218 against an exact -0.182. The
 *    ceiling is a claim about the analogue waveform, and it is only true if the programme lives
 *    inside the band the estimator covers.
 *  - **Resampling.** Every distributed recording is 44.1 kHz, Nyquist 22.05 kHz, and a sample-rate
 *    converter puts its transition band at roughly 20 .. 22.05 kHz. Whatever sits above 20 kHz is
 *    either removed there or folded down into the audible band as an alias.
 *  - **Nonlinear stages.** A clipper or a saturator downstream mixes the ultrasonic content with the
 *    audible one and puts the difference frequencies back in the middle of the mix.
 *
 * **Why fourth order at 18 kHz.** Two cascaded trapezoidal SVF low passes with the Butterworth
 * dampings 2 cos(pi/8) and 2 cos(3 pi/8). The trapezoidal (bilinear) low pass carries a double zero
 * at Nyquist, so four poles bring four zeros with them and the last band before Nyquist collapses:
 * measured on the percussion kit the share of 22 .. 24 kHz falls by 49 dB and 20 .. 22 kHz by 22 dB,
 * while the calibrated 2 .. 16 kHz third-octave curve moves by 0.16 dB rms and the 16 kHz band by
 * 0.51 dB. A corner at 19 kHz costs less (0.04 dB rms) but leaves 20 .. 22 kHz only 15 dB down,
 * which is not an end; a sixth order at 18 kHz buys another 20 dB nobody can spend and costs three
 * times as much in the passband.
 */
struct BandLimit {
    Svf a;   ///< first Butterworth section (damping 2 cos(pi/8))
    Svf b;   ///< second Butterworth section (damping 2 cos(3 pi/8))

    /** @brief Sets the corner; it never goes above 0.4 fs, so a low sample rate degrades gracefully. */
    void prepare(double sr, float cornerHz = 18000.0f)
    {
        const float fc = clampv(cornerHz, 1000.0f, static_cast<float>(0.4 * sr));
        a.setK(fc, 1.8477590f, static_cast<float>(sr));   // 2 cos(pi/8)
        b.setK(fc, 0.7653669f, static_cast<float>(sr));   // 2 cos(3 pi/8)
        reset();
    }
    /** @brief One sample. */
    inline float process(float x) { return b.lp(a.lp(x)); }
    /** @brief Clears the states. */
    void reset() { a.reset(); b.reset(); }
};

/**
 * @brief Flush-to-zero and denormals-are-zero for the lifetime of the object, restored afterwards.
 *
 * A percussion lane that has died away keeps decaying towards zero, and below about 1e-38 the values
 * become subnormal, where every multiply costs a hundred times more. Rather than clamping each state
 * in the loop, the processor is told to treat those values as zero. The mode applies to scalar and
 * vector code alike (on x86-64 all float arithmetic is SSE; on AArch64 the FZ bit covers NEON and
 * scalar), so the lane paths stay identical to each other. (Added in Phosphene.)
 */
class DenormalGuard {
public:
    DenormalGuard()
    {
#if defined(_M_X64) || defined(__x86_64__)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u);   // FZ (bit 15) and DAZ (bit 6)
#elif defined(__aarch64__)
        // FPCR read and written directly rather than through fenv_t: the field that holds it has no
        // portable name (glibc calls it __fpcr, bionic's arm64 fenv_t calls it __control), and the
        // two system-register instructions are what fesetenv would end up doing anyway.
        saved_ = readFpcr();
        writeFpcr(saved_ | (1ull << 24));   // FZ: flush-to-zero for NEON and scalar alike
#endif
    }
    ~DenormalGuard()
    {
#if defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr(saved_);
#elif defined(__aarch64__)
        writeFpcr(saved_);
#endif
    }
    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;
private:
#if defined(_M_X64) || defined(__x86_64__)
    unsigned int saved_ = 0;
#elif defined(__aarch64__)
    /** @brief The floating-point control register. */
    static unsigned long long readFpcr()
    {
        unsigned long long v;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(v));
        return v;
    }
    /** @brief Writes the floating-point control register. */
    static void writeFpcr(unsigned long long v) { __asm__ __volatile__("msr fpcr, %0" : : "r"(v)); }
    unsigned long long saved_ = 0;
#endif
};

/** @brief Cubic soft clip, flat beyond +-1.5. */
inline float softClip(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

} // namespace eph
