/**
 * @file Params.cpp
 * @brief Module descriptor tables and the parameter store.
 * @note The store below the tables is copied from Phosphene `Core/src/Params.cpp` at 9a2f615
 *       (24.09.2026); the tables are Ephemeris's own.
 */
#include "eph/Params.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace eph {

const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const kScaleNames[] = { "Aeolian", "Dorian", "Phrygian", "Harmonic Minor", "Minor Pentatonic" };
const char* const kStyleNames[] = { "Cosmic", "Doom", "Melodic", "Modern", "Drift" };
const char* const kRowDivisionNames[] = { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1 Bar", "2 Bars", "4 Bars" };
const char* const kRowDirectionNames[] = { "Forward", "Backward", "Pendulum", "Random Walk" };
const char* const kRowModeNames[] = { "Notes", "Transposer" };
const char* const kEchoTimeNames[] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
const char* const kTapeSetNames[] = { "Choir", "Strings", "Flute" };

double echoTimeBeats(EchoTime t)
{
    static const double kBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    const int i = static_cast<int>(t);
    return i >= 0 && i < static_cast<int>(EchoTime::Count) ? kBeats[i] : 0.75;
}

double rowDivisionBeats(RowDivision d)
{
    switch (d) {
    case RowDivision::Quarter:      return 1.0;
    case RowDivision::Eighth:       return 0.5;
    case RowDivision::EighthT:      return 1.0 / 3.0;
    case RowDivision::Sixteenth:    return 0.25;
    case RowDivision::SixteenthT:   return 1.0 / 6.0;
    case RowDivision::ThirtySecond: return 0.125;
    case RowDivision::Bar1:         return 4.0;
    case RowDivision::Bars2:        return 8.0;
    case RowDivision::Bars4:        return 16.0;
    default:                        return 0.25;
    }
}

namespace {

const ParamDesc kComposeParams[compose::Count] = {
    { "bpm",           "Tempo",         "BPM",  60.0f, 160.0f, 118.0f, Curve::Linear },
    { "key",           "Key",           "",      0.0f,  11.0f,   9.0f, Curve::Choice, kKeyNames },
    { "scale",         "Scale",         "",      0.0f,   4.0f,   0.0f, Curve::Choice, kScaleNames },
    { "style",         "Style",         "",      0.0f,   4.0f,   0.0f, Curve::Choice, kStyleNames },
    { "piece_minutes", "Piece Length",  "min",   4.0f,  40.0f,  16.0f, Curve::Linear },
};

const ParamDesc kRowParams[row::Count] = {
    { "active",    "Active",     "",      0.0f,  1.0f,  0.0f, Curve::Toggle },
    { "length",    "Length",     "steps", 1.0f, 32.0f, 16.0f, Curve::Int },
    { "division",  "Division",   "",      0.0f,  8.0f,  3.0f, Curve::Choice, kRowDivisionNames },
    { "direction", "Direction",  "",      0.0f,  3.0f,  0.0f, Curve::Choice, kRowDirectionNames },
    { "octave",    "Octave",     "",     -3.0f,  3.0f,  0.0f, Curve::Int },
    { "transpose", "Transpose",  "st",  -12.0f, 12.0f,  0.0f, Curve::Int },
    // Chance per cycle that one step changes (the Turing machine's shift register, PLAN 5.1).
    { "mutation",  "Mutation",   "",      0.0f,  1.0f,  0.0f, Curve::Linear },
    { "gate",      "Gate",       "%",     5.0f, 100.0f, 50.0f, Curve::Linear },
    { "level",     "Level",      "dB",  -60.0f,  6.0f, -6.0f, Curve::Linear },
    { "pan",       "Pan",        "",     -1.0f,  1.0f,  0.0f, Curve::Linear },
    { "echo",      "Echo Send",  "",      0.0f,  1.0f,  0.3f, Curve::Linear },
    { "mode",      "Mode",       "",      0.0f,  1.0f,  0.0f, Curve::Choice, kRowModeNames },
    { "reverb",    "Reverb Send", "",     0.0f,  1.0f,  0.3f, Curve::Linear },
};

/**
 * The modular voice (PLAN 5.2). Ranges after the instruments the style is played on: a ladder from 30 Hz
 * to 16 kHz, a filter envelope of up to six octaves, decays from a tick of 15 ms to the two-second
 * swell a sequence turns into when the hand opens the decay.
 */
const ParamDesc kVoiceParams[voice::Count] = {
    { "wave",       "Wave",         "",       0.0f,     1.0f,    0.0f, Curve::Linear },   // saw .. pulse
    { "detune",     "Detune",       "ct",     0.0f,    30.0f,    7.0f, Curve::Linear },   // second VCO
    { "pw",         "Pulse Width",  "",       0.05f,    0.5f,    0.5f, Curve::Linear },
    { "drift",      "Drift",        "ct",     0.0f,    15.0f,    3.0f, Curve::Linear },   // stationary spread per VCO
    { "drive",      "Drive",        "dB",     0.0f,    24.0f,    6.0f, Curve::Linear },
    { "cutoff",     "Cutoff",       "Hz",    30.0f, 16000.0f,  600.0f, Curve::Log },
    { "resonance",  "Resonance",    "",       0.0f,     1.0f,    0.35f, Curve::Linear },
    { "env_amount", "Env Amount",   "oct",    0.0f,     6.0f,    2.5f, Curve::Linear },
    { "decay",      "Filter Decay", "ms",    15.0f,  2000.0f,  180.0f, Curve::Log },
    { "keytrack",   "Key Track",    "",       0.0f,     1.0f,    0.5f, Curve::Linear },
    { "accent",     "Accent",       "",       0.0f,     1.0f,    0.5f, Curve::Linear },
    { "amp_decay",  "Amp Release",  "ms",     5.0f,  2000.0f,   60.0f, Curve::Log },
    { "glide",      "Glide",        "ms",     1.0f,   500.0f,   60.0f, Curve::Log },
};

/**
 * 24.09.2026: the user heard the first study as dull; the cutoffs and envelope depths of the three
 * voices that play first are raised by about two thirds of an octave.
 */
const char* const kVoiceDefaults =
    "voice1.cutoff=650 voice1.resonance=0.4 voice1.env_amount=3.5 voice1.decay=190\n"
    "voice2.wave=0.6 voice2.pw=0.3 voice2.cutoff=1300 voice2.resonance=0.3 voice2.decay=140 voice2.env_amount=2.5\n"
    "voice3.cutoff=1800 voice3.decay=240 voice3.env_amount=2\n";

/** The lead: a brighter, longer, sung version of the voice, with glide and a vibrato that comes in late. */
const ParamDesc kLeadParams[lead::Count] = {
    { "wave",         "Wave",          "",       0.0f,     1.0f,    0.25f, Curve::Linear },
    { "detune",       "Detune",        "ct",     0.0f,    30.0f,    4.0f, Curve::Linear },
    { "pw",           "Pulse Width",   "",       0.05f,    0.5f,    0.45f, Curve::Linear },
    { "drift",        "Drift",         "ct",     0.0f,    15.0f,    2.0f, Curve::Linear },
    { "drive",        "Drive",         "dB",     0.0f,    24.0f,    8.0f, Curve::Linear },
    { "cutoff",       "Cutoff",        "Hz",    30.0f, 16000.0f, 1500.0f, Curve::Log },
    { "resonance",    "Resonance",     "",       0.0f,     1.0f,    0.25f, Curve::Linear },
    { "env_amount",   "Env Amount",    "oct",    0.0f,     6.0f,    2.0f, Curve::Linear },
    { "decay",        "Filter Decay",  "ms",    15.0f,  2000.0f,  450.0f, Curve::Log },
    { "keytrack",     "Key Track",     "",       0.0f,     1.0f,    0.6f, Curve::Linear },
    { "accent",       "Accent",        "",       0.0f,     1.0f,    0.3f, Curve::Linear },
    { "amp_decay",    "Amp Release",   "ms",     5.0f,  2000.0f,  300.0f, Curve::Log },
    { "glide",        "Glide",         "ms",     1.0f,   500.0f,   80.0f, Curve::Log },
    { "level",        "Level",         "dB",   -60.0f,     6.0f,   -9.0f, Curve::Linear },
    { "pan",          "Pan",           "",      -1.0f,     1.0f,    0.15f, Curve::Linear },
    { "echo",         "Echo Send",     "",       0.0f,     1.0f,    0.45f, Curve::Linear },
    { "vibrato",      "Vibrato",       "ct",     0.0f,   100.0f,   18.0f, Curve::Linear },
    { "vibrato_rate", "Vibrato Rate",  "Hz",     1.0f,    10.0f,    5.2f, Curve::Linear },
    { "reverb",       "Reverb Send",   "",       0.0f,     1.0f,    0.4f, Curve::Linear },
};

/** The tape keyboard: a first setting of the machine (PLAN 5.4 wants it measured on recordings). */
const ParamDesc kTapeParams[tape::Count] = {
    { "set",     "Tapes",        "",      0.0f,     2.0f,    0.0f, Curve::Choice, kTapeSetNames },
    { "vowel",   "Vowel",        "",      0.0f,     1.0f,    0.2f, Curve::Linear },   // aah .. ooh
    { "wow",     "Wow",          "ct",    0.0f,    30.0f,    6.0f, Curve::Linear },
    { "flutter", "Flutter",      "ct",    0.0f,    10.0f,    2.0f, Curve::Linear },
    { "sag",     "Motor Load",   "ct",    0.0f,     5.0f,    1.0f, Curve::Linear },   // per key beyond the first
    { "tone",    "Tone",         "Hz", 1500.0f, 16000.0f, 7000.0f, Curve::Log },
    { "age",     "Age",          "",      0.0f,     1.0f,    0.5f, Curve::Linear },
    { "level",   "Level",        "dB",  -60.0f,     6.0f,   -8.0f, Curve::Linear },
    { "pan",     "Pan",          "",     -1.0f,     1.0f,   -0.1f, Curve::Linear },
    { "echo",    "Echo Send",    "",      0.0f,     1.0f,    0.1f, Curve::Linear },
    { "reverb",  "Reverb Send",  "",      0.0f,     1.0f,    0.5f, Curve::Linear },
};

/** The hall: long and dark, as the style's spaces are (a first setting, to be judged by ear). */
const ParamDesc kReverbParams[reverb::Count] = {
    { "size",     "Size",       "",     0.3f,     3.0f,    1.8f, Curve::Linear },
    { "decay",    "Decay",      "s",    0.3f,    20.0f,    5.5f, Curve::Log },
    { "damping",  "Damping",    "",     0.0f,     1.0f,    0.45f, Curve::Linear },
    { "predelay", "Pre-Delay",  "ms",   0.0f,   200.0f,   25.0f, Curve::Linear },
    { "lowcut",   "Low Cut",    "Hz",  40.0f,   500.0f,  110.0f, Curve::Log },
    { "highcut",  "High Cut",   "Hz", 1000.0f, 20000.0f, 9000.0f, Curve::Log },
    { "return",   "Return",     "dB",  -60.0f,   12.0f,    4.0f, Curve::Linear },   // the FDN returns quietly: +4 dB puts the tail ~10 dB under the dry mix
};

const ParamDesc kEchoParams[echo::Count] = {
    { "time",      "Time",        "",       0.0f,    5.0f,    2.0f, Curve::Choice, kEchoTimeNames },
    { "feedback",  "Feedback",    "",       0.0f,    1.1f,    0.45f, Curve::Linear },   // above 1: runaway, held by the tape
    { "tone",      "Tone",        "Hz",   800.0f, 12000.0f, 3500.0f, Curve::Log },      // loop low pass
    { "wow",       "Wow",         "ms",     0.0f,    4.0f,    0.6f, Curve::Linear },
    { "flutter",   "Flutter",     "ms",     0.0f,    0.5f,    0.06f, Curve::Linear },
    { "drive",     "Tape Drive",  "dB",     0.0f,   18.0f,    4.0f, Curve::Linear },
    { "pingpong",  "Ping-Pong",   "",       0.0f,    1.0f,    1.0f, Curve::Toggle },
    { "return",    "Return",      "dB",   -60.0f,    6.0f,   -4.0f, Curve::Linear },
};

/**
 * The rows start different from each other: a bass row of sixteen sixteenths an octave down, and
 * counter rows of lengths that do not divide sixteen, so that switching one on makes it drift against
 * the first (PLAN 2.2). Only the first is on.
 */
const char* const kDefaultRows =
    "row1.active=1 row1.length=16 row1.division=1/16 row1.octave=-1 row1.pan=0\n"
    "row2.length=13 row2.division=1/16 row2.octave=0 row2.pan=-0.4\n"
    "row3.length=12 row3.division=1/8 row3.octave=1 row3.pan=0.4\n"
    "row4.length=7 row4.division=1/16 row4.octave=1 row4.pan=-0.7\n"
    "row5.length=5 row5.division=1/8 row5.octave=0 row5.pan=0.7\n"
    "row6.length=9 row6.division=1/16 row6.octave=2 row6.pan=-0.2\n"
    "row7.length=24 row7.division=1/8 row7.octave=-1 row7.pan=0.2\n"
    "row8.length=32 row8.division=1/4 row8.octave=0 row8.pan=0\n";

const ParamDesc kMasterParams[master::Count] = {
    { "level", "Level", "dB", -60.0f, 6.0f, 0.0f, Curve::Linear },
};

/** @brief One module: its prefix, table, how many instances exist, and optionally their names. */
struct ModuleSpec {
    const char* prefix;
    const ParamDesc* descs;
    int count;
    int instances;
    const char* const* instanceNames = nullptr;   ///< prefixes of the instances instead of prefix + number
};

const ModuleSpec kModules[static_cast<int>(Module::Count)] = {
    { "compose", kComposeParams, compose::Count, 1 },
    { "row",     kRowParams,     row::Count,     kRows },
    { "master",  kMasterParams,  master::Count,  1 },
    { "voice",   kVoiceParams,   voice::Count,   kRows },
    { "echo",    kEchoParams,    echo::Count,    1 },
    { "lead",    kLeadParams,    lead::Count,    1 },
    { "reverb",  kReverbParams,  reverb::Count,  1 },
    { "tape",    kTapeParams,    tape::Count,    1 },
};

bool isDiscrete(Curve c) { return c == Curve::Int || c == Curve::Choice || c == Curve::Toggle; }

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

} // namespace

ParamStore::ParamStore()
{
    for (auto& row : bases_) for (int& b : row) b = -1;
    int total = 0;
    for (const ModuleSpec& m : kModules) total += m.count * m.instances;
    entries_.reserve(static_cast<size_t>(total));
    for (int mi = 0; mi < static_cast<int>(Module::Count); ++mi) {
        const ModuleSpec& m = kModules[mi];
        for (int inst = 0; inst < m.instances && inst < kMaxInstances; ++inst) {
            bases_[mi][inst] = static_cast<int>(entries_.size());
            std::string prefix = m.instanceNames != nullptr ? std::string(m.instanceNames[inst]) : std::string(m.prefix);
            if (m.instances > 1 && m.instanceNames == nullptr) prefix += std::to_string(inst + 1);
            for (int p = 0; p < m.count; ++p) {
                Entry e{ &m.descs[p], prefix + "." + m.descs[p].key, static_cast<Module>(mi), inst };
                index_.emplace(e.key, static_cast<int>(entries_.size()));
                entries_.push_back(std::move(e));
            }
        }
    }
    values_ = std::make_unique<std::atomic<float>[]>(entries_.size());
    // Defaults: the descriptors', then the instance-specific ones of the rows on top.
    defaults_.resize(entries_.size());
    for (int i = 0; i < count(); ++i) {
        defaults_[static_cast<size_t>(i)] = desc(i).defValue;
        values_[static_cast<size_t>(i)].store(desc(i).defValue, std::memory_order_relaxed);
    }
    parseText(kDefaultRows);
    parseText(kVoiceDefaults);
    for (int i = 0; i < count(); ++i) defaults_[static_cast<size_t>(i)] = get(i);
}

int ParamStore::base(Module m, int instance) const
{
    const int mi = static_cast<int>(m);
    if (mi < 0 || mi >= static_cast<int>(Module::Count) || instance < 0 || instance >= kMaxInstances) return -1;
    return bases_[mi][instance];
}

int ParamStore::find(std::string_view key) const
{
    const auto it = index_.find(std::string(key));
    return it == index_.end() ? -1 : it->second;
}

int ParamStore::getInt(int id) const
{
    return static_cast<int>(std::lround(get(id)));
}

void ParamStore::set(int id, float value)
{
    if (id < 0 || id >= count()) return;
    const ParamDesc& d = desc(id);
    if (!(value == value)) value = defaults_.empty() ? d.defValue : defaults_[static_cast<size_t>(id)];   // NaN
    float v = value < d.minValue ? d.minValue : (value > d.maxValue ? d.maxValue : value);
    if (isDiscrete(d.curve)) v = std::round(v);
    values_[static_cast<size_t>(id)].store(v, std::memory_order_relaxed);
}

float ParamStore::toNormalised(int id, float value) const
{
    const ParamDesc& d = desc(id);
    if (d.maxValue <= d.minValue) return 0.0f;
    float n;
    if (d.curve == Curve::Log) n = std::log(value / d.minValue) / std::log(d.maxValue / d.minValue);
    else n = (value - d.minValue) / (d.maxValue - d.minValue);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

float ParamStore::fromNormalised(int id, float norm) const
{
    const ParamDesc& d = desc(id);
    const float n = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    float v;
    if (d.curve == Curve::Log) v = d.minValue * std::pow(d.maxValue / d.minValue, n);
    else v = d.minValue + n * (d.maxValue - d.minValue);
    if (isDiscrete(d.curve)) v = std::round(v);
    return v;
}

void ParamStore::resetDefaults()
{
    for (int i = 0; i < count(); ++i) values_[static_cast<size_t>(i)].store(defaults_[static_cast<size_t>(i)], std::memory_order_relaxed);
}

int ParamStore::moduleCount(Module m)
{
    const int mi = static_cast<int>(m);
    return mi >= 0 && mi < static_cast<int>(Module::Count) ? kModules[mi].count : 0;
}

void ParamStore::readModule(Module m, int instance, float* out) const
{
    const int b = base(m, instance);
    if (b < 0) return;
    const int n = moduleCount(m);
    for (int i = 0; i < n; ++i) out[i] = get(b + i);
}

void ParamStore::copyValuesFrom(const ParamStore& other)
{
    const int n = count() < other.count() ? count() : other.count();
    for (int i = 0; i < n; ++i) values_[static_cast<size_t>(i)].store(other.get(i), std::memory_order_relaxed);
}

namespace {
/** @brief Lower case without spaces, for matching choice names ("Harmonic Minor" = "harmonicminor"). */
std::string foldName(std::string_view s)
{
    std::string out;
    for (char ch : s) {
        if (ch == ' ' || ch == '_' || ch == '-') continue;
        out += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    return out;
}
} // namespace

bool ParamStore::parseText(std::string_view text, std::string* error)
{
    // Split into assignments. Newlines and ';' always separate; whitespace separates only where the
    // next word contains '=' -- so a choice name with a space ("compose.scale=Harmonic Minor") stays
    // one value while "a=1 b=2" is still two assignments. '#' starts a comment to the end of the line.
    std::vector<std::string> items;
    size_t pos = 0;
    bool newItem = true;
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch == '\n' || ch == ';' || ch == '\r') { newItem = true; ++pos; continue; }
        if (ch == ' ' || ch == '\t') { ++pos; continue; }
        if (ch == '#') { while (pos < text.size() && text[pos] != '\n') ++pos; continue; }
        size_t end = pos;
        while (end < text.size() && text[end] != '\n' && text[end] != ';' && text[end] != '\r' && text[end] != ' ' && text[end] != '\t') ++end;
        const std::string_view word = text.substr(pos, end - pos);
        pos = end;
        if (newItem || word.find('=') != std::string_view::npos || items.empty()) items.emplace_back(word);
        else { items.back() += ' '; items.back() += word; }
        newItem = false;
    }

    bool ok = true;
    for (const std::string& item : items) {
        const std::string_view tok = trim(item);
        const size_t eq = tok.find('=');
        if (eq == std::string_view::npos) {
            if (error && ok) *error = "missing '=' in \"" + std::string(tok) + "\"";
            ok = false;
            continue;
        }
        const std::string_view k = trim(tok.substr(0, eq)), v = trim(tok.substr(eq + 1));
        const int id = find(k);
        if (id < 0) {
            if (error && ok) *error = "unknown parameter \"" + std::string(k) + "\"";
            ok = false;
            continue;
        }
        const ParamDesc& d = desc(id);
        bool matched = false;
        if ((d.curve == Curve::Choice && d.choices != nullptr) || d.curve == Curve::Toggle) {
            const std::string fv = foldName(v);
            if (d.curve == Curve::Toggle && (fv == "on" || fv == "off")) { set(id, fv == "on" ? 1.0f : 0.0f); matched = true; }
            for (int c = 0; !matched && d.choices != nullptr && c <= static_cast<int>(d.maxValue); ++c) {
                if (fv == foldName(d.choices[c])) { set(id, static_cast<float>(c)); matched = true; }
            }
        }
        if (!matched) {
            const std::string vs(v);
            char* stop = nullptr;
            const double x = std::strtod(vs.c_str(), &stop);
            if (vs.empty() || stop == nullptr || *stop != 0) {
                if (error && ok) *error = "bad value \"" + vs + "\" for " + std::string(k);
                ok = false;
                continue;
            }
            set(id, static_cast<float>(x));
        }
    }
    return ok;
}

std::string ParamStore::toText(bool onlyChanged) const
{
    std::string out;
    char buf[64];
    for (int i = 0; i < count(); ++i) {
        const float v = get(i);
        if (onlyChanged && v == defaults_[static_cast<size_t>(i)]) continue;
        // %.9g round-trips every float exactly.
        std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
        out += key(i);
        out += '=';
        out += buf;
        out += '\n';
    }
    return out;
}

std::string ParamStore::format(int id) const
{
    const ParamDesc& d = desc(id);
    const float v = get(id);
    if (d.curve == Curve::Choice && d.choices != nullptr) return d.choices[getInt(id)];
    if (d.curve == Curve::Toggle) return v >= 0.5f ? "On" : "Off";
    char buf[64];
    if (d.curve == Curve::Int) std::snprintf(buf, sizeof(buf), "%d", getInt(id));
    else if (std::fabs(v) >= 100.0f) std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(v));
    else if (std::fabs(v) >= 10.0f) std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    else std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    std::string s = buf;
    if (d.unit != nullptr && d.unit[0] != 0) { s += ' '; s += d.unit; }
    return s;
}

} // namespace eph
