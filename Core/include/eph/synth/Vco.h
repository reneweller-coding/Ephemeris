/**
 * @file Vco.h
 * @brief The classic VCOs (26.09.2026): the Moog 921, the Prophet-5's, the Oberheim SEM's, the ARP 2600's and the
 *        E-mu modular's oscillators, after their circuits, with hard sync and cross modulation.
 *
 * **What makes them differ.** Every one of these is a sawtooth core: a capacitor charged by the exponential
 * converter's current until a comparator fires and a transistor dumps it; the pulse is a comparator on that ramp.
 * Three things of the circuit shape the sound, and each model has its own values of them (vcoProfile()):
 *  - The ramp's bow. An ideal current source charges the capacitor linearly; the leakage of a real core (the
 *    converter's output resistance, the capacitor's and the buffer's loads) bends the ramp towards the RC charging
 *    curve (1 - e^(-t/RC)), which to first order adds c t (1 - t) to the ramp: the SEM's discrete core bends it most,
 *    the E-mu's precise one not at all; the ARP's slightly the other way (its integrator speeds up towards the top).
 *  - The reset's knee. Where the reset transistor starts to conduct before the comparator has fired, the ramp's top
 *    rounds off: modelled as a t^8 falling-off of the last fifth of the ramp -- most on the Moog's discrete reset.
 *  - The drift. How far and how fast the pitch wanders (the Ornstein-Uhlenbeck drift of ModVoice.h, scaled), how far
 *    a note lands off its pitch, and a fast jitter of a fraction of a cent (the core's and the converter's noise,
 *    correlated over a few milliseconds): the Moog and the SEM wander the most, the ARP jitters the most, the E-mu
 *    is the steadiest.
 * The magnitudes are set from the circuits' principles and the instruments' reputations, not measured on them.
 *
 * **Band-limited.** The waveform is the ramp blended into the comparator's pulse (the voice's Wave and Pulse Width).
 * Its discontinuities -- the reset, the pulse's edges, a hard sync -- are smoothed with the two-point polynomial BLEP
 * for the jump and the BLAMP (its integral) for the change of slope (Välimäki, Pekonen, Nam, "Perceptually informed
 * synthesis of bandlimited classical waveforms using integrated polynomial interpolation", JASA 2012; Esqueda,
 * Välimäki, Bilbao, "Rounding corners with BLAMP", DAFx 2016), at the voices' twice the sample rate. Every event
 * inside a sample step is found exactly -- its place, the jump and the change of slope there -- and its residuals go
 * onto the samples on either side; since the step's increment is known when the step begins, the oscillator needs
 * no latency.
 *
 * **Hard sync** (the Prophet's Osc A to Osc B): VCO 2 restarts wherever VCO 1 completes a cycle, the restart's jump
 * and change of slope smoothed like the others -- the classic sync sweep when VCO 2's pitch is moved (Osc 2 Pitch,
 * or the matrix's Osc 2 Pitch target from the filter envelope: the Prophet's Poly-Mod). **Cross mod**: VCO 1's
 * output on VCO 2's exponential frequency input at audio rate, as the ARP 2600's FM and the Prophet's Poly-Mod
 * from Osc B.
 */
#pragma once
#include <algorithm>
#include <cmath>

namespace eph {

/** @brief The VCO models (kVcoNames). */
enum class VcoModel : int { Analog, Moog921, Prophet5, Sem, Arp2600, Emu, Count };
constexpr int kVcoModels = static_cast<int>(VcoModel::Count);   ///< how many VCO models there are

/** @brief A model's circuit: the ramp's bow and knee, and its drift against the voice's. */
struct VcoProfile {
    float bow;           ///< c of the ramp 2t - 1 + c t (1 - t) - a t^8: the core's leakage
    float knee;          ///< a: the reset's knee
    float driftSpread;   ///< times the Drift knob's spread
    float driftTau;      ///< times the drift's time constants
    float noteScatter;   ///< times how far a note lands off its pitch
    float jitterCents;   ///< the fast wander of the pitch (a few milliseconds' correlation), cents
};

/** @brief The profile of model @p m (VcoModel; clamped). */
inline const VcoProfile& vcoProfile(int m)
{
    static const VcoProfile k[kVcoModels] = {
        { 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },       // Analog: the voices' own oscillators, ideal ramps
        { 0.1f, 0.2f, 1.4f, 0.7f, 1.5f, 0.6f },       // Moog 921: a discrete reset with a soft knee; wanders, warm
        { 0.05f, 0.04f, 0.8f, 1.0f, 0.8f, 0.3f },     // Prophet-5 (SSM2030): an integrated, nearly ideal core
        { 1.0f, 0.08f, 1.3f, 0.8f, 1.2f, 0.5f },      // Oberheim SEM: the discrete core's leakage, a round ramp
        { -0.15f, 0.0f, 1.0f, 0.6f, 1.0f, 0.8f },     // ARP 2600 (4027): a fast reset, the ramp speeding up a little
        { 0.0f, 0.0f, 0.4f, 1.5f, 0.5f, 0.1f },       // E-mu modular: precise and stable
    };
    return k[std::clamp(m, 0, kVcoModels - 1)];
}

/** @brief One oscillator after a model: its phase and the residuals it owes the next sample. */
struct VcoOsc {
    double ph = 0.0;          ///< phase in cycles, [0, 1)
    float pending = 0.0f;     ///< residuals of the events since the last sample, for the next

