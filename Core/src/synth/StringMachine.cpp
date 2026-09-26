/**
 * @file StringMachine.cpp
 * @brief Divide-down registers, the registration, the ensemble and the phaser.
 */
#include "eph/synth/StringMachine.h"
#include "eph/synth/Oscillator.h"   // polyBlep
#include <algorithm>
#include <cmath>
#include <cstring>

namespace eph {

namespace {
constexpr double kTopOctaveMidi = 108.0;   ///< C8: the top-octave generator's C
constexpr double kModulus = 256.0;         ///< the counter wraps after eight octaves of division

/**
 * @brief A registration: the weights of saw 16', saw 8', saw 4', square 8', square 4', a tone factor, and a trim
 *        measured so that every registration plays a held chord as loud as the Violins (selftest "string
 *        machine"; a saw and a square of one footage are phase-locked and partly cancel, which the plain
 *        normalisation by the weights cannot know).
 */
struct Registration { const char* name; float w[StringMachine::kRegisters]; float tone; float trim; };
const Registration kRegTable[StringMachine::kRegistrations] = {
    { "Violins", { 0.00f, 1.00f, 0.55f, 0.00f, 0.00f }, 1.25f, 1.000f },
    { "Violas",  { 0.00f, 0.85f, 0.20f, 0.45f, 0.00f }, 1.00f, 2.100f },
    { "Cellos",  { 0.55f, 0.90f, 0.00f, 0.20f, 0.00f }, 0.70f, 1.236f },
    { "Basses",  { 1.00f, 0.45f, 0.00f, 0.00f, 0.00f }, 0.50f, 1.046f },
    { "Full",    { 0.60f, 1.00f, 0.60f, 0.00f, 0.00f }, 1.10f, 0.867f },
    { "Hollow",  { 0.00f, 0.30f, 0.00f, 1.00f, 0.45f }, 0.85f, 0.908f },
    { "Brass",   { 0.20f, 1.00f, 0.30f, 0.50f, 0.00f }, 1.60f, 1.673f },
    { "Organ",   { 0.45f, 0.00f, 0.00f, 1.00f, 0.70f }, 0.90f, 0.885f },
};

float frac(double x) { return static_cast<float>(x - std::floor(x)); }
} // namespace

const char* StringMachine::registrationName(int i) { return kRegTable[std::clamp(i, 0, kRegistrations - 1)].name; }

void StringMachine::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    size_t n = 4;
    while (static_cast<double>(n) < 0.04 * sr_) n <<= 1;
    line_.assign(n, 0.0f);
    mask_ = n - 1;
    write_ = 0;
    // The twelve top-octave generators start where the seed puts them, as a machine's do at power-on.
    Rng rng;
    rng.seed(seed);
    for (double& c : counter_) c = kModulus * static_cast<double>(rng.uniform());
    for (Key& k : keys_) k = Key{};
    slow_ = fast_ = animPhase_ = phPhase_ = 0.0;
    count_ = 0;
    tone_.reset();
    for (Svf& b : bbd_) { b.reset(); b.set(7000.0f, 0.0f, static_cast<float>(sr_)); }
    for (int c = 0; c < 2; ++c) { for (int i = 0; i < 4; ++i) apX_[c][i] = apY_[c][i] = 0.0f; phFb_[c] = 0.0f; }
    ring_ = 0.0f;
    mod_.prepare(sr_, mixSeed(seed, 0x5354ull));
    mod_.set(s_.mod);
    pitchMul_ = 1.0;
    toneMul_ = 1.0f;
    set(s_);
}

void StringMachine::set(const StringSettings& s)
{
    if (std::memcmp(&s.mod, &s_.mod, sizeof(ModSettings)) != 0) mod_.set(s.mod);
    s_ = s;
}

void StringMachine::registrationAt(float position)
{
    const float x = std::clamp(position, 0.0f, static_cast<float>(kRegistrations - 1));
    const int i = std::min(static_cast<int>(x), kRegistrations - 2);
    const float f = x - static_cast<float>(i);
    const Registration& a = kRegTable[i];
    const Registration& b = kRegTable[i + 1];
    // The footage balance tilts the mix: low footages (16') against high ones (4').
    const float low = 1.5f - s_.feet, high = 0.5f + s_.feet;
    const float tilt[kRegisters] = { low, 1.0f, high, 1.0f, high };
    float sumSq = 0.0f;
    for (int r = 0; r < kRegisters; ++r) {
        weight_[r] = (a.w[r] + f * (b.w[r] - a.w[r])) * tilt[r];
        sumSq += weight_[r] * weight_[r];
    }
    // The same loudness for every registration: the Violins' 8' and 4' saws at 0.4 are the reference.
    regGain_ = 1.077f / std::sqrt(std::max(sumSq, 1e-4f)) * (a.trim + f * (b.trim - a.trim));
    float tone = s_.toneHz * (a.tone + f * (b.tone - a.tone));
    if (mod_.active() && mod_.targets(ModDest::Cutoff)) tone *= toneMul_;
    tone = std::min(tone, static_cast<float>(0.45 * sr_));
    tone_.set(tone, 0.1f, static_cast<float>(sr_));
}

