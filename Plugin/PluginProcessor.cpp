/**
 * @file PluginProcessor.cpp
 * @brief The plugin's processor.
 */
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "eph/compose/Composer.h"
#include "eph/Midi.h"
#include "eph/WavWriter.h"
#include <cmath>
#include <cstdlib>

using namespace eph;

// ---------------------------------------------------------------------------------------------------
// StoreParameter (after Phosphene's Plugin/PluginProcessor.cpp at 9a2f615)

StoreParameter::StoreParameter(ParamStore& store, int id, const juce::String& name)
    : juce::RangedAudioParameter(juce::ParameterID(juce::String(store.key(id)), 1), name,
                                 juce::AudioProcessorParameterWithIDAttributes().withLabel(store.desc(id).unit)),
      store_(store), id_(id), name_(name)
{
    const ParamDesc& d = store.desc(id);
    ParamStore* s = &store;
    range_ = juce::NormalisableRange<float>(
        d.minValue, d.maxValue,
        [s, id](float, float, float n) { return s->fromNormalised(id, n); },
        [s, id](float, float, float v) { return s->toNormalised(id, v); },
        [s, id](float lo, float hi, float v) {
            const ParamDesc& dd = s->desc(id);
            const float c = juce::jlimit(lo, hi, v);
            return (dd.curve == Curve::Linear || dd.curve == Curve::Log) ? c : std::round(c);
        });
}

float StoreParameter::getValue() const { return store_.toNormalised(id_, store_.get(id_)); }
void StoreParameter::setValue(float newValue) { store_.setNormalised(id_, newValue); }
float StoreParameter::getDefaultValue() const { return store_.toNormalised(id_, store_.defaultValue(id_)); }
// A length of 0 or less means no limit, as in JUCE's own parameters (the slider attachments ask with 0).
juce::String StoreParameter::getName(int maximumStringLength) const { return maximumStringLength > 0 ? name_.substring(0, maximumStringLength) : name_; }
juce::String StoreParameter::getLabel() const { return store_.desc(id_).unit; }

int StoreParameter::getNumSteps() const
{
    const ParamDesc& d = store_.desc(id_);
    if (d.curve == Curve::Toggle) return 2;
    if (d.curve == Curve::Choice || d.curve == Curve::Int) return static_cast<int>(std::lround(d.maxValue - d.minValue)) + 1;
    return juce::AudioProcessor::getDefaultNumParameterSteps();
}

bool StoreParameter::isDiscrete() const
{
    const Curve c = store_.desc(id_).curve;
    return c == Curve::Int || c == Curve::Choice || c == Curve::Toggle;
}

bool StoreParameter::isBoolean() const { return store_.desc(id_).curve == Curve::Toggle; }

juce::String StoreParameter::getText(float normalisedValue, int maximumStringLength) const
{
    const ParamDesc& d = store_.desc(id_);
    const float v = store_.fromNormalised(id_, normalisedValue);
    juce::String t;
    if (d.curve == Curve::Choice && d.choices != nullptr) t = d.choices[juce::jlimit(0, static_cast<int>(d.maxValue), static_cast<int>(std::lround(v)))];
    else if (d.curve == Curve::Toggle) t = v >= 0.5f ? "On" : "Off";
    else if (d.curve == Curve::Int) t = juce::String(static_cast<int>(std::lround(v)));
    else t = juce::String(v, std::fabs(v) >= 100.0f ? 0 : (std::fabs(v) >= 10.0f ? 1 : 2));
    return maximumStringLength > 0 ? t.substring(0, maximumStringLength) : t;
}

float StoreParameter::getValueForText(const juce::String& text) const
{
    const ParamDesc& d = store_.desc(id_);
    if (d.curve == Curve::Choice && d.choices != nullptr)
        for (int c = 0; c <= static_cast<int>(d.maxValue); ++c)
            if (text.equalsIgnoreCase(d.choices[c])) return store_.toNormalised(id_, static_cast<float>(c));
    if (d.curve == Curve::Toggle) return text.equalsIgnoreCase("on") ? 1.0f : 0.0f;
    return store_.toNormalised(id_, text.getFloatValue());
}

// ---------------------------------------------------------------------------------------------------
// The processor

EphemerisProcessor::EphemerisProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("Ephemeris composer")
{
    ParamStore& s = store();
    for (int id = 0; id < s.count(); ++id) {
        auto* p = new StoreParameter(s, id, juce::String(s.key(id)).replaceCharacter('.', ' ') + " (" + s.desc(id).name + ")");
        params_.push_back(p);
        addParameter(p);
    }
    seed_ = static_cast<uint64_t>(juce::Time::currentTimeMillis() % 100000);
    if (const char* env = std::getenv("EPH_SEED")) seed_ = std::strtoull(env, nullptr, 10);
    autoPlay_ = std::getenv("EPH_PLAY") != nullptr;
    startTimerHz(10);
    compose();
}

