/**
 * @file ModVoice.cpp
 * @brief The modular voices: the scalar side per voice, the audio in lanes.
 */
#include "eph/synth/ModVoice.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
/** @brief The half-band filter back down to the rate, designed once. */
const HalfbandDesign& halfband()
{
    static const HalfbandDesign d = designHalfband(96.0, 0.1);
    return d;
}
constexpr double DriftTau1 = 14.0;   ///< VCO 1's wander time constant, s
constexpr double DriftTau2 = 19.0;   ///< VCO 2's, so the two wander apart
constexpr int64_t ControlStep = 4;   ///< samples per control step of pitch and cutoff (a power of two)
}

void ModVoiceBank::prepare(double sampleRate, const uint64_t* seeds)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_stepBase = std::log2(440.0 / (2.0 * m_sr)) - 69.0 / 12.0;
    m_lanes = VoiceLanes{};
    m_lanes.m_comb.assign(static_cast<size_t>(BankLanes) * VoiceLanes::CombLen, 0.0f);   // the comb filters' lines
    for (int l = 0; l < BankLanes; ++l) {
        m_lanes.m_dcR[l] = 1.0f - static_cast<float>(TwoPi * 8.0 / m_sr);
        // Lanes without a voice still compute (they share a register with voices): give them numbers.
        m_lanes.m_ph1[l] = m_lanes.m_ph2[l] = 0.5f;
        m_lanes.m_drive[l] = m_lanes.m_norm[l] = 1.0f;
        for (int i = 0; i < BankSpan; ++i) {
            const int j = i * BankLanes + l;
            m_lanes.m_dt1[j] = m_lanes.m_dt2[j] = 0.001f;
            m_lanes.m_inv1[j] = m_lanes.m_inv2[j] = 1000.0f;
            m_lanes.m_g[j] = 0.1f;
        }
    }
    for (int v = 0; v < BankLanes; ++v) {
        Control& c = m_ctl[v];
        c = Control{};
        c.m_rng.seed(v < BankVoices ? seeds[v] : 0);
        c.m_filt.setSampleRate(m_sr);
        c.m_amp.setSampleRate(m_sr);
        c.m_mod.prepare(m_sr, mixSeed(v < BankVoices ? seeds[v] : 0, 0x4D4F44ull));   // its own stream: the drift's stays
        c.m_jrng.seed(mixSeed(v < BankVoices ? seeds[v] : 0, 0x4A4954ull));
    }
    reset();
    for (int v = 0; v < BankVoices; ++v) set(v, m_ctl[v].m_s);
}

void ModVoiceBank::reset()
{
    m_lanes.clearFilters();
    for (Control& c : m_ctl) {
        c.m_filt.kill();
        c.m_amp.kill();
        c.m_mod.kill();
        c.m_held = -1;
    }
    m_count = 0;
}

void ModVoiceBank::set(int v, const VoiceSettings& s)
{
    Control& c = m_ctl[v];
    c.m_s = s;
    filterTimes(c);
    c.m_amp.setTimes(s.m_ampAttackMs * 0.001f, s.m_ampDecayMs * 0.001f, std::clamp(s.m_ampSustain, 0.0f, 1.0f), s.m_releaseMs * 0.001f);
    c.m_mod.set(s.m_mod);
    c.m_glideCoef = 1.0 - std::exp(-1.0 / (std::max(1.0, static_cast<double>(s.m_glideMs)) * 0.001 * m_sr / 3.0));
    c.m_vibCoef = 1.0 - std::exp(-1.0 / (0.4 * m_sr / 3.0));
    const float drive = dbToGain(s.m_driveDb);
    m_lanes.m_drive[v] = drive;
    // Level the drive: loud enough to bend, without the voice getting louder by the same amount.
    m_lanes.m_norm[v] = 1.0f / std::sqrt(drive);
    // The filter (Filters.h): the model and the Xpander's pole mix (the knob's; shape() reads the resonance, the mode
    // and the FM, which the matrix moves); a new model starts from rest.
    const int model = std::clamp(s.m_filter, 0, FilterModels - 1);
    if (static_cast<int>(m_lanes.m_fmodel[v]) != model) m_lanes.clearFilter(v);
    m_lanes.m_fmodel[v] = static_cast<float>(model);
    {
        const float* mix = poleMix(std::clamp(s.m_filterMode, 0.0f, 1.0f));
        for (int j = 0; j < 5; ++j) m_lanes.m_pm[j][v] = mix[j];
    }
    c.m_table = s.m_table > 0 ? &wavetable(s.m_table - 1) : nullptr;
    // A VCO model, sync or cross mod: the oscillators of Vco.h, handed in as a wavetable's (a wavetable wins).
    c.m_model = s.m_vco != 0 || s.m_sync != 0 || s.m_crossMod > 0.0f;
    m_lanes.m_tbl[v] = c.m_table != nullptr || c.m_model ? 1.0f : 0.0f;
    shape(v);
}

