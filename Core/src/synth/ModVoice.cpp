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
    lanes_.comb.assign(static_cast<size_t>(kBankLanes) * VoiceLanes::kCombLen, 0.0f);   // the comb filters' lines
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
        c.mod.prepare(sr_, mixSeed(v < kBankVoices ? seeds[v] : 0, 0x4D4F44ull));   // its own stream: the drift's stays
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
        c.mod.kill();
        c.held = -1;
    }
    count_ = 0;
}

void ModVoiceBank::set(int v, const VoiceSettings& s)
{
    Control& c = ctl_[v];
    c.s = s;
    filterTimes(c);
    c.amp.setTimes(s.ampAttackMs * 0.001f, s.ampDecayMs * 0.001f, std::clamp(s.ampSustain, 0.0f, 1.0f), s.releaseMs * 0.001f);
    c.mod.set(s.mod);
    c.glideCoef = 1.0 - std::exp(-1.0 / (std::max(1.0, static_cast<double>(s.glideMs)) * 0.001 * sr_ / 3.0));
    c.vibCoef = 1.0 - std::exp(-1.0 / (0.4 * sr_ / 3.0));
    const float drive = dbToGain(s.driveDb);
    lanes_.drive[v] = drive;
    // Level the drive: loud enough to bend, without the voice getting louder by the same amount.
    lanes_.norm[v] = 1.0f / std::sqrt(drive);
    // The filter (Filters.h): the model and the Xpander's pole mix (the knob's; shape() reads the resonance, the mode
    // and the FM, which the matrix moves); a new model starts from rest.
    const int model = std::clamp(s.filter, 0, kFilterModels - 1);
    if (static_cast<int>(lanes_.fmodel[v]) != model) lanes_.clearFilter(v);
    lanes_.fmodel[v] = static_cast<float>(model);
    {
        const float* mix = poleMix(std::clamp(s.filterMode, 0.0f, 1.0f));
        for (int j = 0; j < 5; ++j) lanes_.pm[j][v] = mix[j];
    }
    c.table = s.table > 0 ? &wavetable(s.table - 1) : nullptr;
    lanes_.tbl[v] = c.table != nullptr ? 1.0f : 0.0f;
    shape(v);
}

void ModVoiceBank::filterTimes(Control& c)
{
    const VoiceSettings& s = c.s;
    const float decay = s.decayMs * 0.001f * c.decayMul;
    // Linked, the release is the decay's curve: Envelope's release reaches -43 dB in its time, its decay -26 dB, so the
    // same one-pole takes 5/3 of the decay's time.
    const float release = s.filtLink != 0 ? decay * (5.0f / 3.0f) : s.filtReleaseMs * 0.001f;
    c.filt.setTimes(s.filtAttackMs * 0.001f, decay, std::clamp(s.filtSustain, 0.0f, 1.0f), release);
}

void ModVoiceBank::shape(int v)
{
    Control& c = ctl_[v];
    const VoiceSettings& s = c.s;
    const bool mod = c.mod.active();
    // A knob plus its matrix sum where a slot reaches it; untouched where none does, to the bit.
    auto moved = [&](float knob, ModDest d) { return mod && c.mod.targets(d) ? knob + c.mo[static_cast<int>(d)] : knob; };
    c.wv = std::clamp(moved(s.wave, ModDest::Wave), 0.0f, 1.0f);
    c.pwv = std::clamp(moved(s.pulseWidth, ModDest::PulseWidth), 0.05f, 0.95f);
    const FilterModel fm = static_cast<FilterModel>(std::clamp(s.filter, 0, kFilterModels - 1));
    c.kv = FilterVoicing::feedback(fm, std::clamp(moved(s.resonance, ModDest::Resonance), 0.0f, 1.0f));
    c.mkv = FilterVoicing::makeup(fm, c.kv);
    c.modev = std::clamp(moved(s.filterMode, ModDest::FilterMode), 0.0f, 1.0f);
    c.ffmv = 3.0f * std::clamp(moved(s.filterFm, ModDest::FilterFm), 0.0f, 1.0f);
    c.levelv = mod && c.mod.targets(ModDest::Level) ? std::clamp(1.0f + c.mo[static_cast<int>(ModDest::Level)], 0.0f, 2.0f) : 1.0f;
    // The place in the table: the knob's, moved by the note's modulation step (a bright step further in), and the matrix.
    c.tposv = std::clamp(moved(s.tablePos + s.tableMod * c.noteBright / 1.5f, ModDest::TablePos), 0.0f, 1.0f);
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
        c.noteBright = bright;
        c.decayMul = std::exp2(decay);
        filterTimes(c);
        c.noteCents = 0.15 * static_cast<double>(c.s.driftCents) * static_cast<double>(c.rng.bipolar());
        c.vibLevel = 0.0;
        c.fresh = true;
        c.mod.noteOn(count_, beatAt(count_));
        shape(v);
    }
    c.held = id;
}

