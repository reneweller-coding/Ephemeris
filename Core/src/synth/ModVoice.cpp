/**
 * @file ModVoice.cpp
 * @brief The modular voices: the scalar side per voice, the audio in lanes.
 */
#include "eph/synth/ModVoice.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
const HalfbandDesign& halfband()
{
    static const HalfbandDesign d = designHalfband(96.0, 0.1);
    return d;
}
constexpr double kDriftTau1 = 14.0, kDriftTau2 = 19.0;   ///< seconds; two, so the VCOs wander apart
constexpr int64_t kControl = 4;   ///< samples per control step of pitch and cutoff (a power of two)
}

void ModVoiceBank::prepare(double sampleRate, const uint64_t* seeds)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    stepBase_ = std::log2(440.0 / (2.0 * sr_)) - 69.0 / 12.0;
    lanes_ = VoiceLanes{};
    for (int l = 0; l < kBankLanes; ++l) {
        lanes_.dcR[l] = 1.0f - static_cast<float>(kTwoPi * 8.0 / sr_);
        // Lanes without a voice still compute (they share a register with voices): give them numbers.
        lanes_.ph1[l] = lanes_.ph2[l] = 0.5f;
        lanes_.drive[l] = lanes_.norm[l] = 1.0f;
        for (int i = 0; i < kBankSpan; ++i) {
            const int j = i * kBankLanes + l;
            lanes_.dt1[j] = lanes_.dt2[j] = 0.001f;
            lanes_.inv1[j] = lanes_.inv2[j] = 1000.0f;
            lanes_.g[j] = 0.1f;
        }
    }
    for (int v = 0; v < kBankLanes; ++v) {
        Control& c = ctl_[v];
        c = Control{};
        c.rng.seed(v < kBankVoices ? seeds[v] : 0);
        c.filt.setSampleRate(sr_);
        c.amp.setSampleRate(sr_);
    }
    reset();
    for (int v = 0; v < kBankVoices; ++v) set(v, ctl_[v].s);
}

void ModVoiceBank::reset()
{
    lanes_.clearFilters();
    for (Control& c : ctl_) {
        c.filt.kill();
        c.amp.kill();
        c.held = -1;
    }
    count_ = 0;
}

void ModVoiceBank::set(int v, const VoiceSettings& s)
{
    Control& c = ctl_[v];
    c.s = s;
    c.filt.setTimes(0.0015f, s.decayMs * 0.001f * c.decayMul, 0.0f, s.decayMs * 0.001f * c.decayMul);
    c.amp.setTimes(0.002f, 0.05f, 1.0f, s.releaseMs * 0.001f);
    c.glideCoef = 1.0 - std::exp(-1.0 / (std::max(1.0, static_cast<double>(s.glideMs)) * 0.001 * sr_ / 3.0));
    c.vibCoef = 1.0 - std::exp(-1.0 / (0.4 * sr_ / 3.0));
    const float drive = dbToGain(s.driveDb);
    lanes_.drive[v] = drive;
    // Level the drive: loud enough to bend, without the voice getting louder by the same amount.
    lanes_.norm[v] = 1.0f / std::sqrt(drive);
    lanes_.wave[v] = std::clamp(s.wave, 0.0f, 1.0f);
    lanes_.pw[v] = std::clamp(s.pulseWidth, 0.05f, 0.95f);
    lanes_.k[v] = 4.0f * std::clamp(s.resonance, 0.0f, 1.0f) * 0.985f;
}

void ModVoiceBank::noteOn(int v, int pitch, float velocity, bool accent, bool legato, int id, float bright, float decay)
{
    Control& c = ctl_[v];
    c.target = pitch;
    if (!(legato && c.held >= 0)) {
        c.pitch = c.target;
        c.filt.noteOn();
        c.amp.noteOn();
        c.velocity = velocity;
        c.accentAmt = accent ? c.s.accent : 0.0f;
        c.noteOct = bright;
        c.decayMul = std::exp2(decay);
        c.filt.setTimes(0.0015f, c.s.decayMs * 0.001f * c.decayMul, 0.0f, c.s.decayMs * 0.001f * c.decayMul);
        c.noteCents = 0.15 * static_cast<double>(c.s.driftCents) * static_cast<double>(c.rng.bipolar());
        c.vibLevel = 0.0;
        c.fresh = true;
    }
    c.held = id;
}