    /** @brief The waveform at phase @p t: the model's ramp blended into the pulse (-1 below @p pw, +1 above). */
    static float value(const VcoProfile& p, float wave, float pw, double t)
    {
        const float x = static_cast<float>(t), x2 = x * x, x4 = x2 * x2;
        const float ramp = 2.0f * x - 1.0f + p.bow * x * (1.0f - x) - p.knee * x4 * x4;
        return ramp + wave * ((x < pw ? -1.0f : 1.0f) - ramp);
    }
    /** @brief Its slope per cycle at @p t (the pulse's is zero between its edges). */
    static float slope(const VcoProfile& p, float wave, double t)
    {
        const float x = static_cast<float>(t), x2 = x * x, x3 = x2 * x;
        return (1.0f - wave) * (2.0f + p.bow * (1.0f - 2.0f * x) - 8.0f * p.knee * x3 * x3 * x);
    }

    /**
     * @brief The sample at the present phase, band-limited, then one step of @p dt cycles.
     * @param syncAt  where in this step (0..1] VCO 1 completed a cycle, to restart there (negative: no sync)
     * @param wrapAt  if not null: where in this step this oscillator completed its cycle (negative: it did not)
     */
    float step(double dt, const VcoProfile& p, float wave, float pw, double syncAt, double* wrapAt)
    {
        float y = value(p, wave, pw, ph) + pending;
        pending = 0.0f;
        if (wrapAt != nullptr) *wrapAt = -1.0;
        const float bendScale = static_cast<float>(dt);
        double s = 0.0, t = ph;   // how much of the step has gone, and the phase there
        for (int guard = 0; guard < 6; ++guard) {
            // The next event in (s, 1]: the end of the cycle, the pulse's edge, the restart by sync.
            double next = 2.0;
            int kind = 0;
            const double toEnd = s + (1.0 - t) / dt;
            if (toEnd <= 1.0) { next = toEnd; kind = 1; }
            if (wave > 0.0f && t < static_cast<double>(pw)) {
                const double e = s + (static_cast<double>(pw) - t) / dt;
                if (e <= 1.0 && e < next) { next = e; kind = 2; }
            }
            if (syncAt > s && syncAt <= 1.0 && syncAt <= next) { next = syncAt; kind = 3; }
            if (kind == 0) break;
            const double at = std::min(1.0, t + (next - s) * dt);   // the phase at the event
            float jump = 0.0f, bend = 0.0f;
            if (kind == 2) {
                jump = 2.0f * wave;   // the pulse from -1 to +1
                t = static_cast<double>(pw);
            } else {
                // The end of the cycle or a restart: from the value and slope there to those at the start.
                jump = value(p, wave, pw, 0.0) - (kind == 1 ? value(p, wave, 0.0f, 1.0) : value(p, wave, pw, at));   // at the cycle's end the pulse is high
                bend = (slope(p, wave, 0.0) - slope(p, wave, at)) * bendScale;
                if (kind == 1 && wrapAt != nullptr) *wrapAt = next;
                t = 0.0;
            }
            // The residuals of a step and of a bend at `next` of the way from this sample to the next one.
            const float d = static_cast<float>(next), e = 1.0f - d;
            y += jump * 0.5f * e * e + bend * e * e * e * (1.0f / 6.0f);
            pending += -jump * 0.5f * d * d + bend * d * d * d * (1.0f / 6.0f);
            s = next;
        }
        ph = t + (1.0 - s) * dt;
        if (ph >= 1.0) ph -= 1.0;
        return y;
    }
};

} // namespace eph