EphemerisProcessor::~EphemerisProcessor()
{
    stopTimer();
    stopThread(10000);
    if (exporter_ && exporter_->joinable()) exporter_->join();
}

double EphemerisProcessor::concertMinutes() const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    return s.get(s.id(Module::Compose, 0, compose::ConcertMinutes));
}

Score EphemerisProcessor::composeNow()
{
    ParamStore snapshot;
    snapshot.copyValuesFrom(store());
    Curation cur;
    uint64_t seed;
    {
        std::lock_guard<std::mutex> g(lock_);
        cur = curation_;
        seed = seed_;
    }
    const double concert = snapshot.get(snapshot.id(Module::Compose, 0, compose::ConcertMinutes));
    const double minutes = snapshot.get(snapshot.id(Module::Compose, 0, compose::PieceMinutes));
    return concert > 0.0 ? composeConcert(snapshot, seed, concert, &cur) : composePiece(snapshot, seed, minutes, 0, &cur);
}

void EphemerisProcessor::compose()
{
    // One at a time: a press while composing is remembered and composed when the first is done.
    if (composing_.exchange(true)) { again_ = true; return; }
    if (isThreadRunning()) waitForThreadToExit(-1);   // the last run is past its end, only not yet gone
    startThread();
}

void EphemerisProcessor::newSeed()
{
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = juce::Random::getSystemRandom().nextInt64() & 0xFFFFFF;
        curation_ = Curation{};
    }
    compose();
}

void EphemerisProcessor::reroll(const juce::String& unit)
{
    {
        std::lock_guard<std::mutex> g(lock_);
        curation_.reroll(unit.toStdString());
    }
    compose();
}

juce::String EphemerisProcessor::curationText() const
{
    std::lock_guard<std::mutex> g(lock_);
    juce::String t;
    for (const auto& r : curation_.rerolls) t << r.first << " x" << r.second << "  ";
    return t.isEmpty() ? juce::String("nothing rerolled") : t.trimEnd();
}

void EphemerisProcessor::run()
{
    auto score = std::make_unique<Score>(composeNow());
    {
        std::lock_guard<std::mutex> g(lock_);
        pending_ = std::move(score);
    }
    composing_ = false;
}

void EphemerisProcessor::timerCallback()
{
    std::unique_ptr<Score> next;
    {
        std::lock_guard<std::mutex> g(lock_);
        next = std::move(pending_);
    }
    if (!next) {
        // In a host the piece plays at the host's tempo. A new tempo means new sample positions for every
        // event, and the engine allocates when it loads: so the score is loaded again here, on the message
        // thread, and the engine goes on from the beat it was at.
        const double bpm = hostBpm_.load();
        if (wrapperType != wrapperType_Standalone && bpm > 0.0 && std::fabs(bpm - playedBpm_.load()) > 1.0e-3) {
            Score s;
            {
                std::lock_guard<std::mutex> g(lock_);
                s = forPlayback(current_);
            }
            suspendProcessing(true);
            const double beat = engine_.beat();
            engine_.prepare(sampleRate_, blockSize_);
            engine_.load(s);
            engine_.seek(beat);
            playedBpm_ = bpm;
            suspendProcessing(false);
        }
        if (again_ && !composing_) { again_ = false; compose(); }
        return;
    }
    // The engine allocates when it loads: never on the audio thread.
    suspendProcessing(true);
    engine_.prepare(sampleRate_, blockSize_);
    engine_.load(forPlayback(*next));
    playedBpm_ = wrapperType != wrapperType_Standalone ? hostBpm_.load() : 0.0;
    {
        std::lock_guard<std::mutex> g(lock_);
        current_ = std::move(*next);
    }
    position_ = 0.0;
    if (autoPlay_) { autoPlay_ = false; playing_ = true; }
    suspendProcessing(false);
}

void EphemerisProcessor::arrangement(std::vector<Marker>& markers, double& lengthBeats, double& seconds) const
{
    std::lock_guard<std::mutex> g(lock_);
    markers = current_.markers;
    lengthBeats = current_.lengthBeats;
    seconds = current_.tempo.secondsAt(current_.lengthBeats);
}