void ModVoiceBank::noteOff(int v, int id)
{
    Control& c = ctl_[v];
    if (id != c.held) return;
    c.held = -1;
    c.amp.noteOff();
    c.mod.noteOff();
    // The filter envelope's release. Linked with no sustain it is the decay running on, which it already does.
    if (c.s.filtLink == 0 || c.s.filtSustain > 1e-4f) c.filt.noteOff();
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
    c.mod.tick();
    float gain = c.amp.process() * c.velocity * (1.0f + 0.4f * c.accentAmt);
    const bool mod = c.mod.active();
    if (c.fresh || (at & (kControl - 1)) == 0) {
        c.fresh = false;
        if (mod) {
            // The matrix at this control step; its sources besides the LFOs and the modulation envelope.
            float ext[kModSources] = {};
            ext[static_cast<int>(ModSource::FilterEnv)] = fe;
            ext[static_cast<int>(ModSource::Velocity)] = c.velocity;
            ext[static_cast<int>(ModSource::ModLane)] = std::clamp(c.noteBright / 1.5f, -1.0f, 1.0f);
            c.mod.evaluate(at, beatAt(at), ext, c.mo);
            shape(v);
        }
        const double sr2 = 2.0 * sr_;
        // The phase step straight from the note: 440 Hz * 2^((note - 69) / 12) / (2 fs), one exp2.
        auto step = [this](double note, float& dt, float& inv) {
            dt = static_cast<float>(std::clamp(std::exp2(stepBase_ + note / 12.0), 1e-7, 0.45));
            inv = 1.0f / dt;
        };
        double pitch = c.pitch;
        if (mod && c.mod.targets(ModDest::Pitch)) pitch += static_cast<double>(c.mo[static_cast<int>(ModDest::Pitch)]);
        step(pitch + (c.drift1.x + c.noteCents + vib) * 0.01, c.dt1, c.inv1);
        step(pitch + (c.drift2.x + c.noteCents + vib + s.detuneCents) * 0.01, c.dt2, c.inv2);
        float env = s.envOctaves * fe * (1.0f + c.accentAmt);
        if (s.envVelocity > 0.0f) env *= 1.0f - s.envVelocity + s.envVelocity * c.velocity;
        float octs = env + s.keyTrack * static_cast<float>((c.pitch - 60.0) / 12.0) + c.noteOct;
        if (mod && c.mod.targets(ModDest::Cutoff)) octs += c.mo[static_cast<int>(ModDest::Cutoff)];
        const float fc = std::min(static_cast<float>(0.42 * sr2), s.cutoffHz * std::exp2(octs));
        c.g = std::tan(kPi * fc / static_cast<float>(sr2));
    }
    if (mod && c.mod.targets(ModDest::Level)) gain *= c.levelv;
    const int j = i * kBankLanes + v;
    lanes_.dt1[j] = c.dt1; lanes_.inv1[j] = c.inv1;
    lanes_.dt2[j] = c.dt2; lanes_.inv2[j] = c.inv2;
    lanes_.g[j] = c.g;
    lanes_.gain[j] = gain;
    lanes_.wave[j] = c.wv; lanes_.pw[j] = c.pwv; lanes_.k[j] = c.kv; lanes_.fmk[j] = c.mkv;
    lanes_.fmode[j] = c.modev; lanes_.ffm[j] = c.ffmv;
    tpos_[v][i] = c.tposv;
}

