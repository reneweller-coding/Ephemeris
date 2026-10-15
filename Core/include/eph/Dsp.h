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
 * @author Rene Weller
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#if defined( _M_X64 ) || defined( __x86_64__ )
  #include <xmmintrin.h>
#elif defined( __aarch64__ )
  #include <fenv.h>
#endif

namespace eph
{

constexpr float Pi = 3.14159265358979f;      ///< pi as float
constexpr float TwoPi = 6.28318530717959f;   ///< 2 pi as float

/**
 * @brief Clamps @p v into [@p lo, @p hi].
 * @param[in] v   the value
 * @param[in] lo  the lower bound
 * @param[in] hi  the upper bound
 * @return v limited to [lo, hi]
 */
template <class T>
inline T clampv( T v, T lo, T hi )
{
    return v < lo ? lo : ( v > hi ? hi : v );
}

/**
 * @brief Decibels to a linear amplitude factor.
 * @param[in] db  the level in decibels
 * @return the linear amplitude factor
 */
inline float dbToGain( float db )
{
    return std::pow( 10.0f, db * 0.05f );
}

/** @brief MIDI note number to frequency in Hz (A4 = 69 = 440 Hz). */
/**
 * @brief The pitch class 0..11 of a note or interval, for negative values too. (Added in Ephemeris.)
 * @param[in] n  a note or an interval in semitones
 * @return its pitch class, 0 .. 11
 */
inline int pitchClass( int n )
{
    return ( ( n % 12 ) + 12 ) % 12;
}

/**
 * @brief The frequency of MIDI note @p note (A4 = 440 Hz), Hz.
 * @param[in] note  the MIDI note, fractional allowed
 * @return its frequency in Hz
 */
inline double midiToHz( double note )
{
    return 440.0 * std::pow( 2.0, ( note - 69.0 ) / 12.0 );
}

/**
 * @brief xorshift64 generator: deterministic per seed, one instance per owner.
 *
 * Never shared across threads. Derived streams come from fork(), so that one set seed can drive
 * every module without the modules disturbing each other's sequences.
 */
struct Rng
{
    uint64_t m_s = 0x9E3779B97F4A7C15ull;   ///< state, never zero

