/**
 * @file vst3test.cpp
 * @brief Loads the built VST3 the way a DAW does, and asks it to behave.
 *
 * Through the VST3 wrapper: the module is loaded from disk, the factory is asked what is in it, an
 * instance is created, and everything a host does to a freshly loaded plugin is done to it -- describe
 * the parameters, prepare, play under a transport at a tempo of the host's, change that tempo, jump,
 * save and restore the state, change the rate, load a second instance, open the editor, and take it
 * away again. That is the part of pluginval that can live in the repository.
 *
 * The composer runs on a thread of its own and the finished piece is loaded by a timer on the message
 * thread (PluginProcessor.h), so this test runs the message loop while it plays, as a host's would.
 * The path of the plugin comes from the command line, which CMake fills in with the built artefact.
 * @note After Phosphene `Tests/vst3test.cpp` (the checks that apply to a generator without MIDI output).
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

int failures = 0;
int checks = 0;

/** @brief Records one check's result and prints it. */
void check(bool ok, const juce::String& what)
{
    ++checks;
    std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what.toRawUTF8());
    if (!ok) ++failures;
}

/** @brief A transport a host would offer: playing, at a tempo, from a position. */
class TestPlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setTimeInSamples(static_cast<juce::int64>(samples));
        info.setTimeInSeconds(samples / sampleRate);
        info.setIsPlaying(true);
        return info;
    }
    void advance(int n) { samples += n; ppq += n * bpm / (60.0 * sampleRate); }   ///< moves on by @p n samples
    double bpm = 120.0, ppq = 0.0, sampleRate = 48000.0, samples = 0.0;           ///< the transport's state
};

/** @brief Plays @p seconds of blocks, running the message loop between them; returns the peak. */
double play(juce::AudioPluginInstance& p, TestPlayHead& head, double seconds, int block, bool* finite = nullptr)
{
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    double peak = 0.0;
    const int blocks = static_cast<int>(seconds * head.sampleRate / block);
    for (int i = 0; i < blocks; ++i) {
        if (i % 16 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        buf.clear();
        midi.clear();
        p.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < block; ++s) {
                const float v = buf.getReadPointer(c)[s];
                if (finite != nullptr && !std::isfinite(v)) *finite = false;
                peak = juce::jmax(peak, static_cast<double>(std::fabs(v)));
            }
        head.advance(block);
    }
    return peak;
}