void ModVoiceBank::filterTimes(Control& c)
{
    const VoiceSettings& s = c.m_s;
    const float decay = s.m_decayMs * 0.001f * c.m_decayMul;
    // Linked, the release is the decay's curve: Envelope's release reaches -43 dB in its time, its decay -26 dB, so the
    // same one-pole takes 5/3 of the decay's time.
    const float release = s.m_filtLink != 0 ? decay * (5.0f / 3.0f) : s.m_filtReleaseMs * 0.001f;
    c.m_filt.setTimes(s.m_filtAttackMs * 0.001f, decay, std::clamp(s.m_filtSustain, 0.0f, 1.0f), release);
}

void ModVoiceBank::shape(int v)
{
    Control& c = m_ctl[v];
    const VoiceSettings& s = c.m_s;
    const bool mod = c.m_mod.active();
    // A knob plus its matrix sum where a slot reaches it; untouched where none does, to the bit.
    auto moved = [&](float knob, ModDestE d) { return mod && c.m_mod.targets(d) ? knob + c.m_mo[static_cast<int>(d)] : knob; };
    c.m_wv = std::clamp(moved(s.m_wave, ModDestE::WAVE), 0.0f, 1.0f);
    c.m_pwv = std::clamp(moved(s.m_pulseWidth, ModDestE::PULSE_WIDTH), 0.05f, 0.95f);
    const FilterModelE fm = static_cast<FilterModelE>(std::clamp(s.m_filter, 0, FilterModels - 1));
    c.m_kv = FilterVoicing::feedback(fm, std::clamp(moved(s.m_resonance, ModDestE::RESONANCE), 0.0f, 1.0f));
    c.m_mkv = FilterVoicing::makeup(fm, c.m_kv);
    c.m_modev = std::clamp(moved(s.m_filterMode, ModDestE::FILTER_MODE), 0.0f, 1.0f);
    c.m_ffmv = 3.0f * std::clamp(moved(s.m_filterFm, ModDestE::FILTER_FM), 0.0f, 1.0f);
    c.m_levelv = mod && c.m_mod.targets(ModDestE::LEVEL) ? std::clamp(1.0f + c.m_mo[static_cast<int>(ModDestE::LEVEL)], 0.0f, 2.0f) : 1.0f;
    // The place in the table: the knob's, moved by the note's modulation step (a bright step further in), and the matrix.
    c.m_tposv = std::clamp(moved(s.m_tablePos + s.m_tableMod * c.m_noteBright / 1.5f, ModDestE::TABLE_POS), 0.0f, 1.0f);
}

void ModVoiceBank::noteOn(int v, int pitch, float velocity, bool accent, bool legato, int id, float bright, float decay)
{
    Control& c = m_ctl[v];
    c.m_target = pitch;
    if (!(legato && c.m_held >= 0)) {
        c.m_pitch = c.m_target;
        c.m_filt.noteOn();
        c.m_amp.noteOn();
        c.m_velocity = velocity;
        c.m_accentAmt = accent ? c.m_s.m_accent : 0.0f;
        c.m_noteOct = bright;
        c.m_noteBright = bright;
        c.m_decayMul = std::exp2(decay);
        filterTimes(c);
        c.m_noteCents = 0.15 * static_cast<double>(c.m_s.m_driftCents) * static_cast<double>(c.m_rng.bipolar()) * vcoProfile(c.m_s.m_vco).m_noteScatter;
        c.m_vibLevel = 0.0;
        c.m_fresh = true;
        c.m_mod.noteOn(m_count, beatAt(m_count));
        shape(v);
    }
    c.m_held = id;
}

