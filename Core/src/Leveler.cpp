/**
 * @file Leveler.cpp
 * @brief Every piece as loud as its style means (Leveler.h).
 */
#include "eph/Leveler.h"
#include "eph/Engine.h"
#include "eph/Loudness.h"
#include <algorithm>
#include <memory>
#include <optional>

namespace eph {

namespace {
constexpr double kRate = 48000.0;   ///< the rate the parts are rendered at to be measured, Hz
constexpr int kBlock = 512;   ///< the block they are rendered in
constexpr double kWarm = 4.0;       ///< seconds before the part: the rooms fill, the notes sounding on are found again
constexpr float kMostDb = 4.0f;     ///< the largest correction either way

/** @brief The loudness of @p seconds from @p beat of the score @p e plays (after kWarm seconds before it); nothing if
 *         @p stop said so on the way. */
std::optional<float> measure(Engine& e, const Score& s, double beat, double seconds, const std::function<bool()>& stop)
{
    const double at = s.tempo.secondsAt(beat);
    e.seek(s.tempo.beatAt(std::max(0.0, at - kWarm)));
    std::vector<float> l(kBlock), r(kBlock);
    const double warm = std::min(kWarm, at);
    for (int done = 0; done < static_cast<int>(warm * kRate); done += kBlock) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), kBlock);
    }
    LoudnessMeter meter;
    meter.prepare(kRate);
    for (int done = 0; done < static_cast<int>(seconds * kRate); done += kBlock) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), kBlock);
        meter.process(l.data(), r.data(), kBlock);
    }
    return static_cast<float>(meter.report().integrated);
}
} // namespace

std::vector<LevelReading> levelScore(Score& score, const ParamStore& params, double seconds, const std::function<bool()>& stop)
{
    std::vector<LevelReading> out;
    if (score.levels.empty()) return out;
    auto e = std::make_unique<Engine>();
    ParamStore& p = e->params();
    p.copyValuesFrom(params);
    // The player's master level stays out of it: the correction is the piece's, the master level comes on top.
    const int master = p.id(Module::Master, 0, master::Level);
    p.set(master, p.defaultValue(master));
    e->prepare(kRate, kBlock);
    // First as composed, then with the corrections found (the master bus compresses and limits after the gain).
    Score s = score;
    for (LevelMark& m : s.levels) m.trimDb = 0.0f;
    e->load(s, true);
    for (const LevelMark& m : s.levels) {
        const std::optional<float> got = measure(*e, s, m.peakBeat, seconds, stop);
        if (!got) return {};
        const float measured = *got;
        const float trim = measured > -70.0f ? std::clamp(m.targetLufs - measured, -kMostDb, kMostDb) : 0.0f;
        out.push_back({ m.beat, measured, m.targetLufs, trim, 0.0f });
    }
    for (size_t i = 0; i < s.levels.size(); ++i) s.levels[i].trimDb = out[i].trim;
    e->params().copyValuesFrom(params);
    p.set(master, p.defaultValue(master));
    e->load(s, true);
    for (size_t i = 0; i < s.levels.size(); ++i) {
        const std::optional<float> got = measure(*e, s, s.levels[i].peakBeat, seconds, stop);
        if (!got) return {};
        const float again = *got;
        if (again > -70.0f) out[i].trim = std::clamp(out[i].trim + (out[i].target - again), -kMostDb, kMostDb);
        out[i].after = again;
    }
    for (size_t i = 0; i < score.levels.size() && i < out.size(); ++i) score.levels[i].trimDb = out[i].trim;
    return out;
}

} // namespace eph