void EphemerisProcessor::takeChannelMeters(float* peak, float* rms)
{
    const int n = meterCount_.exchange(0, std::memory_order_acquire);
    for (int c = 0; c < eph::Engine::kChannels; ++c) {
        const size_t k = static_cast<size_t>(c);
        peak[c] = meterPeak_[k].exchange(0.0f, std::memory_order_relaxed);
        const double s = meterSum_[k].exchange(0.0, std::memory_order_relaxed);
        rms[c] = n > 0 ? static_cast<float>(std::sqrt(s / n)) : 0.0f;
    }
}

void EphemerisProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    blockSize_ = samplesPerBlock;
    engine_.prepare(sampleRate, samplesPerBlock);
    engine_.setMetering(true);   // reading only: the mix is the same to the bit (Engine.h)
    if (const char* secs = std::getenv("EPH_PLAY"); secs != nullptr && std::getenv("EPH_RECORD") != nullptr) {
        recordTarget_ = static_cast<size_t>(std::atof(secs) * sampleRate) * 2;
        record_.assign(recordTarget_, 0.0f);
        recordPos_ = 0;
    }
    std::lock_guard<std::mutex> g(lock_);
    engine_.load(forPlayback(current_));
    playedBpm_ = wrapperType != wrapperType_Standalone ? hostBpm_.load() : 0.0;
}

Score EphemerisProcessor::forPlayback(const Score& s) const
{
    Score out = s;
    const double bpm = hostBpm_.load();
    if (wrapperType != wrapperType_Standalone && bpm > 0.0) out.tempo.setConstant(bpm);
    return out;
}

bool EphemerisProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void EphemerisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int n = buffer.getNumSamples();
    bool play = playing_.load();
    // In a host, the host's playhead is the clock.
    if (wrapperType != wrapperType_Standalone) {
        play = false;
        if (auto* ph = getPlayHead()) {
            if (const auto pos = ph->getPosition()) {
                play = pos->getIsPlaying();
                if (const auto bpm = pos->getBpm(); bpm && *bpm > 0.0) hostBpm_ = *bpm;
                // Until the message thread has loaded the score at a new host tempo (timerCallback), the
                // positions drift apart by design: only a real jump of the host's playhead is followed then.
                const bool tempoPending = std::fabs(hostBpm_.load() - playedBpm_.load()) > 1.0e-3;
                if (play) {
                    if (const auto ppq = pos->getPpqPosition()) {
                        if (std::fabs(*ppq - engine_.beat()) > (tempoPending ? 4.0 : 0.05)) engine_.seek(*ppq);
                    }
                }
            }
        }
    }
    const double seek = seekRequest_.exchange(-1.0);
    if (seek >= 0.0) engine_.seek(seek);
    if (!play || buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    engine_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    position_ = engine_.beat();
    {
        // The channel meters: raised here, taken by the mixer page (takeChannelMeters). One writer, so a load and a
        // store will do; a block the editor takes in between is at worst counted in the next reading.
        float pk[eph::Engine::kChannels];
        double ss[eph::Engine::kChannels];
        const int got = engine_.takeMeters(pk, ss);
        if (got > 0) {
            for (int c = 0; c < eph::Engine::kChannels; ++c) {
                const size_t k = static_cast<size_t>(c);
                if (pk[c] > meterPeak_[k].load(std::memory_order_relaxed)) meterPeak_[k].store(pk[c], std::memory_order_relaxed);
                meterSum_[k].store(meterSum_[k].load(std::memory_order_relaxed) + ss[c], std::memory_order_relaxed);
            }
            meterCount_.fetch_add(got, std::memory_order_release);
        }
    }
    if (recordTarget_ > 0) {
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);
        size_t pos = recordPos_.load();
        for (int i = 0; i < n && pos + 1 < recordTarget_; ++i) { record_[pos++] = l[i]; record_[pos++] = r[i]; }
        recordPos_ = pos + 1 >= recordTarget_ ? recordTarget_ : pos;
    }
    // The standalone stops at the end of the piece, with the rooms rung out.
    if (wrapperType == wrapperType_Standalone && engine_.seconds() > engine_.lengthSeconds() + 8.0) playing_ = false;
}

bool EphemerisProcessor::saveSet(const juce::File& file)
{
    SetFile sf;
    {
        std::lock_guard<std::mutex> g(lock_);
        sf.seed = seed_;
        sf.curation = curation_;
    }
    sf.minutes = store().get(store().id(Module::Compose, 0, compose::PieceMinutes));
    sf.concert = concertMinutes();
    return eph::saveSet(file.getFullPathName().toRawUTF8(), sf, store());
}