void ModVoiceBank::noteOff(int v, int id)
{
    Control& c = ctl_[v];
    if (id != c.held) return;
    c.held = -1;
    c.amp.noteOff();
}

void ModVoiceBank::control(int v, int i)
{
    Control& c = ctl_[v];
    const VoiceSettings& s = c.s;
    const int64_t at = count_ + i;
    if ((at & 31) == 0) {
        const double dt = 32.0 / sr_;
        c.drift1.step(dt, kDriftTau1, s.driftCents, c.rng);
        c.drift2.step(dt, kDriftTau2, s.driftCents, c.rng);
    }
    c.pitch += (c.target - c.pitch) * c.glideCoef;
    double vib = 0.0;
    if (s.vibratoCents > 0.0f) {
        c.vibLevel += ((c.held >= 0 ? 1.0 : 0.0) - c.vibLevel) * c.vibCoef;
        c.vibPhase += static_cast<double>(s.vibratoHz) / sr_;
        if (c.vibPhase >= 1.0) c.vibPhase -= 1.0;
        vib = static_cast<double>(s.vibratoCents) * c.vibLevel * static_cast<double>(sin01(c.vibPhase));
    }
    const float fe = c.filt.process();
    const float gain = c.amp.process() * c.velocity * (1.0f + 0.4f * c.accentAmt);
    if (c.fresh || (at & (kControl - 1)) == 0) {
        c.fresh = false;
        const double sr2 = 2.0 * sr_;
        // The phase step straight from the note: 440 Hz * 2^((note - 69) / 12) / (2 fs), one exp2.
        auto step = [this](double note, float& dt, float& inv) {
            dt = static_cast<float>(std::clamp(std::exp2(stepBase_ + note / 12.0), 1e-7, 0.45));
            inv = 1.0f / dt;
        };
        step(c.pitch + (c.drift1.x + c.noteCents + vib) * 0.01, c.dt1, c.inv1);
        step(c.pitch + (c.drift2.x + c.noteCents + vib + s.detuneCents) * 0.01, c.dt2, c.inv2);
        const float octs = s.envOctaves * fe * (1.0f + c.accentAmt) + s.keyTrack * static_cast<float>((c.pitch - 60.0) / 12.0) + c.noteOct;
        const float fc = std::min(static_cast<float>(0.42 * sr2), s.cutoffHz * std::exp2(octs));
        c.g = std::tan(kPi * fc / static_cast<float>(sr2));
    }
    const int j = i * kBankLanes + v;
    lanes_.dt1[j] = c.dt1; lanes_.inv1[j] = c.inv1;
    lanes_.dt2[j] = c.dt2; lanes_.inv2[j] = c.inv2;
    lanes_.g[j] = c.g;
    lanes_.gain[j] = gain;
}

void ModVoiceBank::process(const bool* run, int n)
{
    n = std::min(n, kBankSpan);
    // The whole bank runs when any voice does: the same decision on every vector path. Its registers
    // run side by side (voiceKernel), which is what makes the second one nearly free.
    bool any = false, pulse = false;
    for (int v = 0; v < kBankVoices; ++v) {
        any = any || run[v];
        pulse = pulse || lanes_.wave[v] > 0.0f;
    }
    if (any) {
        for (int v = 0; v < kBankVoices; ++v) {
            if (run[v]) {
                for (int i = 0; i < n; ++i) control(v, i);
            } else {
                // A silent voice: its oscillators run on at the last step, its VCA shut.
                const Control& c = ctl_[v];
                for (int i = 0; i < n; ++i) {
                    const int j = i * kBankLanes + v;
                    lanes_.dt1[j] = c.dt1; lanes_.inv1[j] = c.inv1;
                    lanes_.dt2[j] = c.dt2; lanes_.inv2[j] = c.inv2;
                    lanes_.g[j] = c.g;
                    lanes_.gain[j] = 0.0f;
                }
            }
        }
        constexpr int regs = (kBankVoices + kVecWidth - 1) / kVecWidth;
        voiceKernel<VecF, regs>(lanes_, halfband(), 0, n, pulse, mixed_);
        for (int v = 0; v < kBankVoices; ++v)
            if (run[v]) for (int i = 0; i < n; ++i) out_[v][i] = mixed_[i * kBankLanes + v];
    }
    count_ += n;
}

} // namespace eph