    /**
     * @brief Seeds the generator; any value including 0 is valid.
     * @param[in] v  the seed
     */
    void seed( uint64_t v )
    {
        m_s = v * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull;
        if ( m_s == 0 ) m_s = 1;
        next();
        next();
    }
    /**
     * @brief Next raw 64-bit value.
     * @return the next raw 64-bit value
     */
    uint64_t next()
    {
        m_s ^= m_s << 13;
        m_s ^= m_s >> 7;
        m_s ^= m_s << 17;
        return m_s;
    }
    /**
     * @brief Uniform float in [0, 1).
     * @return a uniform value in [0, 1)
     */
    float uniform() { return static_cast<float>( ( next() >> 40 ) * ( 1.0 / 16777216.0 ) ); }
    /**
     * @brief Uniform float in [-1, 1).
     * @return a uniform value in [-1, 1)
     */
    float bipolar() { return uniform() * 2.0f - 1.0f; }
    /**
     * @brief Uniform integer in [0, n); 0 when n <= 0.
     * @param[in] n  the bound
     * @return a uniform integer in [0, n), 0 when n <= 0
     */
    int below( int n ) { return n <= 0 ? 0 : static_cast<int>( next() % static_cast<uint64_t>( n ) ); }
    /**
     * @brief A seed for a derived, independent stream.
     * @return a seed for an independent stream
     */
    uint64_t fork() { return next(); }
    /**
     * @brief A standard normal value from twelve uniforms (Irwin-Hall): variance 1, bounded at +-6, cheap.
     *        Plenty for drifts and scatter. (Added in Ephemeris.)
     * @return a standard normal value, bounded at +-6
     */
    double gaussian()
    {
        double n = -6.0;
        for ( int i = 0; i < 12; ++i ) n += static_cast<double>( uniform() );
        return n;
    }
};

/**
 * @brief Mixes two 64-bit values into one well-distributed seed (splitmix64 finaliser).
 *
 * Used to derive per-bar and per-phrase seeds from the set seed, so that recomposing bar 37 gives
 * bar 37 again regardless of what was composed before it.
 * @param[in] a  the first value
 * @param[in] b  the second value
 * @return the mixed seed
 */
inline uint64_t mixSeed( uint64_t a, uint64_t b )
{
    uint64_t z = a + 0x9E3779B97F4A7C15ull * ( b + 1 );
    z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
    z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;
    return z ^ ( z >> 31 );
}

/**
 * @brief Sine lookup on a [0, 1) phase: 4096 entries, linear interpolation.
 *
 * Interpolation error is about 3e-7 of full scale (-130 dB). Two guard entries: a phase of
 * exactly 1.0 lands on index N and reads N + 1.
 */
struct SineTable
{
    static constexpr int N = 4096;     ///< table length
    float                m_v[N + 2];   ///< sin(2 pi i / N), with two guard entries
    SineTable()
    {
        for ( int i = 0; i <= N + 1; ++i )
            m_v[i] = std::sin( TwoPi * static_cast<float>( i ) / static_cast<float>( N ) );
    }
};

/**
 * @brief The one shared sine table.
 * @return the shared sine table
 */
inline const SineTable &sineTable()
{
    static const SineTable t;
    return t;
}

/**
 * @brief sin(2 pi phase) from the table.
 * @param[in] phase01  phase in [0, 1); values outside are wrapped.
 * @return sin(2 pi phase01)
 */
inline float sin01( double phase01 )
{
    double x = phase01 * SineTable::N;
    int    i = static_cast<int>( x );
    if ( static_cast<unsigned>( i ) > static_cast<unsigned>( SineTable::N ) )
    {
        const double p = phase01 - std::floor( phase01 );
        x = p * SineTable::N;
        i = static_cast<int>( x );
        if ( static_cast<unsigned>( i ) > static_cast<unsigned>( SineTable::N ) ) return 0.0f;
    }
    const float  f = static_cast<float>( x - i );
    const float *t = sineTable().m_v;
    return t[i] + f * ( t[i + 1] - t[i] );
}

/**
 * @brief ADSR with exponential segments, as in an analogue envelope generator.
 *
 * The attack is a one-pole towards 1.3 that stops at 1.0, which gives the convex attack of a
 * charging capacitor; decay and release are one-poles towards their targets.
 */
class Envelope
{
public:
    /** @brief Envelope stage. */
    enum class StageE
    {
        IDLE,
        ATTACK,
        DECAY,
        SUSTAIN,
        RELEASE
    };

    /**
     * @brief Sets the sample rate the times refer to.
     * @param[in] sr  the sample rate in Hz
     */
    void setSampleRate( double sr ) { m_sr = sr; }
    /**
     * @brief Sets the segment times.
     * @param[in] attackS   seconds from 0 to 1
     * @param[in] decayS    seconds for the decay to settle near the sustain
     * @param[in] sustain   sustain level 0..1
     * @param[in] releaseS  seconds to -43 dB (silent at about 1.8 times that)
     */
    void setTimes( float attackS, float decayS, float sustain, float releaseS )
    {
        m_aCoef = coef( attackS, 1.466f );
        m_dCoef = coef( decayS, 3.0f );
        m_rCoef = coef( releaseS, 5.0f );
        m_sus = sustain;
    }
    /**
     * @brief Takes over another envelope's segment times (the same sample rate assumed): setTimes without the exp.
     * @param[in] o  the envelope whose times are taken
     */
    void copyTimes( const Envelope &o )
    {
        m_aCoef = o.m_aCoef;
        m_dCoef = o.m_dCoef;
        m_rCoef = o.m_rCoef;
        m_sus = o.m_sus;
    }
    /** @brief Starts the attack from the current level (no reset to zero, so no click). */
    void noteOn() { m_stage = StageE::ATTACK; }
    /**
     * @brief Advances an attack that has just begun by a fraction of a sample, in closed form.
     *
     * The attack is a one-pole towards 1.3, so after s samples the distance to 1.3 has shrunk by
     * (1 - a)^s -- for any real s. Used for sub-sample note onsets. (Added in Phosphene.)
     * @param[in] samples  the fraction of a sample to advance by
     */
    void advanceAttack( double samples )
    {
        if ( m_stage != StageE::ATTACK || samples <= 0.0 ) return;
        m_level =
            static_cast<float>( 1.3 - ( 1.3 - m_level ) * std::pow( 1.0 - static_cast<double>( m_aCoef ), samples ) );
        if ( m_level >= 1.0f )
        {
            m_level = 1.0f;
            m_stage = StageE::DECAY;
        }
    }
    /** @brief Enters the release unless idle. */
    void noteOff()
    {
        if ( m_stage != StageE::IDLE ) m_stage = StageE::RELEASE;
    }
    /** @brief Silences immediately. */
    void kill()
    {
        m_stage = StageE::IDLE;
        m_level = 0.0f;
    }
    /**
     * not idle
     * @return true unless idle
     */
    bool isActive() const { return m_stage != StageE::IDLE; }
    /**
     * in release
     * @return true in the release stage
     */
    bool isReleasing() const { return m_stage == StageE::RELEASE; }
    /**
     * current level
     * @return the current level
     */
    float level() const { return m_level; }
    /**
     * current stage
     * @return the current stage
     */
    StageE stage() const { return m_stage; }

