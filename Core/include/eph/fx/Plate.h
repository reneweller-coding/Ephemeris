/**
 * @file Plate.h
 * @brief A plate reverb (PLAN 5.8, a room of its own beside the hall): Dattorro's figure-eight tank.
 *
 * After J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES 45(9), 1997, the
 * plate-class reverberator in the style of Griesinger: a band limit and a pre-delay, four input diffusers
 * (all-passes of 142, 107, 379 and 277 samples at 29761 Hz), then a tank of two halves that feed each other --
 * each a modulated all-pass (672 and 908 samples, 16 samples of excursion), a delay, a damping low pass, the
 * decay gain, a second all-pass and a second delay. The output is the sum of fourteen taps inside the tank,
 * seven per side, as the paper's table gives them. All lengths are scaled from 29761 Hz to the sample rate.
 *
 * A plate is denser and brighter than the hall and has no early reflections of a room: the sound of the
 * EMT 140 on a seventies record. The decay time is turned into the tank's gain: a round through both halves
 * takes 0.716 s and passes the gain four times, so gain = 0.001^(0.179 / T60).
 * @author Rene Weller
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace eph {

/** @brief The plate; takes a stereo send, adds a stereo return. */
class Plate {
public:
    /**
     * @brief Sample rate; allocates the lines.
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare(double sampleRate);
    /** @brief Silences the tank. */
    void reset();
    /**
     * @brief The plate's settings; call at every 32-sample cell.
     * @param[in] decaySeconds     T60
     * @param[in] damping          0 bright .. 1 dark (the tank's low pass)
     * @param[in] preDelaySamples  before the plate
     * @param[in] lowCutHz         the return's high pass
     * @param[in] highCutHz        the input's band limit
     */
    void set(float decaySeconds, float damping, float preDelaySamples, float lowCutHz, float highCutHz);
    /**
     * @brief Adds the plate's return of @p inL, @p inR to @p outL, @p outR.
     * @param[in]     inL   the left input
     * @param[in]     inR   the right input
     * @param[in,out] outL  the left output, added to
     * @param[in,out] outR  the right output, added to
     * @param[in]     n     how many samples
     */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    /** @brief A delay line with a power-of-two buffer. */
    struct Line {
        std::vector<float> m_buf;   ///< the samples, a ring
        size_t m_mask = 0;   ///< its size - 1
        size_t m_write = 0;   ///< where the next sample goes
        int m_length = 1;   ///< the delay, samples
        /**
         * @brief Allocates for a delay of @p len samples and clears it.
         * @param[in] len  the delay in samples
         */
        void init(int len);
        /**
         * @p d samples ago
         * @param[in] d  how many samples ago
         * @return the sample then
         */
        float at(int d) const { return m_buf[(m_write - static_cast<size_t>(d)) & m_mask]; }
        /**
         * @brief Writes sample @p x.
         * @param[in] x  the sample
         */
        void push(float x) { m_buf[m_write] = x; m_write = (m_write + 1) & m_mask; }
        /**
         * @brief The sample length ago.
         * @return the sample the line's length ago
         */
        float out() const { return at(m_length); }
    };
    /**
     * @brief The all-pass w = x + g w[-D], y = w[-D] - g w (Dattorro's form).
     * @param[in,out] l        the all-pass's delay line
     * @param[in]     x        the input sample
     * @param[in]     g        the all-pass gain
     * @param[in]     delayed  w[-D], the line's output
     * @return the all-pass output
     */
    static float allpass(Line& l, float x, float g, float delayed);
    double m_sr = 48000.0;   ///< the sample rate, Hz
    Line m_pre;   ///< the pre-delay
    Line m_in[4];   ///< the input's four diffusing all-passes
    Line m_apL;   ///< the left half's modulated all-pass
    Line m_apR;   ///< the right half's modulated all-pass
    Line m_dL1;   ///< the left half's first delay
    Line m_dL2;   ///< the left half's second delay
    Line m_dR1;   ///< the right half's first delay
    Line m_dR2;   ///< the right half's second delay
    Line m_ap2L;   ///< the left half's second all-pass
    Line m_ap2R;   ///< the right half's second all-pass
    int m_in1 = 0;   ///< the left modulated all-pass's scaled length
    int m_in2 = 0;   ///< the right modulated all-pass's scaled length
    float m_excursion = 16.0f;                ///< scaled excursion of their modulation
    double m_lfo = 0.0;                       ///< the modulation's phase
    float m_bw = 0.9995f;   ///< the input band limit's coefficient
    float m_bwState = 0.0f;   ///< the input band limit's state
    float m_damp = 0.05f;   ///< the tank's damping, 0 bright .. 1 dark
    float m_dampL = 0.0f;   ///< the left half's damping state
    float m_dampR = 0.0f;   ///< the right half's damping state
    float m_decay = 0.5f;   ///< the tank's decay per pass
    float m_pre_d = 0.0f;   ///< the pre-delay, samples
    float m_hpCoef = 0.0f;   ///< the return's high pass coefficient
    float m_hpL = 0.0f;   ///< the high pass's output state, left
    float m_hpR = 0.0f;   ///< ... right
    float m_hpXL = 0.0f;   ///< the high pass's input state, left
    float m_hpXR = 0.0f;   ///< ... right
    float m_fbL = 0.0f;   ///< the left half's output, fed to the right
    float m_fbR = 0.0f;   ///< the right half's output, fed to the left
    int m_tapL[7] = {};   ///< the left output's taps, scaled
    int m_tapR[7] = {};   ///< the right output's taps, scaled
};

} // namespace eph