void StringMachine::noteOn(int pitch, float velocity, int id)
{
    Key* slot = nullptr;
    for (Key& k : keys_) if (!k.on) { slot = &k; break; }
    if (slot == nullptr) {
        slot = &keys_[0];
        for (Key& k : keys_) if (k.order < slot->order) slot = &k;
    }
    slot->on = slot->held = true;
    slot->pitch = pitch;
    slot->id = id;
    slot->velocity = velocity;
    slot->order = ++order_;
    slot->pc = pitchClass(pitch);
    const int octDown = static_cast<int>(std::lround((kTopOctaveMidi + slot->pc - pitch) / 12.0));   // divisions below the top
    slot->inv8 = std::ldexp(1.0, -std::clamp(octDown, 0, 8));
    slot->inv16 = std::ldexp(1.0, -std::clamp(octDown + 1, 0, 8));
    slot->inv4 = std::ldexp(1.0, -std::clamp(octDown - 1, 0, 8));
    // The envelope is not reset: a stolen key glides from where it was, as the machine's did.
    slot->decaying = false;
    mod_.noteOn(count_, beatAt(count_));
}

void StringMachine::noteOff(int id)
{
    for (Key& k : keys_) if (k.on && k.id == id) k.held = false;
    bool held = false;
    for (const Key& k : keys_) held = held || (k.on && k.held);
    if (!held) mod_.noteOff();
}

void StringMachine::silence()
{
    for (Key& k : keys_) k = Key{};
    std::fill(line_.begin(), line_.end(), 0.0f);
    ring_ = 0.0f;
    mod_.kill();
}

bool StringMachine::active() const
{
    for (const Key& k : keys_) if (k.on) return true;
    return ring_ > 1e-5f;
}