    /**
     * @brief Advances one sample and returns the level.
     * @return the level after the step
     */
    float process()
    {
        switch ( m_stage )
        {
        case StageE::ATTACK:
            m_level += ( 1.3f - m_level ) * m_aCoef;
            if ( m_level >= 1.0f )
            {
                m_level = 1.0f;
                m_stage = StageE::DECAY;
            }
            break;
        case StageE::DECAY:
            m_level += ( m_sus - m_level ) * m_dCoef;
            if ( std::fabs( m_level - m_sus ) < 1e-4f )
            {
                m_level = m_sus;
                m_stage = StageE::SUSTAIN;
            }
            if ( m_sus < 1e-4f && m_level < 1e-4f )
            {
                m_level = 0.0f;
                m_stage = StageE::IDLE;
            }
            break;
        case StageE::SUSTAIN:
            m_level = m_sus;
            if ( m_sus < 1e-4f )
            {
                m_level = 0.0f;
                m_stage = StageE::IDLE;
            }
            break;
        case StageE::RELEASE:
            m_level -= m_level * m_rCoef;
            if ( m_level < 1e-4f )
            {
                m_level = 0.0f;
                m_stage = StageE::IDLE;
            }
            break;
        default: break;
        }
        return m_level;
    }

private:
    /**
     * @brief The one-pole coefficient of a stage of @p seconds, @p k time constants long.
     * @param[in] seconds  the stage's length in seconds
     * @param[in] k        how many time constants the stage lasts
     * @return the one-pole coefficient
     */
    float coef( float seconds, float k ) const
    {
        const float tau = std::max( seconds, 0.0005f ) / k;
        return 1.0f - std::exp( -1.0f / ( tau * static_cast<float>( m_sr ) ) );
    }
    double m_sr = 48000.0;           ///< the sample rate, Hz
    float  m_aCoef = 0.01f;          ///< the attack's coefficient
    float  m_dCoef = 0.001f;         ///< the decay's coefficient
    float  m_rCoef = 0.0005f;        ///< the release's coefficient
    float  m_sus = 1.0f;             ///< the sustain level
    float  m_level = 0.0f;           ///< the level as it stands
    StageE m_stage = StageE::IDLE;   ///< where it is
};

/**
 * @brief Topology-preserving state-variable filter (Simper's trapezoidal SVF).
 *
 * Zero-delay feedback, stable under per-sample coefficient changes, all three outputs at once.
 */
struct Svf
{
    float m_ic1 = 0;    ///< first integrator state
    float m_ic2 = 0;    ///< second integrator state
    float m_a1 = 0;     ///< coefficient 1 / (1 + g (g + k))
    float m_a2 = 0;     ///< coefficient g a1
    float m_a3 = 0;     ///< coefficient g a2
    float m_k = 1.0f;   ///< damping (2 = no resonance)

