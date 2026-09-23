/**
 * @file StringMachine.cpp
 * @brief Divide-down saws and the ensemble.
 */
#include "eph/StringMachine.h"
#include "eph/Oscillator.h"   // polyBlep
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr double kTopOctaveMidi = 108.0;   ///< C8: the top-octave generator's C
constexpr double kModulus = 256.0;         ///< the counter wraps after eight octaves of division
}

void StringMachine::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    size_t n = 4;
    while (static_cast<double>(n) < 0.03 * sr_) n <<= 1;
    line_.assign(n, 0.0f);
    mask_ = n - 1;
    write_ = 0;
    // The twelve top-octave generators start where the seed puts them, as a machine's do at power-on.
    Rng rng;
    rng.seed(seed);
    for (double& c : counter_) c = kModulus * static_cast<double>(rng.uniform());
    for (Key& k : keys_) k = Key{};
    slow_ = fast_ = 0.0;
    tone_.reset();
    for (Svf& b : bbd_) { b.reset(); b.set(7000.0f, 0.0f, static_cast<float>(sr_)); }
    ring_ = 0.0f;
    set(s_);
}

void StringMachine::set(const StringSettings& s)
{
    s_ = s;
    tone_.set(std::min(s.toneHz, static_cast<float>(0.45 * sr_)), 0.1f, static_cast<float>(sr_));
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
    // The envelope is not reset: a stolen key glides from where it was, as the machine's did.
}

void StringMachine::noteOff(int id)
{
    for (Key& k : keys_) if (k.on && k.id == id) k.held = false;
}

void StringMachine::silence()
{
    for (Key& k : keys_) k = Key{};
    std::fill(line_.begin(), line_.end(), 0.0f);
    ring_ = 0.0f;
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
    // Top-octave increments per pitch class (in counter units per sample).
    double inc[12];
    for (int pc = 0; pc < 12; ++pc) inc[pc] = midiToHz(kTopOctaveMidi + pc) * inv;
    for (int i = 0; i < n; ++i) {
        for (int pc = 0; pc < 12; ++pc) {
            counter_[pc] += inc[pc];
            if (counter_[pc] >= kModulus) counter_[pc] -= kModulus;
        }
        float sum = 0.0f;
        for (Key& k : keys_) {
            if (!k.on) continue;
            k.env += ((k.held ? 1.0f : 0.0f) - k.env) * (k.held ? att : rel);
            if (!k.held && k.env < 1e-4f) { k.on = false; continue; }
            const int pc = ((k.pitch % 12) + 12) % 12;
            const int octDown = static_cast<int>(std::lround((kTopOctaveMidi + pc - k.pitch) / 12.0));   // divisions below the top
            const double div8 = std::ldexp(1.0, octDown), div4 = std::ldexp(1.0, std::max(0, octDown - 1));
            // Phase of the 8' and the 4': the counter divided, which keeps every octave in lock.
            const double p8 = std::fmod(counter_[pc] / div8, 1.0), p4 = std::fmod(counter_[pc] / div4, 1.0);
            const float dt8 = static_cast<float>(inc[pc] / div8), dt4 = static_cast<float>(inc[pc] / div4);
            const float s8 = 2.0f * static_cast<float>(p8) - 1.0f - polyBlep(static_cast<float>(p8), dt8);
            const float s4 = 2.0f * static_cast<float>(p4) - 1.0f - polyBlep(static_cast<float>(p4), dt4);
            sum += k.env * k.velocity * (s8 + s_.feet * s4);
        }
        const float dry = tone_.lp(0.12f * sum);
        ring_ = std::max(std::fabs(dry), ring_ * 0.9999f);
        // The ensemble: three delays swept by a slow and a fast LFO, a third of a cycle apart.
        line_[write_] = dry;
        slow_ += 0.6 * inv; if (slow_ >= 1.0) slow_ -= 1.0;
        fast_ += 6.0 * inv; if (fast_ >= 1.0) fast_ -= 1.0;
        float tap[3];
        for (int b = 0; b < 3; ++b) {
            const double ph = static_cast<double>(b) / 3.0;
            const double ms = 6.0 + s_.ensemble * (2.0 * sin01(slow_ + ph) + 0.35 * sin01(fast_ + ph));
            const double d = ms * 0.001 * sr_;
            const double pos = static_cast<double>(write_) - d;
            const double fl = std::floor(pos);
            const float t = static_cast<float>(pos - fl);
            const size_t j = static_cast<size_t>(static_cast<int64_t>(fl)) & mask_;
            const float a = line_[j], c = line_[(j + 1) & mask_];
            tap[b] = bbd_[b].lp(a + t * (c - a));
        }
        write_ = (write_ + 1) & mask_;
        const float e = s_.ensemble;
        L[i] = (1.0f - 0.5f * e) * dry + e * (0.6f * tap[0] + 0.4f * tap[1]);
        R[i] = (1.0f - 0.5f * e) * dry + e * (0.6f * tap[2] + 0.4f * tap[1]);
    }
}

} // namespace eph
