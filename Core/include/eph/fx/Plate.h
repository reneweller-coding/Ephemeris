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
 */
#pragma once
#include <cstdint>
#include <vector>

namespace eph {

/** @brief The plate; takes a stereo send, adds a stereo return. */
class Plate {
public:
    /** @brief Sample rate; allocates the lines. */
    void prepare(double sampleRate);
    /** @brief Silences the tank. */
    void reset();
    /**
     * @brief The plate's settings; call at every 32-sample cell.
     * @param decaySeconds    T60
     * @param damping         0 bright .. 1 dark (the tank's low pass)
     * @param preDelaySamples before the plate
     * @param lowCutHz        the return's high pass
     * @param highCutHz       the input's band limit
     */
    void set(float decaySeconds, float damping, float preDelaySamples, float lowCutHz, float highCutHz);
    /** @brief Adds the plate's return of @p inL, @p inR to @p outL, @p outR. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    /** @brief A delay line with a power-of-two buffer. */
    struct Line {
        std::vector<float> buf;   ///< the samples, a ring
        size_t mask = 0;   ///< its size - 1
        size_t write = 0;   ///< where the next sample goes
        int length = 1;   ///< the delay, samples
        /** @brief Allocates for a delay of @p len samples and clears it. */
        void init(int len);
        float at(int d) const { return buf[(write - static_cast<size_t>(d)) & mask]; }   ///< @p d samples ago
        /** @brief Writes sample @p x. */
        void push(float x) { buf[write] = x; write = (write + 1) & mask; }
        /** @brief The sample length ago. */
        float out() const { return at(length); }
    };
    /** @brief The all-pass w = x + g w[-D], y = w[-D] - g w (Dattorro's form). */
    static float allpass(Line& l, float x, float g, float delayed);
    double sr_ = 48000.0;   ///< the sample rate, Hz
    Line pre_;   ///< the pre-delay
    Line in_[4];   ///< the input's four diffusing all-passes
    Line apL_;   ///< the left half's modulated all-pass
    Line apR_;   ///< the right half's modulated all-pass
    Line dL1_;   ///< the left half's first delay
    Line dL2_;   ///< the left half's second delay
    Line dR1_;   ///< the right half's first delay
    Line dR2_;   ///< the right half's second delay
    Line ap2L_;   ///< the left half's second all-pass
    Line ap2R_;   ///< the right half's second all-pass
    int in1_ = 0;   ///< the left modulated all-pass's scaled length
    int in2_ = 0;   ///< the right modulated all-pass's scaled length
    float excursion_ = 16.0f;                ///< scaled excursion of their modulation
    double lfo_ = 0.0;                       ///< the modulation's phase
    float bw_ = 0.9995f;   ///< the input band limit's coefficient
    float bwState_ = 0.0f;   ///< the input band limit's state
    float damp_ = 0.05f;   ///< the tank's damping, 0 bright .. 1 dark
    float dampL_ = 0.0f;   ///< the left half's damping state
    float dampR_ = 0.0f;   ///< the right half's damping state
    float decay_ = 0.5f;   ///< the tank's decay per pass
    float pre_d_ = 0.0f;   ///< the pre-delay, samples
    float hpCoef_ = 0.0f;   ///< the return's high pass coefficient
    float hpL_ = 0.0f;   ///< the high pass's output state, left
    float hpR_ = 0.0f;   ///< ... right
    float hpXL_ = 0.0f;   ///< the high pass's input state, left
    float hpXR_ = 0.0f;   ///< ... right
    float fbL_ = 0.0f;   ///< the left half's output, fed to the right
    float fbR_ = 0.0f;   ///< the right half's output, fed to the left
    int tapL_[7] = {};   ///< the left output's taps, scaled
    int tapR_[7] = {};   ///< the right output's taps, scaled
};

} // namespace eph