/** @brief Prints the tally and returns the exit code. */
int finish()
{
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

/** @brief Loads the built VST3 as a host would and runs the checks. */
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) { std::printf("usage: eph_vst3test <path to Ephemeris.vst3>\n"); return 2; }
    const juce::File plugin(juce::String::fromUTF8(argv[1]));
    std::printf("Ephemeris VST3 test: %s\n", plugin.getFullPathName().toRawUTF8());
    check(plugin.exists(), "the built VST3 is where the build says it is");
    if (!plugin.exists()) return finish();

    // ---------------------------------------------------------------- the module and its factory
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, plugin.getFullPathName());
    check(found.size() == 1, "the module holds exactly one plugin (" + juce::String(found.size()) + ")");
    if (found.isEmpty()) return finish();
    const juce::PluginDescription& desc = *found[0];
    std::printf("  %s %s by %s\n", desc.name.toRawUTF8(), desc.version.toRawUTF8(), desc.manufacturerName.toRawUTF8());
    check(desc.name == "Ephemeris", "it calls itself Ephemeris");
    check(desc.isInstrument, "it says it is an instrument");

    // ---------------------------------------------------------------- an instance
    juce::AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> instance(formats.createPluginInstance(desc, 48000.0, 256, error));
    check(instance != nullptr, "the host can create an instance" + (error.isEmpty() ? juce::String() : ": " + error));
    if (instance == nullptr) return finish();
    const auto& params = instance->getParameters();
    std::printf("  %d parameters, latency %d samples\n", params.size(), instance->getLatencySamples());
    check(params.size() > 150, "the whole parameter set reaches the host (" + juce::String(params.size()) + ")");
    bool named = true;
    for (auto* p : params) named = named && p->getName(128).isNotEmpty();
    check(named, "every parameter has a name");

    // ---------------------------------------------------------------- playing under a transport
    instance->setPlayConfigDetails(0, 2, 48000.0, 256);
    instance->prepareToPlay(48000.0, 256);
    check(instance->getTotalNumInputChannels() == 0 && instance->getTotalNumOutputChannels() == 2,
          "the instance has no inputs and a stereo output");
    TestPlayHead head;
    head.bpm = 97.0;     // not a tempo a style draws: the piece has to take the host's
    head.ppq = 256.0;    // past the atmosphere at the start, where the rows play
    head.samples = head.ppq * 60.0 / head.bpm * 48000.0;
    instance->setPlayHead(&head);
    // The first piece is composed on the composer thread and loaded by the message thread: give it time.
    bool finite = true;
    double peak = 0.0;
    for (int tries = 0; tries < 30 && peak <= 0.05; ++tries) peak = play(*instance, head, 1.0, 256, &finite);
    peak = juce::jmax(peak, play(*instance, head, 8.0, 256, &finite));
    check(finite, "seconds through the wrapper stay finite");
    check(peak > 0.05, "it sounds when the host transport runs (peak " + juce::String(peak, 3) + ")");
    check(peak <= 1.01, "and it never leaves the ceiling (peak " + juce::String(peak, 3) + ")");
    // A new host tempo: the score is loaded again at it, on the message thread, and the piece goes on.
    head.bpm = 131.0;
    const double afterTempo = play(*instance, head, 4.0, 256, &finite);
    check(finite && afterTempo > 0.05, "a new host tempo, and it plays on (peak " + juce::String(afterTempo, 3) + ")");
    // A jump of the host's playhead.
    head.ppq = 512.0;
    head.samples = head.ppq * 60.0 / head.bpm * 48000.0;
    const double afterJump = play(*instance, head, 4.0, 256, &finite);
    check(finite && afterJump > 0.05, "a jump of the playhead, and it plays on (peak " + juce::String(afterJump, 3) + ")");

    // ---------------------------------------------------------------- and in real time
    {
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        double livePeak = 0.0;
        const auto started = std::chrono::steady_clock::now();
        const int blocks = static_cast<int>(2.0 * 48000.0 / 256.0);
        for (int i = 0; i < blocks; ++i) {
            buf.clear();
            midi.clear();
            instance->processBlock(buf, midi);
            livePeak = juce::jmax(livePeak, static_cast<double>(buf.getMagnitude(0, 256)));
            head.advance(256);
            std::this_thread::sleep_until(started + std::chrono::microseconds(static_cast<long long>((i + 1) * 256 * 1.0e6 / 48000.0)));
        }
        check(livePeak > 0.05, "and it sounds in real time as well (peak " + juce::String(livePeak, 3) + ")");
    }
    instance->setPlayHead(nullptr);

    // ---------------------------------------------------------------- state through the wrapper
    {
        juce::Array<juce::AudioProcessorParameter*> ours;
        for (auto* p : params) if (p->getName(64) != "Bypass") ours.add(p);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        // A value a host sets reaches the processor through the parameter queues of the next process call.
        auto settle = [&] {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
            for (int i = 0; i < 4; ++i) { buf.clear(); midi.clear(); instance->processBlock(buf, midi); }
        };
        juce::Random rng(7);
        for (auto* p : ours) p->setValueNotifyingHost(rng.nextFloat());
        settle();
        juce::MemoryBlock state;
        instance->getStateInformation(state);
        check(state.getSize() > 1000, "the state has the whole parameter set in it (" + juce::String(static_cast<int>(state.getSize())) + " bytes)");
        for (auto* p : ours) p->setValueNotifyingHost(0.25f);
        settle();
        juce::MemoryBlock other;
        instance->getStateInformation(other);
        check(other != state, "writing over the parameters really changes the state");
        instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        settle();
        juce::MemoryBlock again;
        instance->getStateInformation(again);
        check(again == state, "the state comes back through the wrapper, byte for byte");
        // Back to the defaults: random levels would leave nothing to hear in the checks below.
        for (auto* p : ours) p->setValueNotifyingHost(p->getDefaultValue());
        settle();
    }

    // ---------------------------------------------------------------- rates, and a second instance
    {
        for (double sr : { 44100.0, 96000.0 }) {
            instance->releaseResources();
            instance->setPlayConfigDetails(0, 2, sr, 128);
            instance->prepareToPlay(sr, 128);
            TestPlayHead h;
            h.sampleRate = sr;
            h.ppq = 300.0;
            instance->setPlayHead(&h);
            bool ok = true;
            double pk = 0.0;
            for (int tries = 0; tries < 10 && pk <= 0.05; ++tries) pk = play(*instance, h, 1.0, 128, &ok);
            instance->setPlayHead(nullptr);
            check(ok && pk > 0.05, "the wrapper plays at " + juce::String(static_cast<int>(sr)) + " Hz, block 128 (peak " + juce::String(pk, 3) + ")");
        }
        juce::String err2;
        std::unique_ptr<juce::AudioPluginInstance> second(formats.createPluginInstance(desc, 48000.0, 256, err2));
        check(second != nullptr, "a second instance loads beside the first" + (err2.isEmpty() ? juce::String() : ": " + err2));
        if (second != nullptr) {
            second->setPlayConfigDetails(0, 2, 48000.0, 256);
            second->prepareToPlay(48000.0, 256);
            TestPlayHead head2;
            head2.ppq = 256.0;
            second->setPlayHead(&head2);
            double peak2 = 0.0;
            for (int tries = 0; tries < 30 && peak2 <= 0.05; ++tries) peak2 = play(*second, head2, 1.0, 256);
            second->setPlayHead(nullptr);
            check(peak2 > 0.05, "and it plays on its own (peak " + juce::String(peak2, 3) + ")");
            second->releaseResources();
            second.reset();
            check(true, "and goes away again without taking the first with it");
        }
    }

    // ---------------------------------------------------------------- the editor, and away again
    {
        check(instance->hasEditor(), "the wrapper offers an editor");
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorAndMakeActive());
        check(editor != nullptr, "the editor opens through the wrapper");
        if (editor != nullptr) {
            check(editor->getWidth() >= 960 && editor->getHeight() >= 640,
                  "and has its size (" + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()) + ")");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(200);   // the meter timer, the arrange view
            juce::Image img(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            juce::Graphics g(img);
            editor->paintEntireComponent(g, true);
            check(img.isValid(), "and paints");
        }
        editor.reset();
    }
    instance->releaseResources();
    instance.reset();
    check(true, "the instance is destroyed without a crash");
    return finish();
}