void ModVoiceBank::noteOff(int v, int id)
{
    Control& c = m_ctl[v];
    if (id != c.m_held) return;
    c.m_held = -1;
    c.m_amp.noteOff();
    c.m_mod.noteOff();
    // The filter envelope's release. Linked with no sustain it is the decay running on, which it already does.
    if (c.m_s.m_filtLink == 0 || c.m_s.m_filtSustain > 1e-4f) c.m_filt.noteOff();
}

void ModVoiceBank::control(int v, int i)
{
    Control& c = m_ctl[v];
    const VoiceSettings& s = c.m_s;
    const int64_t at = m_count + i;
    const VcoProfile& vp = vcoProfile(s.m_vco);
    if ((at & 31) == 0) {
        // The drift, as the VCO model wanders (Vco.h; the voice's own oscillators as the knob says).
        const double dt = 32.0 / m_sr;
        c.m_drift1.step(dt, DriftTau1 * vp.m_driftTau, s.m_driftCents * vp.m_driftSpread, c.m_rng);
        c.m_drift2.step(dt, DriftTau2 * vp.m_driftTau, s.m_driftCents * vp.m_driftSpread, c.m_rng);
    }
    c.m_pitch += (c.m_target - c.m_pitch) * c.m_glideCoef;
    double vib = 0.0;
    if (s.m_vibratoCents > 0.0f) {
        c.m_vibLevel += ((c.m_held >= 0 ? 1.0 : 0.0) - c.m_vibLevel) * c.m_vibCoef;
        c.m_vibPhase += static_cast<double>(s.m_vibratoHz) / m_sr;
        if (c.m_vibPhase >= 1.0) c.m_vibPhase -= 1.0;
        vib = static_cast<double>(s.m_vibratoCents) * c.m_vibLevel * static_cast<double>(sin01(c.m_vibPhase));
    }
    const float fe = c.m_filt.process();
    c.m_mod.tick();
    float gain = c.m_amp.process() * c.m_velocity * (1.0f + 0.4f * c.m_accentAmt);
    const bool mod = c.m_mod.active();
    if (c.m_fresh || (at & (ControlStep - 1)) == 0) {
        c.m_fresh = false;
        if (mod) {
            // The matrix at this control step; its sources besides the LFOs and the modulation envelope.
            float ext[ModSources] = {};
            ext[static_cast<int>(ModSourceE::FILTER_ENV)] = fe;
            ext[static_cast<int>(ModSourceE::VELOCITY)] = c.m_velocity;
            ext[static_cast<int>(ModSourceE::MOD_LANE)] = std::clamp(c.m_noteBright / 1.5f, -1.0f, 1.0f);
            c.m_mod.evaluate(at, beatAt(at), ext, c.m_mo);
            shape(v);
        }
        const double sr2 = 2.0 * m_sr;
        // The phase step straight from the note: 440 Hz * 2^((note - 69) / 12) / (2 fs), one exp2.
        auto step = [this](double note, float& dt, float& inv) {
            dt = static_cast<float>(std::clamp(std::exp2(m_stepBase + note / 12.0), 1e-7, 0.45));
            inv = 1.0f / dt;
        };
        double pitch = c.m_pitch;
        if (mod && c.m_mod.targets(ModDestE::PITCH)) pitch += static_cast<double>(c.m_mo[static_cast<int>(ModDestE::PITCH)]);
        double first = pitch + (c.m_drift1.m_x + c.m_noteCents + vib) * 0.01;
        double second = pitch + (c.m_drift2.m_x + c.m_noteCents + vib + s.m_detuneCents) * 0.01;
        if (vp.m_jitterCents > 0.0f) {
            // The model's jitter: 4 ms of correlation, on the control raster.
            const double dt = static_cast<double>(ControlStep) / m_sr;
            first += 0.01 * c.m_jitter1.step(dt, 0.004, vp.m_jitterCents, c.m_jrng);
            second += 0.01 * c.m_jitter2.step(dt, 0.004, vp.m_jitterCents, c.m_jrng);
        }
        // VCO 2's own interval, and the matrix's on it (the sync sweep).
        if (s.m_osc2Semis != 0.0f) second += static_cast<double>(s.m_osc2Semis);
        if (mod && c.m_mod.targets(ModDestE::OSC2_PITCH)) second += static_cast<double>(c.m_mo[static_cast<int>(ModDestE::OSC2_PITCH)]);
        step(first, c.m_dt1, c.m_inv1);
        step(second, c.m_dt2, c.m_inv2);
        float env = s.m_envOctaves * fe * (1.0f + c.m_accentAmt);
        if (s.m_envVelocity > 0.0f) env *= 1.0f - s.m_envVelocity + s.m_envVelocity * c.m_velocity;
        float octs = env + s.m_keyTrack * static_cast<float>((c.m_pitch - 60.0) / 12.0) + c.m_noteOct;
        if (mod && c.m_mod.targets(ModDestE::CUTOFF)) octs += c.m_mo[static_cast<int>(ModDestE::CUTOFF)];
        const float fc = std::min(static_cast<float>(0.42 * sr2), s.m_cutoffHz * std::exp2(octs));
        c.m_g = std::tan(Pi * fc / static_cast<float>(sr2));
    }
    if (mod && c.m_mod.targets(ModDestE::LEVEL)) gain *= c.m_levelv;
    const int j = i * BankLanes + v;
    m_lanes.m_dt1[j] = c.m_dt1; m_lanes.m_inv1[j] = c.m_inv1;
    m_lanes.m_dt2[j] = c.m_dt2; m_lanes.m_inv2[j] = c.m_inv2;
    m_lanes.m_g[j] = c.m_g;
    m_lanes.m_gain[j] = gain;
    m_lanes.m_wave[j] = c.m_wv; m_lanes.m_pw[j] = c.m_pwv; m_lanes.m_k[j] = c.m_kv; m_lanes.m_fmk[j] = c.m_mkv;
    m_lanes.m_fmode[j] = c.m_modev; m_lanes.m_ffm[j] = c.m_ffmv;
    m_tpos[v][i] = c.m_tposv;
}

