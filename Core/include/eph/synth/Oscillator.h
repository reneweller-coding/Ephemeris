/**
 * @file Oscillator.h
 * @brief Band-limited virtual-analogue oscillator: PolyBLEP saw blended into a pulse.
 *
 * A naive sawtooth has a discontinuity every period whose partials fold back above Nyquist as
 * inharmonic tones. PolyBLEP subtracts a two-sample polynomial approximation of the band-limited
 * step residual at every discontinuity (Valimaki and Huovilainen, "Antialiasing oscillators in
 * subtractive synthesis", IEEE Signal Processing Magazine 24(2), 2007). Run at twice the sample
 * rate and decimated through a half-band filter, as the bass does, the remaining aliasing below
 * 18 kHz lies near -57 dB (measured in the self test).
 *
 * **Phase convention.** The oscillator is addressed by the phase of its *fundamental*, as a sine:
 * phase 0 means the fundamental is at an upward zero crossing. The rising ramp 2t - 1 has the
 * Fourier series -(2/pi) sum sin(2 pi k t)/k, so its fundamental is sin(2 pi (t - 1/2)): the ramp
 * position is the fundamental phase plus one half. At that point the ramp itself is zero.
 *
 * **Pulse polarity.** A pulse that is +1 for t < w has the fundamental +(4/pi) sin(2 pi t) at w = 1/2,
 * which is opposite in sign to the ramp's. Blending the two therefore cancelled the fundamental: at
 * Wave = 1/3 it vanished completely. The pulse is inverted (-1 for t < w) so both fundamentals share
 * one phase and the blend moves the timbre without taking the note away.
 * @note Copied from Phosphene `Core/include/phos/Oscillator.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 * @author Rene Weller
 */
#pragma once
#include "eph/Dsp.h"
#include <cmath>

namespace eph
{

/**
 * @brief Two-point PolyBLEP residual for a unit step at phase 0; @p t phase in [0,1), @p dt increment.
 * @param[in] t   the phase in [0, 1)
 * @param[in] dt  the phase increment per sample
 * @return the residual to add to the naive step
 */
inline float polyBlep( float t, float dt )
{
    if ( t < dt )
    {
        const float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if ( t > 1.0f - dt )
    {
        const float x = ( t - 1.0f ) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

/** @brief PolyBLEP saw-to-pulse oscillator. */
class VaOscillator
{
public:
    /**
     * @brief Sets the shape for the next samples.
     * @param[in] hz          frequency
     * @param[in] sampleRate  rate the oscillator runs at
     * @param[in] wave        0 = saw, 1 = pulse, blended in between
     * @param[in] pulseWidth  duty cycle of the pulse, 0.05..0.95
     */
    void set( double hz, double sampleRate, float wave, float pulseWidth )
    {
        m_dt = static_cast<float>( clampv( hz / sampleRate, 1e-7, 0.45 ) );
        m_wave = clampv( wave, 0.0f, 1.0f );
        m_pw = clampv( pulseWidth, 0.05f, 0.95f );
    }
    /**
     * @brief Restarts so that the fundamental has sine phase @p fundamentalPhase at the time of the
     *        next sample minus @p late samples; the jump from the current value is band-limited.
     * @param[in] fundamentalPhase  phase of the fundamental in cycles
     * @param[in] late              how many samples ago (at this oscillator's rate) the restart ideally happened
     */
    void restart( double fundamentalPhase, double late )
    {
        double t = fundamentalPhase + 0.5 + late * static_cast<double>( m_dt );
        t -= std::floor( t );
        m_phase = static_cast<float>( t );
        m_pendingJump = shapeAt( m_phase ) - m_lastValue;
        m_hasJump = true;
    }
    /**
     * @brief Next sample.
     * @return the next sample
     */
    inline float next()
    {
        const float t = m_phase;
        const float saw = 2.0f * t - 1.0f - polyBlep( t, m_dt );
        float       v = saw;
        if ( m_wave > 0.0f )
        {   // a pure saw (the tape keys' singers, most rows) skips the pulse
            float t2 = t + 1.0f - m_pw;
            if ( t2 >= 1.0f ) t2 -= 1.0f;
            const float pulse = -( ( t < m_pw ? 1.0f : -1.0f ) + polyBlep( t, m_dt ) - polyBlep( t2, m_dt ) );
            v = saw + m_wave * ( pulse - saw );
        }
        if ( m_hasJump )
        {
            // The sample before the restart already went out; the first one after it carries half
            // of the step, which spreads the edge over two samples.
            v -= 0.5f * m_pendingJump;
            m_hasJump = false;
        }
        m_lastValue = v;
        m_phase += m_dt;
        if ( m_phase >= 1.0f ) m_phase -= 1.0f;
        return v;
    }

private:
    /**
     * @brief The naive wave at phase @p t: the saw blended into the pulse by m_wave.
     * @param[in] t  the phase in [0, 1)
     * @return the naive wave there
     */
    float shapeAt( float t ) const
    {
        const float saw = 2.0f * t - 1.0f;
        const float pulse = t < m_pw ? -1.0f : 1.0f;
        return saw + m_wave * ( pulse - saw );
    }
    float m_phase = 0.5f;         ///< the phase, 0..1
    float m_dt = 0.001f;          ///< the phase step per sample
    float m_wave = 0.0f;          ///< the wave: 0 saw .. 1 pulse
    float m_pw = 0.5f;            ///< the pulse's width
    float m_lastValue = 0.0f;     ///< the last sample before its correction
    float m_pendingJump = 0.0f;   ///< a jump still to be smoothed in the next sample (its PolyBLEP's second half)
    bool  m_hasJump = false;      ///< m_pendingJump holds one
};

}   // namespace eph