    /**
     * @brief Cutoff and a 0..1 resonance (maps to damping 2 .. 0.1).
     * @param[in] cutoffHz   the cutoff frequency in Hz
     * @param[in] resonance  the resonance, 0..1
     * @param[in] sr         the sample rate in Hz
     */
    void set( float cutoffHz, float resonance, float sr )
    {
        setK( cutoffHz, 2.0f - 1.9f * clampv( resonance, 0.0f, 1.0f ), sr );
    }
    /**
     * @brief Cutoff and quality factor.
     * @param[in] cutoffHz  the cutoff frequency in Hz
     * @param[in] q         the quality factor
     * @param[in] sr        the sample rate in Hz
     */
    void setQ( float cutoffHz, float q, float sr ) { setK( cutoffHz, 1.0f / std::max( q, 0.05f ), sr ); }
    /**
     * @brief Takes another filter's coefficients, keeping this filter's state.
     * @param[in] o  the filter whose coefficients are taken
     */
    void copyCoefficients( const Svf &o )
    {
        m_a1 = o.m_a1;
        m_a2 = o.m_a2;
        m_a3 = o.m_a3;
        m_k = o.m_k;
    }
    /**
     * @brief Cutoff and damping directly.
     * @param[in] cutoffHz  the cutoff frequency in Hz
     * @param[in] damping   the damping k
     * @param[in] sr        the sample rate in Hz
     */
    void setK( float cutoffHz, float damping, float sr )
    {
        const float fc = clampv( cutoffHz, 10.0f, sr * 0.45f );
        setG( std::tan( Pi * fc / sr ), damping );
    }
    /**
     * @brief Prewarped gain g = tan(pi fc / fs) and damping directly.
     * @param[in] g        the prewarped gain tan(pi fc / fs)
     * @param[in] damping  the damping k
     */
    void setG( float g, float damping )
    {
        m_k = damping;
        m_a1 = 1.0f / ( 1.0f + g * ( g + m_k ) );
        m_a2 = g * m_a1;
        m_a3 = g * m_a2;
    }
    /**
     * @brief One sample, all outputs. The band pass has unity gain at the cutoff after scaling by k.
     * @param[in]  in  the input sample
     * @param[out] lp  receives the low-pass output
     * @param[out] bp  receives the band-pass output
     * @param[out] hp  receives the high-pass output
     */
    inline void tick( float in, float &lp, float &bp, float &hp )
    {
        const float v3 = in - m_ic2;
        const float v1 = m_a1 * m_ic1 + m_a2 * v3;
        const float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
        m_ic1 = 2.0f * v1 - m_ic1;
        m_ic2 = 2.0f * v2 - m_ic2;
        lp = v2;
        bp = v1;
        hp = in - m_k * v1 - v2;
    }
    /**
     * @brief One sample, low-pass output only.
     * @param[in] in  the input sample
     * @return the low-pass output
     */
    inline float lp( float in )
    {
        const float v3 = in - m_ic2;
        const float v1 = m_a1 * m_ic1 + m_a2 * v3;
        const float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
        m_ic1 = 2.0f * v1 - m_ic1;
        m_ic2 = 2.0f * v2 - m_ic2;
        return v2;
    }
    /** @brief Clears the states. */
    void reset() { m_ic1 = m_ic2 = 0.0f; }
};

/** @brief One-pole exponential smoother. */
struct Smoother
{
    float m_value = 0;       ///< current value
    float m_coef = 0.001f;   ///< per-sample coefficient
    /**
     * @brief Time constant in seconds.
     * @param[in] seconds  the time constant in seconds
     * @param[in] sr       the rate of the steps in Hz
     */
    void setTime( float seconds, double sr )
    {
        m_coef = 1.0f - std::exp( -1.0f / ( std::max( seconds, 1e-4f ) * static_cast<float>( sr ) ) );
    }
    /**
     * @brief One step towards @p target.
     * @param[in] target  the value to move towards
     * @return the smoothed value after the step
     */
    inline float next( float target )
    {
        m_value += ( target - m_value ) * m_coef;
        return m_value;
    }
    /**
     * @brief Jumps to @p v.
     * @param[in] v  the value to jump to
     */
    void snap( float v ) { m_value = v; }
};

/**
 * @brief One-pole DC blocker, y[n] = x[n] - x[n-1] + R y[n-1].
 */
struct DcBlocker
{
    float m_x1 = 0.0f;     ///< previous input
    float m_y1 = 0.0f;     ///< previous output
    float m_r = 0.9995f;   ///< pole radius
    /**
     * @brief Sets the corner frequency.
     * @param[in] sr  the sample rate in Hz
     * @param[in] hz  the corner frequency in Hz
     */
    void prepare( double sr, float hz = 5.0f )
    {
        m_r = 1.0f - static_cast<float>( TwoPi * hz / sr );
        reset();
    }
    /**
     * @brief One sample.
     * @param[in] x  the input sample
     * @return the sample without its DC
     */
    inline float process( float x )
    {
        const float y = x - m_x1 + m_r * m_y1;
        m_x1 = x;
        m_y1 = y;
        return y;
    }
    /** @brief Clears the states. */
    void reset() { m_x1 = m_y1 = 0.0f; }
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
struct BandLimit
{
    Svf m_a;   ///< first Butterworth section (damping 2 cos(pi/8))
    Svf m_b;   ///< second Butterworth section (damping 2 cos(3 pi/8))