void ModVoiceBank::process(const bool* run, int n)
{
    n = std::min(n, BankSpan);
    // The whole bank runs when any voice does: the same decision on every vector path. Its registers
    // run side by side (voiceKernel), which is what makes the second one nearly free.
    // Whether any lane blends in the pulse or modulates its cutoff: from the settings (they change only at a cell), so
    // the same whatever the host's blocks.
    bool any = false, pulse = false, fm = false;
    for (int v = 0; v < BankVoices; ++v) {
        const Control& c = m_ctl[v];
        const bool mod = c.m_mod.active();
        any = any || run[v];
        pulse = pulse || c.m_s.m_wave > 0.0f || (mod && c.m_mod.targets(ModDestE::WAVE));
        fm = fm || c.m_s.m_filterFm > 0.0f || (mod && c.m_mod.targets(ModDestE::FILTER_FM));
    }
    if (any) {
        for (int v = 0; v < BankVoices; ++v) {
            if (run[v]) {
                for (int i = 0; i < n; ++i) control(v, i);
            } else {
                // A silent voice: its oscillators run on at the last step, its VCA shut.
                const Control& c = m_ctl[v];
                for (int i = 0; i < n; ++i) {
                    const int j = i * BankLanes + v;
                    m_lanes.m_dt1[j] = c.m_dt1; m_lanes.m_inv1[j] = c.m_inv1;
                    m_lanes.m_dt2[j] = c.m_dt2; m_lanes.m_inv2[j] = c.m_inv2;
                    m_lanes.m_g[j] = c.m_g;
                    m_lanes.m_gain[j] = 0.0f;
                    m_lanes.m_wave[j] = c.m_wv; m_lanes.m_pw[j] = c.m_pwv; m_lanes.m_k[j] = c.m_kv; m_lanes.m_fmk[j] = c.m_mkv;
                    m_lanes.m_fmode[j] = c.m_modev; m_lanes.m_ffm[j] = c.m_ffmv;
                    m_tpos[v][i] = c.m_tposv;
                }
            }
        }
        // The wavetables (25.09.2026): a voice that reads one gets its two oscillators here, at the lanes' twice the
        // rate and on their steps, and the kernel takes them instead of its own. The place in the table per sample
        // (shape()); the level a little up, so a table sits where the saw sat.
        bool table = false;
        for (int v = 0; v < BankVoices; ++v) {
            Control& c = m_ctl[v];
            if (c.m_table == nullptr) continue;
            table = true;
            const double sr2 = 2.0 * m_sr;
            c.m_wlev1 = cycleLevelFor(static_cast<double>(m_lanes.m_dt1[v]) * sr2, sr2, c.m_wlev1);
            c.m_wlev2 = cycleLevelFor(static_cast<double>(m_lanes.m_dt2[v]) * sr2, sr2, c.m_wlev2);
            for (int i = 0; i < n; ++i) {
                const float pos = m_tpos[v][i];
                const double d1 = m_lanes.m_dt1[i * BankLanes + v], d2 = m_lanes.m_dt2[i * BankLanes + v];
                for (int h = 0; h < 2; ++h) {
                    const int w = (2 * i + h) * BankLanes + v;
                    m_lanes.m_wt1[w] = 1.6f * c.m_table->at(c.m_wlev1, pos, c.m_wph1);
                    m_lanes.m_wt2[w] = 1.6f * c.m_table->at(c.m_wlev2, pos, c.m_wph2);
                    c.m_wph1 += d1; if (c.m_wph1 >= 1.0) c.m_wph1 -= 1.0;
                    c.m_wph2 += d2; if (c.m_wph2 >= 1.0) c.m_wph2 -= 1.0;
                }
            }
        }
        // The classic VCOs (Vco.h): a voice with a model, sync or cross mod plays these, at the lanes' twice the rate and
        // on their steps, into the kernel's wavetable inputs (a wavetable, where one is chosen, wins).
        for (int v = 0; v < BankVoices; ++v) {
            Control& c = m_ctl[v];
            if (!c.m_model || c.m_table != nullptr) continue;
            table = true;
            const VcoProfile& vp = vcoProfile(c.m_s.m_vco);
            const bool sync = c.m_s.m_sync != 0;
            const double xm = 3.0 * std::clamp(static_cast<double>(c.m_s.m_crossMod), 0.0, 1.0);
            for (int i = 0; i < n; ++i) {
                const int j = i * BankLanes + v;
                const double d1 = m_lanes.m_dt1[j], d2 = m_lanes.m_dt2[j];
                const float wave = m_lanes.m_wave[j], pw = m_lanes.m_pw[j];
                for (int h = 0; h < 2; ++h) {
                    double wrap = -1.0;
                    const float y1 = c.m_osc1.step(d1, vp, wave, pw, -1.0, sync ? &wrap : nullptr);
                    const double d2x = xm > 0.0 ? std::min(0.45, d2 * std::exp2(xm * static_cast<double>(y1))) : d2;
                    const float y2 = c.m_osc2.step(d2x, vp, wave, pw, sync ? wrap : -1.0, nullptr);
                    const int w = (2 * i + h) * BankLanes + v;
                    m_lanes.m_wt1[w] = y1;
                    m_lanes.m_wt2[w] = y2;
                }
            }
        }
        // The filter models each register's voices use: a register computes only its own (the lanes without a voice
        // take none, their output unread).
        constexpr int regs = (BankVoices + VecWidth - 1) / VecWidth;
        unsigned models[regs] = {};
        for (int v = 0; v < BankVoices; ++v) models[v / VecWidth] |= 1u << std::clamp(static_cast<int>(m_lanes.m_fmodel[v]), 0, FilterModels - 1);
        voiceKernel<VecF, regs>(m_lanes, halfband(), 0, n, pulse, m_mixed, table, models, fm);
        for (int v = 0; v < BankVoices; ++v)
            if (run[v]) for (int i = 0; i < n; ++i) m_out[v][i] = m_mixed[i * BankLanes + v];
    }
    m_count += n;
}

} // namespace eph
