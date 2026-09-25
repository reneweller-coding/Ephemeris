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
 */
#pragma once
#include "eph/Dsp.h"
#include <cmath>

namespace eph {

/** @brief Two-point PolyBLEP residual for a unit step at phase 0; @p t phase in [0,1), @p dt increment. */
inline float polyBlep(float t, float dt)
{
    if (t < dt) { const float x = t / dt; return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
    return 0.0f;
}

/** @brief PolyBLEP saw-to-pulse oscillator. */
class VaOscillator {
public:
    /**
     * @brief Sets the shape for the next samples.
     * @param hz         frequency
     * @param sampleRate rate the oscillator runs at
     * @param wave       0 = saw, 1 = pulse, blended in between
     * @param pulseWidth duty cycle of the pulse, 0.05..0.95
     */
    void set(double hz, double sampleRate, float wave, float pulseWidth)
    {
        dt_ = static_cast<float>(clampv(hz / sampleRate, 1e-7, 0.45));
        wave_ = clampv(wave, 0.0f, 1.0f);
        pw_ = clampv(pulseWidth, 0.05f, 0.95f);
    }
    /**
     * @brief Restarts so that the fundamental has sine phase @p fundamentalPhase at the time of the
     *        next sample minus @p late samples; the jump from the current value is band-limited.
     * @param fundamentalPhase phase of the fundamental in cycles
     * @param late             how many samples ago (at this oscillator's rate) the restart ideally happened
     */
    void restart(double fundamentalPhase, double late)
    {
        double t = fundamentalPhase + 0.5 + late * static_cast<double>(dt_);
        t -= std::floor(t);
        phase_ = static_cast<float>(t);
        pendingJump_ = shapeAt(phase_) - lastValue_;
        hasJump_ = true;
    }
    /** @brief Next sample. */
    inline float next()
    {
        const float t = phase_;
        const float saw = 2.0f * t - 1.0f - polyBlep(t, dt_);
        float v = saw;
        if (wave_ > 0.0f) {   // a pure saw (the tape keys' singers, most rows) skips the pulse
            float t2 = t + 1.0f - pw_;
            if (t2 >= 1.0f) t2 -= 1.0f;
            const float pulse = -((t < pw_ ? 1.0f : -1.0f) + polyBlep(t, dt_) - polyBlep(t2, dt_));
            v = saw + wave_ * (pulse - saw);
        }
        if (hasJump_) {
            // The sample before the restart already went out; the first one after it carries half
            // of the step, which spreads the edge over two samples.
            v -= 0.5f * pendingJump_;
            hasJump_ = false;
        }
        lastValue_ = v;
        phase_ += dt_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        return v;
    }

private:
    float shapeAt(float t) const
    {
        const float saw = 2.0f * t - 1.0f;
        const float pulse = t < pw_ ? -1.0f : 1.0f;
        return saw + wave_ * (pulse - saw);
    }
    float phase_ = 0.5f, dt_ = 0.001f, wave_ = 0.0f, pw_ = 0.5f;
    float lastValue_ = 0.0f, pendingJump_ = 0.0f;
    bool  hasJump_ = false;
};

} // namespace eph