    /**
     * @brief Sets the corner; it never goes above 0.4 fs, so a low sample rate degrades gracefully.
     * @param[in] sr        the sample rate in Hz
     * @param[in] cornerHz  the corner frequency in Hz (at most 0.4 fs)
     */
    void prepare( double sr, float cornerHz = 18000.0f )
    {
        const float fc = clampv( cornerHz, 1000.0f, static_cast<float>( 0.4 * sr ) );
        m_a.setK( fc, 1.8477590f, static_cast<float>( sr ) );   // 2 cos(pi/8)
        m_b.setK( fc, 0.7653669f, static_cast<float>( sr ) );   // 2 cos(3 pi/8)
        reset();
    }
    /**
     * @brief One sample.
     * @param[in] x  the input sample
     * @return the band-limited sample
     */
    inline float process( float x ) { return m_b.lp( m_a.lp( x ) ); }
    /** @brief Clears the states. */
    void reset()
    {
        m_a.reset();
        m_b.reset();
    }
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
class DenormalGuard
{
public:
    DenormalGuard()
    {
#if defined( _M_X64 ) || defined( __x86_64__ )
        m_saved = _mm_getcsr();
        _mm_setcsr( m_saved | 0x8040u );   // FZ (bit 15) and DAZ (bit 6)
#elif defined( __aarch64__ )
        // FPCR read and written directly rather than through fenv_t: the field that holds it has no
        // portable name (glibc calls it __fpcr, bionic's arm64 fenv_t calls it __control), and the
        // two system-register instructions are what fesetenv would end up doing anyway.
        m_saved = readFpcr();
        writeFpcr( m_saved | ( 1ull << 24 ) );   // FZ: flush-to-zero for NEON and scalar alike
#endif
    }
    ~DenormalGuard()
    {
#if defined( _M_X64 ) || defined( __x86_64__ )
        _mm_setcsr( m_saved );
#elif defined( __aarch64__ )
        writeFpcr( m_saved );
#endif
    }
    DenormalGuard( const DenormalGuard & ) = delete;
    DenormalGuard &operator=( const DenormalGuard & ) = delete;

private:
#if defined( _M_X64 ) || defined( __x86_64__ )
    unsigned int m_saved = 0;
#elif defined( __aarch64__ )
    /** @brief The floating-point control register. */
    static unsigned long long readFpcr()
    {
        unsigned long long v;
        __asm__ __volatile__( "mrs %0, fpcr" : "=r"( v ) );
        return v;
    }
    /** @brief Writes the floating-point control register. */
    static void writeFpcr( unsigned long long v ) { __asm__ __volatile__( "msr fpcr, %0" : : "r"( v ) ); }
    unsigned long long m_saved = 0;
#endif
};

/**
 * @brief Cubic soft clip, flat beyond +-1.5.
 * @param[in] x  the input sample
 * @return the clipped sample
 */
inline float softClip( float x )
{
    if ( x > 1.5f ) return 1.0f;
    if ( x < -1.5f ) return -1.0f;
    return x - ( 4.0f / 27.0f ) * x * x * x;
}

}   // namespace eph