bool EphemerisProcessor::loadSet(const juce::File& file)
{
    SetFile sf;
    std::string err;
    if (!eph::loadSet(file.getFullPathName().toRawUTF8(), sf, store(), &err)) return false;
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = sf.seed;
        curation_ = sf.curation;
    }
    if (sf.minutes > 0.0) store().set(store().id(Module::Compose, 0, compose::PieceMinutes), static_cast<float>(sf.minutes));
    store().set(store().id(Module::Compose, 0, compose::ConcertMinutes), static_cast<float>(sf.concert));
    compose();
    return true;
}

void EphemerisProcessor::exportTo(const juce::File& wav)
{
    if (exporting_.exchange(true)) return;
    if (exporter_ && exporter_->joinable()) exporter_->join();
    Score score;
    {
        std::lock_guard<std::mutex> g(lock_);
        score = current_;
    }
    auto params = std::make_shared<ParamStore>();
    params->copyValuesFrom(store());
    const juce::File mid = wav.withFileExtension(".mid");
    exporter_ = std::make_unique<std::thread>([this, score, params, wav, mid]() {
        Engine e;
        e.params().copyValuesFrom(*params);
        e.prepare(48000.0, 512);
        e.load(score);
        WavWriter w;
        const bool ok = w.open(wav.getFullPathName().toRawUTF8(), 48000, 2, WavFormat::Pcm24);
        const int64_t total = static_cast<int64_t>((e.lengthSeconds() + 8.0) * 48000.0);
        std::vector<float> L(512), R(512);
        for (int64_t done = 0; ok && done < total; done += 512) {
            const int n = static_cast<int>(std::min<int64_t>(512, total - done));
            e.process(L.data(), R.data(), n);
            w.write(L.data(), R.data(), n);
        }
        w.close();
        writeMidiFile(score, mid.getFullPathName().toRawUTF8(), "Ephemeris", params.get());
        {
            std::lock_guard<std::mutex> g(lock_);
            lastExport_ = ok ? "exported " + wav.getFileName() + " and " + mid.getFileName() : "could not write " + wav.getFileName();
        }
        exporting_ = false;
    });
}

void EphemerisProcessor::writeRecording()
{
    const char* path = std::getenv("EPH_RECORD");
    if (path == nullptr || record_.empty()) return;
    WavWriter w;
    if (!w.open(path, static_cast<int>(sampleRate_), 2, WavFormat::Float32)) return;
    std::vector<float> L(record_.size() / 2), R(record_.size() / 2);
    for (size_t i = 0; i < L.size(); ++i) { L[i] = record_[2 * i]; R[i] = record_[2 * i + 1]; }
    w.write(L.data(), R.data(), static_cast<int>(L.size()));
    w.close();
    recordTarget_ = 0;
}

juce::String EphemerisProcessor::status() const
{
    if (composing_) return "composing ...";
    if (exporting_) return "exporting ...";
    std::vector<Marker> m;
    double beats = 0.0, secs = 0.0;
    arrangement(m, beats, secs);
    juce::String t;
    std::lock_guard<std::mutex> g(lock_);
    t << "seed " << juce::String(static_cast<juce::int64>(seed_)) << "   " << juce::String(secs / 60.0, 1) << " min";
    if (lastExport_.isNotEmpty()) t << "   " << lastExport_;
    return t;
}

void EphemerisProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("Ephemeris");
    {
        std::lock_guard<std::mutex> g(lock_);
        xml.setAttribute("seed", juce::String(static_cast<juce::int64>(seed_)));
        juce::String rerolls;
        for (const auto& r : curation_.rerolls) rerolls << r.first << "=" << r.second << ";";
        xml.setAttribute("rerolls", rerolls);
    }
    xml.setAttribute("params", juce::String(store().toText(true)));
    copyXmlToBinary(xml, destData);
}

void EphemerisProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName("Ephemeris")) return;
    store().resetDefaults();
    store().parseText(xml->getStringAttribute("params").toStdString());
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = static_cast<uint64_t>(xml->getStringAttribute("seed").getLargeIntValue());
        curation_ = Curation{};
        for (const auto& item : juce::StringArray::fromTokens(xml->getStringAttribute("rerolls"), ";", ""))
            if (item.contains("=")) curation_.rerolls[item.upToFirstOccurrenceOf("=", false, false).toStdString()] = item.fromFirstOccurrenceOf("=", false, false).getIntValue();
    }
    compose();
}

juce::AudioProcessorEditor* EphemerisProcessor::createEditor() { return new EphemerisEditor(*this); }

/** @brief The plugin's factory, called by the JUCE wrappers. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EphemerisProcessor(); }