void StringMachine::process(float* L, float* R, int n)
{
    const double inv = 1.0 / sr_;
    const float att = static_cast<float>(1.0 - std::exp(-inv / std::max(0.005, static_cast<double>(s_.attackS) / 3.0)));
    const float rel = static_cast<float>(1.0 - std::exp(-inv / std::max(0.01, static_cast<double>(s_.releaseS) / 3.0)));
    const float dec = static_cast<float>(1.0 - std::exp(-inv / std::max(0.01, static_cast<double>(s_.decayS) / 3.0)));
    const float sus = std::clamp(s_.sustain, 0.0f, 1.0f);
    // Top-octave increments per pitch class (in counter units per sample).
    double inc[12];
    for (int pc = 0; pc < 12; ++pc) inc[pc] = midiToHz(kTopOctaveMidi + pc) * inv;
    // The ensemble's shape by type: base delay, slow and fast depth (ms), their rates, the taps and the spread.
    const int type = std::clamp(s_.ensembleType, 0, 2);
    static const double kBase[3] = { 6.0, 8.0, 10.0 }, kSlowMs[3] = { 2.0, 2.5, 3.5 }, kFastMs[3] = { 0.35, 0.0, 0.5 };
    static const double kSlowHz[3] = { 0.6, 0.8, 0.25 }, kFastHz[3] = { 6.0, 6.0, 5.0 };
    static const float kSide[3] = { 0.6f, 1.0f, 0.7f };   // the share of the outer tap on each side
    const int taps = type == 1 ? 2 : 3;
    const float e = s_.ensemble;
    for (int i = 0; i < n; ++i) {
        if ((count_ & 31) == 0) {
            if (mod_.active()) {
                // The machine's matrix (Modulation.h): pitch and tone as factors, the level below.
                const float ext[kModSources] = {};
                mod_.evaluate(count_, beatAt(count_), ext, mo_);
                pitchMul_ = std::exp2(static_cast<double>(mod_.offset(mo_, ModDest::Pitch)) / 12.0);
                toneMul_ = std::exp2(mod_.offset(mo_, ModDest::Cutoff));
            }
            // The registration, moved by the animation's slow sine, and the phaser's sweep.
            const double dt = 32.0 * inv;
            animPhase_ += s_.animateHz * dt;
            animPhase_ -= std::floor(animPhase_);
            registrationAt(s_.registration + s_.animate * 3.5f * sin01(animPhase_));
            if (s_.phaser > 0.0f) {
                phPhase_ += 0.3 * dt;
                phPhase_ -= std::floor(phPhase_);
                for (int c = 0; c < 2; ++c) {
                    const double sweep = 0.5 + 0.5 * sin01(phPhase_ + 0.25 * c);
                    const double hz = 200.0 * std::pow(12.5, sweep);                       // 200 Hz .. 2.5 kHz
                    const double t = std::tan(3.14159265358979 * std::min(hz, 0.45 * sr_) * inv);
                    apCoef_[c] = static_cast<float>((t - 1.0) / (t + 1.0));
                }
            }
        }
        ++count_;
        const bool bend = mod_.active() && mod_.targets(ModDest::Pitch);
        double step[12];
        for (int pc = 0; pc < 12; ++pc) {
            step[pc] = bend ? inc[pc] * pitchMul_ : inc[pc];
            counter_[pc] += step[pc];
            if (counter_[pc] >= kModulus) counter_[pc] -= kModulus;
        }
        float sum = 0.0f;
        for (Key& k : keys_) {
            if (!k.on) continue;
            // Crescendo, then (below a sustain level of 1) the decay towards it, and the release.
            if (k.held && !k.decaying && sus < 1.0f && k.env > 0.98f) k.decaying = true;
            k.env += ((k.held ? (k.decaying ? sus : 1.0f) : 0.0f) - k.env) * (k.held ? (k.decaying ? dec : att) : rel);
            if (!k.held && k.env < 1e-4f) { k.on = false; continue; }
            // The phases of the footages: the counter divided, which keeps every octave in lock.
            const double c = counter_[k.pc];
            float v = 0.0f;
            const double invs[3] = { k.inv16, k.inv8, k.inv4 };
            for (int f = 0; f < 3; ++f) {
                const float ws = weight_[f];
                const float wq = f == 0 ? 0.0f : weight_[f + 2];   // squares at 8' and 4'
                if (ws <= 1e-4f && wq <= 1e-4f) continue;
                const float p = frac(c * invs[f]);
                const float dt = static_cast<float>(step[k.pc] * invs[f]);
                const float bl = polyBlep(p, dt);
                if (ws > 1e-4f) v += ws * (2.0f * p - 1.0f - bl);
                if (wq > 1e-4f) v += wq * ((p < 0.5f ? 1.0f : -1.0f) + bl - polyBlep(frac(p + 0.5), dt));
            }
            sum += k.env * k.velocity * v;
        }
        if (mod_.active() && mod_.targets(ModDest::Level)) sum *= std::clamp(1.0f + mo_[static_cast<int>(ModDest::Level)], 0.0f, 2.0f);
        const float dry = tone_.lp(0.12f * regGain_ * sum);
        ring_ = std::max(std::fabs(dry), ring_ * 0.9999f);
        // The ensemble: delay lines swept by a slow and a fast LFO, a third (or a half) of a cycle apart.
        line_[write_] = dry;
        slow_ += kSlowHz[type] * inv; if (slow_ >= 1.0) slow_ -= 1.0;
        fast_ += kFastHz[type] * inv; if (fast_ >= 1.0) fast_ -= 1.0;
        float tap[3] = { 0.0f, 0.0f, 0.0f };
        for (int b = 0; b < taps; ++b) {
            const double ph = static_cast<double>(b) / taps;
            const double ms = kBase[type] + e * (kSlowMs[type] * sin01(slow_ + ph) + kFastMs[type] * sin01(fast_ + ph));
            const double pos = static_cast<double>(write_) - ms * 0.001 * sr_;
            const double fl = std::floor(pos);
            const float t = static_cast<float>(pos - fl);
            const size_t j = static_cast<size_t>(static_cast<int64_t>(fl)) & mask_;
            const float a = line_[j], cc = line_[(j + 1) & mask_];
            tap[b] = bbd_[b].lp(a + t * (cc - a));
        }
        write_ = (write_ + 1) & mask_;
        float l, r;
        if (taps == 2) {
            l = (1.0f - 0.5f * e) * dry + e * tap[0];
            r = (1.0f - 0.5f * e) * dry + e * tap[1];
        } else {
            const float side = kSide[type];
            l = (1.0f - 0.5f * e) * dry + e * (side * tap[0] + (1.0f - side) * tap[1]);
            r = (1.0f - 0.5f * e) * dry + e * (side * tap[2] + (1.0f - side) * tap[1]);
        }
        // The phaser: four first-order all-passes with feedback, mixed with the direct sound for the notches.
        if (s_.phaser > 0.0f) {
            float* out[2] = { &l, &r };
            for (int ch = 0; ch < 2; ++ch) {
                float x = *out[ch] + 0.35f * phFb_[ch];
                const float a = apCoef_[ch];
                for (int st = 0; st < 4; ++st) {
                    const float y = a * x + apX_[ch][st] - a * apY_[ch][st];
                    apX_[ch][st] = x;
                    apY_[ch][st] = y;
                    x = y;
                }
                phFb_[ch] = x;
                *out[ch] += s_.phaser * 0.5f * (x - *out[ch]);
            }
        }
        L[i] = l;
        R[i] = r;
    }
}

} // namespace eph
