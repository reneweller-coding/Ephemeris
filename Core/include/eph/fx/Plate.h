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
        std::vector<float> buf;
        size_t mask = 0, write = 0;
        int length = 1;
        void init(int len);
        float at(int d) const { return buf[(write - static_cast<size_t>(d)) & mask]; }   ///< @p d samples ago
        void push(float x) { buf[write] = x; write = (write + 1) & mask; }
        float out() const { return at(length); }
    };
    /** @brief The all-pass w = x + g w[-D], y = w[-D] - g w (Dattorro's form). */
    static float allpass(Line& l, float x, float g, float delayed);
    double sr_ = 48000.0;
    Line pre_, in_[4], apL_, apR_, dL1_, dL2_, dR1_, dR2_, ap2L_, ap2R_;
    int in1_ = 0, in2_ = 0;                  ///< scaled modulated all-pass lengths (left, right)
    float excursion_ = 16.0f;                ///< scaled excursion of their modulation
    double lfo_ = 0.0;                       ///< the modulation's phase
    float bw_ = 0.9995f, bwState_ = 0.0f;    ///< input band limit
    float damp_ = 0.05f, dampL_ = 0.0f, dampR_ = 0.0f;
    float decay_ = 0.5f, pre_d_ = 0.0f;
    float hpCoef_ = 0.0f, hpL_ = 0.0f, hpR_ = 0.0f, hpXL_ = 0.0f, hpXR_ = 0.0f;
    float fbL_ = 0.0f, fbR_ = 0.0f;          ///< the halves' outputs, fed across
    int tapL_[7] = {}, tapR_[7] = {};        ///< scaled output taps
};

} // namespace eph
