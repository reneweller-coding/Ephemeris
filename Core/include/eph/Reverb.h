/**
 * @file Reverb.h
 * @brief Eight-line feedback delay network reverb for the send buses (room and hall).
 *
 * Each channel passes its own pre-delay and a four-stage Schroeder all-pass diffusion into four of the
 * eight lines, and reads those same four back (Reverb.cpp, kLeft); a Householder reflection mixes all
 * eight. The four are not the first half or the second: the line set is ordered by length, so that
 * split handed one channel every short line and the other every long one and the two returns came out
 * with different colours (16.09.2026: 1.45 dB rms between the third octaves 500 Hz .. 8 kHz in the
 * room, 1.36 in the hall, worst band 2.55). Interleaved so that each channel holds two short lines and
 * two long ones, the returns are as decorrelated as before -- they share no line -- and now differ by
 * 0.65 and 0.53 dB rms.
 * Every line carries a short all-pass inside its loop, which scatters each echo into many so the echo
 * density grows quickly (Schlecht and Habets, "Scattering in feedback delay networks", IEEE/ACM TASLP
 * 2020), and the line lengths are the set Noctuary's optimiser found for the flattest third-octave
 * magnitude of the tail (after the colourless-FDN work of Dal Santo, Prawda, Schlecht and Valimaki).
 * Each line's gain gives the decay time exactly: g = 10^(-3 L / (T60 fs)). A one-pole low pass in every
 * loop darkens the tail; a high cut and a 12 dB/octave low cut sit on the return.
 *
 * **Depth rule.** The return's low cut never goes below 150 Hz: a tail under 140 Hz would smear
 * exactly the band where kick and bass are phase-locked.
 *
 * @note Adapted from Noctuary `Core/include/ambient/Effects.h` (class Reverb) at b60a2fe (15.09.2026):
 *       the network, diffusion, scattering, line lengths, damping and return filters are unchanged; the
 *       freeze, the rotating-matrix mode and the classic line set are left behind, the pre-delay is set
 *       in samples by the caller (so it can follow the tempo), and the output is wet only.
 *
 * **The gate (20.09.2026, round "reverb", A2-gated-reverb.md).** The user's rule: a big hall that is
 * ducked while the synths play and cut hard at the bar change, as an option per voice/bus. Two stages,
 * both multiplicative on the wet return and independent of each other:
 *  - setDuck()/processDucked(): an envelope follower on the *send itself* (self-sidechain, the classic
 *    gated-reverb trick) pulls the return down while the feed is loud and lets it back up as the feed's
 *    own envelope falls quiet -- no note events are needed, so this class stays self-contained and the
 *    result reacts to whatever the caller actually sent, stutters and level automation included.
 *  - barGate(): stateless, a function of the absolute beat alone (same idea as TranceGate::open): open
 *    through the bar, a fast raised-cosine close from the bar line, a hold at the floor, a fast reopen.
 *    Being a pure function of `beat`, a bar rendered alone closes at the same instant as the same bar
 *    inside a whole set -- the host's block size decides nothing. It is `static` and returns a plain
 *    gain because, unlike the duck, it needs the tempo (beats per sample) to reach the caller's absolute
 *    beat grid, which only Engine.cpp's send routing knows; Engine.cpp multiplies it onto this class's
 *    gated output.
 * @note Copied from Phosphene `Core/include/phos/Reverb.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include "eph/Dsp.h"
#include <vector>

namespace eph {

/** @brief Stereo FDN reverb, wet output only. */
class Reverb {
public:
    /** @brief Allocates for a sample rate. */
    void prepare(double sampleRate);
    /** @brief Clears every line. */
    void reset();
    /**
     * @brief Sets the space.
     * @param size          line-length scale, 0.3 .. 3 (1 = 30 to 90 ms lines)
     * @param decaySeconds  T60 of the tail
     * @param damping       0..1, high-frequency loss per loop
     * @param preDelaySamples pre-delay
     * @param lowCutHz      return low cut (at least 150 Hz)
     * @param highCutHz     return high cut
     */
    void set(float size, float decaySeconds, float damping, float preDelaySamples, float lowCutHz, float highCutHz);
    /**
     * @brief Processes a stereo send.
     * @param inL,inR   send input
     * @param outL,outR receives the wet return (replaced)
     */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

    /**
     * @brief Configures the duck: an envelope follower on the send pulls the wet return down while the
     *        send is above @p thresholdDb, and lets it back up once the send falls quiet again.
     * @param depth      0..1, how far the follower pulls the wet return down at full activity
     * @param thresholdDb the send level, in dBFS peak, that counts as "the voice is sounding"
     * @param attackS    seconds, how fast the duck engages once the send crosses the threshold
     * @param releaseS   seconds, how fast it lets go once the send falls back under it -- this is the
     *                   "opens as the voice's envelope releases" time constant the brief asks to report
     */
    void setDuck(float depth, float thresholdDb, float attackS, float releaseS);
    /**
     * @brief Processes a stereo send through the network, then ducks the wet return by the send's own
     *        envelope (setDuck()). Does not apply the bar-line cut; see barGate().
     * @param inL,inR   send input (also the duck's own sidechain)
     * @param outL,outR receives the ducked wet return (replaced)
     */
    void processDucked(const float* inL, const float* inR, float* outL, float* outR, int n);

    /**
     * @brief How open a tempo-synced bar gate is at an absolute beat, 0..1. Stateless: a pure function
     *        of @p beat, like TranceGate::open, so it needs no `this` and no per-sample state.
     * @param beat        beats since the start of the set
     * @param closeBeats  how long, from the bar line, the raised-cosine close takes to reach @p floorGain
     * @param holdBeats   how long the gate then stays at @p floorGain
     * @param openBeats   how long it then takes to reopen (raised cosine) to unity
     * @param floorGain   linear gain while closed (0 = full mute)
     */
    static float barGate(double beat, double closeBeats, double holdBeats, double openBeats, float floorGain);

private:
    static constexpr int kLines = 8;
    static constexpr int kAllpasses = 4;
    double sr_ = 48000.0;
    std::vector<float> line_[kLines], sc_[kLines], ap_[kAllpasses], apR_[kAllpasses], pre_, preR_;
    int mask_ = 0, w_ = 0;
    int apLen_[kAllpasses] = {}, scLen_[kLines] = {};
    float lenTarget_[kLines] = {}, lenCur_[kLines] = {}, gain_[kLines] = {}, lp_[kLines] = {};
    double modPh_[kLines] = {};
    float modRate_[kLines] = {};
    float preTarget_ = 0.0f, preCur_ = 0.0f;
    float damp_ = 0.4f;
    float dcX_[2] = {}, dcY_[2] = {}, dcR_ = 0.999f;
    float hcCoef_ = 1.0f, hcL_ = 0.0f, hcR_ = 0.0f;
    float lcCoef_ = 0.0f, lcL1_ = 0.0f, lcR1_ = 0.0f, lcL2_ = 0.0f, lcR2_ = 0.0f;
    // The gate's duck (20.09.2026, round "reverb"): a one-pole follower on the send's own peak, with a
    // threshold so a silent send stays fully open rather than sitting at some small fraction of depth.
    float duckDepth_ = 0.0f, duckThreshold_ = 0.01f, duckAttackC_ = 1.0f, duckReleaseC_ = 1.0f, duckEnv_ = 0.0f;
};

} // namespace eph
