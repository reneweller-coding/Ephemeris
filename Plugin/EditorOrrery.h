/**
 * @file EditorOrrery.h
 * @brief The rack page (PLAN 8.1): the orrery beside the rows' parameters.
 *
 * **The orrery.** An orrery is the clockwork model of the planets; here every sequencer row is a planet
 * on an orbit, the bass row innermost. A full turn is one cycle of the row (its length in steps times
 * its step), so rows of different lengths run at different speeds and meet again only now and then.
 * The top of every orbit is step 1: when running rows stand there together, that is a *conjunction*
 * (Rack.h), drawn as a line of light from the centre; the moment all running rows meet again is counted
 * down under the orbits. The sun in the centre is the transposer: the root the rows play on, which it
 * moves. Everything is computed from the score the engine plays (its rows' shapes and their start and
 * stop events) and the playhead, so it shows what sounds, in the standalone and in a host.
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class ParamPage;

/** @brief The rows as orbits around the root (see the file comment). */
class OrreryView final : public juce::Component, private juce::Timer {
public:
    explicit OrreryView(EphemerisProcessor& p);   ///< shows @p p's score and follows its playhead
    ~OrreryView() override;                       ///< stops the timer
    void paint(juce::Graphics& g) override;       ///< the orbits, the planets, the conjunction, the root

private:
    /** @brief One row at one beat. */
    struct RowState {
        bool known = false;     ///< the score has a shape for it
        bool running = false;   ///< it plays
        bool transposer = false;   ///< it is the transposer
        int length = 16;        ///< steps per cycle
        double divBeats = 0.25; ///< beats per step
        double start = 0.0;     ///< the beat it last started at
        double cycle = 0.0;     ///< position in its cycle, 0..1
    };
    /** @brief Follows the piece: the rows at the playhead. */
    void timerCallback() override;
    /** @brief The rows at @p beat. */
    std::vector<RowState> rowsAt(double beat) const;
    /** @brief The next beat after @p beat at which every running row of @p rows is on its first step, or -1. */
    static double nextConjunction(const std::vector<RowState>& rows, double beat);

    EphemerisProcessor& proc_;   ///< where the score and the playhead come from
    eph::Score score_;           ///< a copy of the score the engine plays
    int version_ = -1;           ///< which one (EphemerisProcessor::scoreVersion)
};

/** @brief The rack tab: the orrery on the left, the rows' parameters on the right. */
class RackPage final : public juce::Component {
public:
    /** @brief Builds both halves; @p params is the rows' parameter page, which the rack page owns. */
    RackPage(EphemerisProcessor& p, std::unique_ptr<ParamPage> params);
    ~RackPage() override;          ///< needs ParamPage complete
    void resized() override;       ///< a square for the orrery, the rest for the parameters

private:
    OrreryView orrery_;                    ///< the orbits
    std::unique_ptr<ParamPage> params_;    ///< the rows' parameters
    juce::Viewport view_;                  ///< ... scrolling where they need more height
};