void ModVoiceBank::process(const bool* run, int n)
{
    n = std::min(n, kBankSpan);
    // The whole bank runs when any voice does: the same decision on every vector path. Its registers
    // run side by side (voiceKernel), which is what makes the second one nearly free.
    // Whether any lane blends in the pulse or modulates its cutoff: from the settings (they change only at a cell), so
    // the same whatever the host's blocks.
    bool any = false, pulse = false, fm = false;
    for (int v = 0; v < kBankVoices; ++v) {
        const Control& c = ctl_[v];
        const bool mod = c.mod.active();
        any = any || run[v];
        pulse = pulse || c.s.wave > 0.0f || (mod && c.mod.targets(ModDest::Wave));
        fm = fm || c.s.filterFm > 0.0f || (mod && c.mod.targets(ModDest::FilterFm));
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
                    lanes_.wave[j] = c.wv; lanes_.pw[j] = c.pwv; lanes_.k[j] = c.kv; lanes_.fmk[j] = c.mkv;
                    lanes_.fmode[j] = c.modev; lanes_.ffm[j] = c.ffmv;
                    tpos_[v][i] = c.tposv;
                }
            }
        }
        // The wavetables (25.09.2026): a voice that reads one gets its two oscillators here, at the lanes' twice the
        // rate and on their steps, and the kernel takes them instead of its own. The place in the table per sample
        // (shape()); the level a little up, so a table sits where the saw sat.
        bool table = false;
        for (int v = 0; v < kBankVoices; ++v) {
            Control& c = ctl_[v];
            if (c.table == nullptr) continue;
            table = true;
            const double sr2 = 2.0 * sr_;
            c.wlev1 = cycleLevelFor(static_cast<double>(lanes_.dt1[v]) * sr2, sr2, c.wlev1);
            c.wlev2 = cycleLevelFor(static_cast<double>(lanes_.dt2[v]) * sr2, sr2, c.wlev2);
            for (int i = 0; i < n; ++i) {
                const float pos = tpos_[v][i];
                const double d1 = lanes_.dt1[i * kBankLanes + v], d2 = lanes_.dt2[i * kBankLanes + v];
                for (int h = 0; h < 2; ++h) {
                    const int w = (2 * i + h) * kBankLanes + v;
                    lanes_.wt1[w] = 1.6f * c.table->at(c.wlev1, pos, c.wph1);
                    lanes_.wt2[w] = 1.6f * c.table->at(c.wlev2, pos, c.wph2);
                    c.wph1 += d1; if (c.wph1 >= 1.0) c.wph1 -= 1.0;
                    c.wph2 += d2; if (c.wph2 >= 1.0) c.wph2 -= 1.0;
                }
            }
        }
        // The filter models the bank's voices use (the kernel computes those).
        unsigned models = 0u;
        for (int v = 0; v < kBankVoices; ++v) models |= 1u << std::clamp(static_cast<int>(lanes_.fmodel[v]), 0, kFilterModels - 1);
        constexpr int regs = (kBankVoices + kVecWidth - 1) / kVecWidth;
        voiceKernel<VecF, regs>(lanes_, halfband(), 0, n, pulse, mixed_, table, models, fm);
        for (int v = 0; v < kBankVoices; ++v)
            if (run[v]) for (int i = 0; i < n; ++i) out_[v][i] = mixed_[i * kBankLanes + v];
    }
    count_ += n;
}

} // namespace eph
