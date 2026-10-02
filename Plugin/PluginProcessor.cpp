/**
 * @file PluginProcessor.cpp
 * @brief The plugin's processor.
 */
#include "PluginProcessor.h"
#include "eph/Leveler.h"
#include <map>
#include <tuple>
#include "PluginEditor.h"
#include "eph/Loudness.h"
#include "eph/Presets.h"
#include "eph/compose/Composer.h"
#include "eph/Midi.h"
#include "eph/WavWriter.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

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
    else if (d.curve == Curve::Linear && d.choices != nullptr) t = morphText(d, v);
    else if (d.curve == Curve::Int) t = juce::String(static_cast<int>(std::lround(v)));
    // (juce::String(v, 0) would mean "as precise as it gets", not "no decimals": 159.126 ms)
    else t = std::fabs(v) >= 100.0f ? juce::String(juce::roundToInt(v)) : juce::String(v, std::fabs(v) >= 10.0f ? 1 : 2);
    return maximumStringLength > 0 ? t.substring(0, maximumStringLength) : t;
}

float StoreParameter::getValueForText(const juce::String& text) const
{
    const ParamDesc& d = store_.desc(id_);
    if (d.curve == Curve::Choice && d.choices != nullptr)
        for (int c = 0; c <= static_cast<int>(d.maxValue); ++c)
            if (text.equalsIgnoreCase(d.choices[c])) return store_.toNormalised(id_, static_cast<float>(c));
    if (d.curve == Curve::Linear && d.choices != nullptr)
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
        p->addListener(this);   // undo: the panel's gestures (parameterGestureChanged)
    }
    seed_ = static_cast<uint64_t>(juce::Time::currentTimeMillis() % 100000);
    if (const char* env = std::getenv("EPH_SEED")) seed_ = std::strtoull(env, nullptr, 10);
    autoPlay_ = std::getenv("EPH_PLAY") != nullptr;
    // EPH_MUTE=1 (and the screenshot mode): silent from the first sample, never unmuted from inside. The house
    // rule for every automated run -- tests, screenshots, the manual -- is that nothing makes a sound.
    forceMute_ = std::getenv("EPH_MUTE") != nullptr || std::getenv("EPH_SHOT") != nullptr;
    // EPH_SETS: knobs for an automated run, as eph_render's --set takes them ("compose.concert_minutes=60;...").
    if (const char* sets = std::getenv("EPH_SETS")) s.parseText(sets);
    mute_ = forceMute_;
    // The performer's controllers, the same in every generator (01.10.2026): CC 74 (brightness) the rows' filters, the
    // expression pedal the throw, the sustain pedal holds the moves. The mod wheel (1) is left free.
    for (auto& c : ccMap_) c = -1;
    ccMap_[74] = s.id(Module::Perform, 0, perform::Filter);
    ccMap_[11] = s.id(Module::Perform, 0, perform::Throw);
    ccMap_[64] = s.id(Module::Perform, 0, perform::Hold);
    startTimerHz(10);
    headsetTick_.startTimerHz(30);
    compose();
}

EphemerisProcessor::~EphemerisProcessor()
{
    headsetTick_.stopTimer();
    stopTimer();
    stopThread(10000);
    if (exporter_ && exporter_->joinable()) exporter_->join();
}

double EphemerisProcessor::concertMinutes() const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    return s.get(s.id(Module::Compose, 0, compose::ConcertMinutes));
}

Score EphemerisProcessor::composeNow(ParamStore& snapshot)
{
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
    newer_ = true;   // the last piece's measuring, if it still runs, is broken off
    if (isThreadRunning()) waitForThreadToExit(-1);
    newer_ = false;
    startThread();
}

void EphemerisProcessor::newSeed()
{
    beginStep("New seed");
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = juce::Random::getSystemRandom().nextInt64() & 0xFFFFFF;
        curation_ = Curation{};
    }
    endStep();
    compose();
}

void EphemerisProcessor::reroll(const juce::String& unit)
{
    beginStep("reroll " + juce::String(unit).replace("hands", "moves"));
    {
        std::lock_guard<std::mutex> g(lock_);
        curation_.reroll(unit.toStdString());
    }
    endStep();
    compose();
}

int EphemerisProcessor::chosenKind() const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    if (s.get(s.id(Module::Compose, 0, compose::ConcertMinutes)) <= 0.0f) return 0;
    return s.getBool(s.id(Module::Compose, 0, compose::NightSet)) ? 2 : 1;
}

void EphemerisProcessor::chooseKind(int kind)
{
    ParamStore& s = store();
    const int concert = s.id(Module::Compose, 0, compose::ConcertMinutes);
    const int was = chosenKind();
    static const char* const kKind[3] = { "Piece", "Concert", "Night set" };
    beginStep(kKind[std::clamp(kind, 0, 2)]);
    if (was != 0 && kind == 0) concertMinutes_ = s.get(concert);
    if ((was == 0) != (kind == 0)) setFromUi(concert, kind == 0 ? 0.0f : std::max(20.0f, concertMinutes_));
    if (kind != 0) setFromUi(s.id(Module::Compose, 0, compose::NightSet), kind == 2 ? 1.0f : 0.0f);
    endStep();
    if (was != kind || playingKind() != kind) compose();
}

void EphemerisProcessor::setFromUi(int id, float value)
{
    StoreParameter* p = parameter(id);
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(store().toNormalised(id, value));
    p->endChangeGesture();
}

juce::String EphemerisProcessor::curationText() const
{
    std::lock_guard<std::mutex> g(lock_);
    juce::String t;
    for (const auto& r : curation_.rerolls) t << juce::String(r.first).replace("hands", "moves") << " x" << r.second << "  ";
    return t.isEmpty() ? juce::String("nothing rerolled") : t.trimEnd();
}

void EphemerisProcessor::run()
{
    const bool adopt = adoptNext_.exchange(false);
    ParamStore snapshot;
    Score s = composeNow(snapshot);
    const int kind = snapshot.get(snapshot.id(Module::Compose, 0, compose::ConcertMinutes)) <= 0.0f ? 0
                   : (snapshot.getBool(snapshot.id(Module::Compose, 0, compose::NightSet)) ? 2 : 1);
    uint64_t id = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        pending_ = std::make_unique<Score>(s);
        pendingAdopt_ = adopt;
        pendingKind_ = kind;
        id = pendingId_ = ++composed_;
    }
    composing_ = false;
    // Then its loudness (Leveler.h), while it already plays: 48 seconds rendered, some seconds of work. The correction
    // follows and glides in; a newer piece asked for, or the plugin closing, breaks the measuring off.
    if (levelScore(s, snapshot, 20.0, [this] { return newer_.load() || threadShouldExit(); }).empty()) return;
    std::lock_guard<std::mutex> g(lock_);
    trims_.clear();
    for (const LevelMark& m : s.levels) trims_.push_back(m.trimDb);
    trimsFor_ = id;
}

void EphemerisProcessor::takeTrims()
{
    std::vector<float> trims;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (trimsFor_ == 0 || trimsFor_ > playingId_) return;   // (none, or for a piece not yet loaded)
        const bool mine = trimsFor_ == playingId_;
        trimsFor_ = 0;
        if (!mine) return;
        trims.swap(trims_);
        for (size_t i = 0; i < current_.levels.size() && i < trims.size(); ++i) current_.levels[i].trimDb = trims[i];
        levelled_ = true;
    }
    // A few numbers: the audio thread waits for them instead of losing a block (suspendProcessing would silence one).
    const juce::ScopedLock sl(getCallbackLock());
    engine_.setLevelTrims(trims);
}

void EphemerisProcessor::takeSounds(const Score& next)
{
    // Per synth, its sound and its mix apart: the values the new piece sets at its start against those of the piece
    // playing. What stays (a synth's sound after a reroll of the rows, say) keeps its knobs as the player left them;
    // what is new is taken, as a program change.
    using Key = std::tuple<int, int, int>;
    auto startOf = [](const Score& s) {
        std::map<Key, std::vector<std::pair<int, float>>> m;
        for (const KnobSet& k : s.knobs) if (k.beat <= 1e-9) m[{ k.module, k.instance, k.kind }].push_back({ k.param, k.value });
        return m;
    };
    std::map<Key, std::vector<std::pair<int, float>>> was;
    if (engine_.soundGroup() <= 1e-9) {   // (a concert's later piece on the knobs: every synth takes the new start)
        std::lock_guard<std::mutex> g(lock_);
        was = startOf(current_);
    }
    for (const auto& [synth, values] : startOf(next)) {
        const auto old = was.find(synth);
        if (old != was.end() && old->second == values) continue;
        for (const auto& [id, v] : values) {
            store().set(id, v);
            if (StoreParameter* p = parameter(id)) p->sendValueChangedMessageToListeners(p->getValue());
        }
    }
}

void EphemerisProcessor::tellSounds(const Score& s)
{
    std::vector<bool> told(static_cast<size_t>(store().count()), false);
    for (const KnobSet& k : s.knobs) {
        if (k.param < 0 || k.param >= static_cast<int>(told.size()) || told[static_cast<size_t>(k.param)]) continue;
        told[static_cast<size_t>(k.param)] = true;
        if (StoreParameter* p = parameter(k.param)) p->sendValueChangedMessageToListeners(p->getValue());
    }
}

void EphemerisProcessor::timerCallback()
{
    // The cue sender follows its two parameters (message thread: the socket is opened and closed here).
    {
        const ParamStore& s = store();
        const int port = s.getBool(s.id(Module::Cue, 0, cue::Enabled)) ? s.getInt(s.id(Module::Cue, 0, cue::Port)) : 0;
        if (port != cuePort_) {
            cues_.stop();
            cuePort_ = port;
            if (port > 0) {
                const char* host = std::getenv("EPH_CUE_HOST");
                if (!cues_.start(host != nullptr ? host : "127.0.0.1", port)) cuePort_ = 0;
            }
        }
    }
    std::unique_ptr<Score> next;
    bool adopt = false;
    uint64_t nextId = 0;
    int nextKind = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        next = std::move(pending_);
        adopt = pendingAdopt_;
        nextId = pendingId_;
        nextKind = pendingKind_;
    }
    // A piece asked for again while it was composed is out of date: the next one comes (and inherits its word on the
    // sounds -- a state restored right after the start must not be overwritten by the first piece's).
    if (next && again_ && !composing_) {
        next.reset();
        again_ = false;
        if (adopt) adoptNext_ = true;
        compose();
        return;
    }
    // A concert's next piece brought its sounds (the engine put them on the knobs): the host and the pages are told.
    if (const uint32_t v = engine_.soundsVersion(); v != toldSounds_) {
        toldSounds_ = v;
        Score s;
        {
            std::lock_guard<std::mutex> g(lock_);
            s.knobs = current_.knobs;
        }
        tellSounds(s);
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
            engine_.load(s, false);   // the knobs hold the sounds as they are
            engine_.seek(beat);
            playedBpm_ = bpm;
            suspendProcessing(false);
        }
        if (again_ && !composing_) { again_ = false; compose(); }
        takeTrims();
        return;
    }
    // The engine allocates when it loads: never on the audio thread.
    suspendProcessing(true);
    if (!adopt) takeSounds(*next);
    engine_.prepare(sampleRate_, blockSize_);
    engine_.load(forPlayback(*next), false);
    toldSounds_ = engine_.soundsVersion();
    playedBpm_ = wrapperType != wrapperType_Standalone ? hostBpm_.load() : 0.0;
    {
        std::lock_guard<std::mutex> g(lock_);
        current_ = std::move(*next);
        playingId_ = nextId;
        levelled_ = current_.levels.empty();
    }
    playingKind_ = nextKind;
    ++scoreVersion_;
    position_ = 0.0;
    if (autoPlay_) { autoPlay_ = false; playing_ = true; }
    suspendProcessing(false);
    takeTrims();
}

int EphemerisProcessor::composedPreset(Module m, int instance) const
{
    const double beat = position_.load();
    std::lock_guard<std::mutex> g(lock_);
    int preset = -1;
    for (const SoundPick& k : current_.sounds)   // in beat order: the last one begun is the one that holds
        if (k.module == static_cast<int>(m) && k.instance == instance && k.beat <= beat + 1e-9) preset = k.preset;
    return preset;
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
    engine_.setLive(true);   // the keyboard and the composer switch act here, never in an export (01.10.2026)
    engine_.prepare(sampleRate, samplesPerBlock);
    engine_.setMetering(true);   // reading only: the mix is the same to the bit (Engine.h)
    engine_.setNoteTap(&noteTap_);   // MIDI out (02.10.2026)
    if (const char* secs = std::getenv("EPH_PLAY"); secs != nullptr && std::getenv("EPH_RECORD") != nullptr) {
        recordTarget_ = static_cast<size_t>(std::atof(secs) * sampleRate) * 2;
        record_.assign(recordTarget_, 0.0f);
        recordPos_ = 0;
    }
    std::lock_guard<std::mutex> g(lock_);
    engine_.load(forPlayback(current_), false);   // the knobs hold the sounds as they are
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
    perform(midi);
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
    {   // Stopped, or another keyboard target: every played key is released (01.10.2026).
        const int target = store().getInt(store().id(Module::Perform, 0, perform::KeyboardPart));
        if (!play || target != keyboardSeen_) engine_.liveAllOff();
        keyboardSeen_ = target;
    }
    if (!play || buffer.getNumChannels() < 2) {
        silenceMidi(midi);
        buffer.clear();
        return;
    }
    const double before = engine_.beat();
    const int64_t start = engine_.samplePosition();
    if (start != midiExpect_) silenceMidi(midi);   // a jump: whatever sounded is released
    noteTap_.clear();
    engine_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    emitMidi(midi, start, n);
    midiExpect_ = engine_.samplePosition();
    position_ = engine_.beat();
    if (cues_.running()) {
        // The cues of this block, stamped with the moment it is heard (Cue.h); a jump starts the marks again.
        if (std::fabs(before - lastBeat_) > 1.0e-6) cueTap_.reset();
        const double blockSeconds = static_cast<double>(n) / sampleRate_;
        cueTap_.scan(engine_.cueMarks(), before, engine_.beat(), static_cast<float>(current_.tempo.bpmAt(before)),
                     eph::CueSender::nowNanos(), static_cast<int64_t>(blockSeconds * 1.0e9), static_cast<int64_t>(blockSeconds * 1.0e9), cues_.ring());
    }
    lastBeat_ = engine_.beat();
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
    if (mute_.load(std::memory_order_relaxed)) buffer.clear();   // after the meters and the recording
    // The standalone stops at the end of the piece, with the rooms rung out.
    if (wrapperType == wrapperType_Standalone && engine_.seconds() > engine_.lengthSeconds() + 8.0) playing_ = false;
}

void EphemerisProcessor::silenceMidi(juce::MidiBuffer& midi)
{
    midiExpect_ = -1;
    if (!midiSounding_) return;
    midiSounding_ = false;
    for (int ch = 1; ch <= 16; ++ch) midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
}

void EphemerisProcessor::emitMidi(juce::MidiBuffer& midi, int64_t start, int n)
{
    for (int i = 0; i < noteTap_.count; ++i) {
        const eph::NoteTap::Note& nt = noteTap_.notes[i];
        const int at = static_cast<int>(std::clamp<int64_t>(nt.sample - start, 0, n - 1));
        const int channel = eph::midiChannelOf(static_cast<eph::Part>(nt.part)) + 1;
        if (nt.velocity == 0) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, nt.pitch), at);
        } else {
            midi.addEvent(juce::MidiMessage::noteOn(channel, nt.pitch, static_cast<juce::uint8>(nt.velocity)), at);
            midiSounding_ = true;
        }
    }
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
    beginStep("load " + file.getFileName());
    if (!eph::loadSet(file.getFullPathName().toRawUTF8(), sf, store(), &err)) { endStep(); return false; }
    adoptNext_ = sf.soundsInParams;   // its parameters hold the sounds as they were played
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = sf.seed;
        curation_ = sf.curation;
    }
    if (sf.minutes > 0.0) store().set(store().id(Module::Compose, 0, compose::PieceMinutes), static_cast<float>(sf.minutes));
    store().set(store().id(Module::Compose, 0, compose::ConcertMinutes), static_cast<float>(sf.concert));
    endStep();
    compose();
    return true;
}

void EphemerisProcessor::exportTo(const juce::File& wav, bool stems)
{
    if (exporting_.exchange(true)) return;
    if (exporter_ && exporter_->joinable()) exporter_->join();
    Score score;
    bool levelled = true;
    {
        std::lock_guard<std::mutex> g(lock_);
        score = current_;
        levelled = levelled_;
    }
    auto params = std::make_shared<ParamStore>();
    params->copyValuesFrom(store());
    const bool fresh = engine_.soundGroup() > 1e-9;   // a concert's later piece on the knobs: the export starts with the first's
    const juce::File mid = wav.withFileExtension(".mid");
    exporter_ = std::make_unique<std::thread>([this, score, params, wav, mid, stems, fresh, levelled]() mutable {
        if (!levelled) levelScore(score, *params);   // exported before its measuring was done: measured here
        Engine e;
        e.params().copyValuesFrom(*params);
        e.prepare(48000.0, 512);
        e.load(score, fresh);   // the knobs as they are, the piece's sounds on them -- unless a concert's later piece holds them
        WavWriter w;
        bool ok = w.open(wav.getFullPathName().toRawUTF8(), 48000, 2, WavFormat::Pcm24);
        // The stems: a WAV per channel strip and one for the rooms, in a folder beside the mix.
        constexpr int kStems = Engine::kChannels + 1;
        std::vector<std::vector<float>> stemBuf(stems ? 2 * kStems : 0, std::vector<float>(512));
        std::vector<float*> stemL(kStems), stemR(kStems);
        std::vector<WavWriter> stemWav(stems ? kStems : 0);
        if (stems && ok) {
            const juce::File dir = wav.getParentDirectory().getChildFile(wav.getFileNameWithoutExtension() + "_stems");
            dir.createDirectory();
            for (int c = 0; c < kStems; ++c) {
                stemL[static_cast<size_t>(c)] = stemBuf[static_cast<size_t>(2 * c)].data();
                stemR[static_cast<size_t>(c)] = stemBuf[static_cast<size_t>(2 * c + 1)].data();
                const juce::String name = juce::String(c + 1).paddedLeft('0', 2) + "_"
                                        + juce::String(c < Engine::kChannels ? Engine::channelName(c) : "Rooms").replaceCharacter(' ', '_') + ".wav";
                ok = ok && stemWav[static_cast<size_t>(c)].open(dir.getChildFile(name).getFullPathName().toRawUTF8(), 48000, 2, WavFormat::Pcm24);
            }
            e.setStems(stemL.data(), stemR.data());
        }
        // Twenty seconds of the rooms after the end, faded in over two seconds and out over the last ten (Loudness.h).
        const int64_t total = static_cast<int64_t>((e.lengthSeconds() + 20.0) * 48000.0);
        std::vector<float> L(512), R(512);
        for (int64_t done = 0; ok && done < total; done += 512) {
            const int n = static_cast<int>(std::min<int64_t>(512, total - done));
            e.process(L.data(), R.data(), n);
            for (int i = 0; i < n; ++i) {
                const float g = exportFade(done + i, total, 48000.0);
                L[static_cast<size_t>(i)] *= g;
                R[static_cast<size_t>(i)] *= g;
            }
            w.write(L.data(), R.data(), n);
            for (size_t c = 0; c < stemWav.size(); ++c) stemWav[c].write(stemL[c], stemR[c], n);
        }
        w.close();
        for (WavWriter& sw : stemWav) sw.close();
        writeMidiFile(score, mid.getFullPathName().toRawUTF8(), "Ephemeris", params.get());
        {
            std::lock_guard<std::mutex> g(lock_);
            lastExport_ = ok ? "exported " + wav.getFileName() + " and " + mid.getFileName() + (stems ? " with stems" : "")
                             : "could not write " + wav.getFileName();
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
    // What plays, in words: a single piece, or a concert or a night set of so many (their markers say "Stueck N").
    int pieces = 0;
    for (const Marker& mk : m)
        if (mk.text.rfind("Stueck ", 0) == 0) pieces = std::max(pieces, std::atoi(mk.text.c_str() + 7));
    const int kind = playingKind_.load();
    juce::String t;
    std::lock_guard<std::mutex> g(lock_);
    t << "seed " << juce::String(static_cast<juce::int64>(seed_)) << "   ";
    if (kind == 0) t << "single piece";
    else t << (kind == 2 ? "night set of " : "concert of ") << pieces << (pieces == 1 ? " piece" : " pieces");
    t << ", " << juce::String(secs / 60.0, 1) << " min";
    if (lastExport_.isNotEmpty()) t << "   " << lastExport_;
    return t;
}

int EphemerisProcessor::controllerFor(int id) const
{
    for (int c = 0; c < 128; ++c) if (ccMap_[static_cast<size_t>(c)].load() == id) return c;
    return -1;
}

void EphemerisProcessor::setFromMidi(int id, float value)
{
    StoreParameter* p = parameter(id);
    if (p == nullptr) return;
    const float norm = store().toNormalised(id, value);
    if (std::fabs(p->getValue() - norm) < 1.0e-6f) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(norm);
    p->endChangeGesture();
}

void EphemerisProcessor::applyPreset(Module module, int instance, int index)
{
    const std::vector<SoundPreset>& list = factoryPresets(module);
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    applyPresetValues(module, instance, list[static_cast<size_t>(index)]);
}

namespace {
/** @brief Where a synth's user presets live: <application data>/Ephemeris/Presets/<synth>. */
juce::File presetFolder(Module module)
{
    const char* name = module == Module::Voice ? "Voices" : module == Module::Lead ? "Lead" : module == Module::Drone ? "Drone"
                     : module == Module::Tape ? "Tape Keys" : module == Module::Strings ? "Strings" : module == Module::Drums ? "Drums" : "Atmosphere";
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Ephemeris").getChildFile("Presets").getChildFile(name);
}
} // namespace

std::vector<SoundPreset> EphemerisProcessor::userPresets(Module module) const
{
    std::vector<SoundPreset> out;
    juce::Array<juce::File> files = presetFolder(module).findChildFiles(juce::File::findFiles, false, "*.txt");
    files.sort();
    for (const juce::File& f : files) {
        SoundPreset p;
        if (presetFromText(module, f.getFileNameWithoutExtension().toStdString(), f.loadFileAsString().toStdString(), p)) out.push_back(std::move(p));
    }
    return out;
}

bool EphemerisProcessor::saveUserPreset(Module module, int instance, const juce::String& name)
{
    const juce::String clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty()) return false;
    const juce::File dir = presetFolder(module);
    if (!dir.createDirectory()) return false;
    return dir.getChildFile(clean + ".txt").replaceWithText(juce::String(presetText(store(), module, instance)));
}

void EphemerisProcessor::applyKeyText(const juce::String& text)
{
    beginStep("page preset");
    const juce::ScopedValueSetter<bool> one(restoring_, true);   // the knobs' gestures join the step, not steps of their own
    struct End { EphemerisProcessor& p; ~End() { p.restoring_ = false; p.endStep(); } } end{ *this };
    for (const juce::String& line : juce::StringArray::fromLines(text)) {
        const int eq = line.indexOfChar('=');
        if (eq <= 0) continue;
        const int id = store().find(line.substring(0, eq).trim().toStdString());
        StoreParameter* p = id >= 0 ? parameter(id) : nullptr;
        if (p == nullptr) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost(store().toNormalised(id, line.substring(eq + 1).getFloatValue()));
        p->endChangeGesture();
    }
}

void EphemerisProcessor::applyPresetValues(Module module, int instance, const SoundPreset& preset)
{
    beginStep(juce::String("preset ") + preset.name);
    const juce::ScopedValueSetter<bool> one(restoring_, true);   // the knobs' gestures join the step, not steps of their own
    struct End { EphemerisProcessor& p; ~End() { p.restoring_ = false; p.endStep(); } } end{ *this };
    for (const auto& e : presetKnobs(module, preset)) {
        const int id = store().id(module, instance, e.first);
        StoreParameter* p = parameter(id);
        if (p == nullptr) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost(store().toNormalised(id, e.second));
        p->endChangeGesture();
    }
}

void EphemerisProcessor::perform(const juce::MidiBuffer& midi)
{
    const ParamStore& s = store();
    // A keyboard that plays (perform.keyboard_part, 01.10.2026): its keys go to the engine, on their samples; else a key
    // is the transposition key.
    const bool keys = s.getInt(s.id(Module::Perform, 0, perform::KeyboardPart)) != perform::keys::Off;
    for (const auto meta : midi) {
        const juce::MidiMessage m = meta.getMessage();
        if (m.isAllNotesOff() || m.isAllSoundOff()) {
            engine_.liveAllOff();
        } else if (keys && (m.isNoteOn() || m.isNoteOff())) {
            engine_.queueLive(meta.samplePosition, m.getNoteNumber(), m.getVelocity(), m.getChannel() - 1, m.isNoteOn());
        } else if (m.isNoteOn()) {
            // The transposition key: the distance from middle C, an octave either way at most.
            setFromMidi(s.id(Module::Perform, 0, perform::Transpose), static_cast<float>(std::clamp(m.getNoteNumber() - 60, -12, 12)));
        } else if (m.isController()) {
            const int cc = m.getControllerNumber();
            if (cc < 0 || cc > 127) continue;
            const int learning = learn_.exchange(-1);
            if (learning >= 0) {
                for (auto& c : ccMap_) if (c.load() == learning) c = -1;   // one controller per control
                ccMap_[static_cast<size_t>(cc)] = learning;
            }
            const int id = ccMap_[static_cast<size_t>(cc)].load();
            if (id < 0) continue;
            const ParamDesc& d = s.desc(id);
            const float u = static_cast<float>(m.getControllerValue()) / 127.0f;
            // A bipolar control has its middle on the controller's middle (64), exactly.
            float v = d.minValue + u * (d.maxValue - d.minValue);
            if (d.minValue < 0.0f && d.maxValue > 0.0f && m.getControllerValue() == 64) v = 0.0f;
            setFromMidi(id, v);
        }
    }
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
    xml.setAttribute("sounds", "knobs");   // the parameters hold the composer's sounds (26.09.2026)
    juce::String cc;
    for (int c = 0; c < 128; ++c)
        if (const int id = ccMap_[static_cast<size_t>(c)].load(); id >= 0) cc << c << "=" << juce::String(store().key(id)) << ";";
    xml.setAttribute("controllers", cc);
    xml.setAttribute("concertMinutes", static_cast<double>(concertMinutes_));
    copyXmlToBinary(xml, destData);
}

void EphemerisProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName("Ephemeris")) return;
    store().resetDefaults();
    store().parseText(xml->getStringAttribute("params").toStdString());
    if (xml->hasAttribute("concertMinutes")) concertMinutes_ = static_cast<float>(xml->getDoubleAttribute("concertMinutes", 60.0));
    if (xml->hasAttribute("controllers")) {
        for (auto& c : ccMap_) c = -1;
        for (const auto& item : juce::StringArray::fromTokens(xml->getStringAttribute("controllers"), ";", "")) {
            const int c = item.upToFirstOccurrenceOf("=", false, false).getIntValue();
            const int id = store().find(item.fromFirstOccurrenceOf("=", false, false).toStdString());
            if (c >= 0 && c < 128 && id >= 0) ccMap_[static_cast<size_t>(c)] = id;
        }
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = static_cast<uint64_t>(xml->getStringAttribute("seed").getLargeIntValue());
        curation_ = Curation{};
        for (const auto& item : juce::StringArray::fromTokens(xml->getStringAttribute("rerolls"), ";", ""))
            if (item.contains("=")) curation_.rerolls[item.upToFirstOccurrenceOf("=", false, false).toStdString()] = item.fromFirstOccurrenceOf("=", false, false).getIntValue();
    }
    adoptNext_ = xml->getStringAttribute("sounds") == "knobs";   // the knobs as they were saved, the sounds among them
    history_.clear();   // a host's state is a new beginning
    compose();
}

// ---------------------------------------------------------------------------------------------------------------- undo

std::vector<float> EphemerisProcessor::values() const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    std::vector<float> v(static_cast<size_t>(s.count()));
    for (int id = 0; id < s.count(); ++id) v[static_cast<size_t>(id)] = s.get(id);
    return v;
}

juce::String EphemerisProcessor::extraState() const
{
    std::lock_guard<std::mutex> g(lock_);
    juce::String t;
    t << "seed=" << juce::String(static_cast<juce::int64>(seed_)) << "\nconcert=" << juce::String(concertMinutes_) << "\nrerolls=";
    for (const auto& r : curation_.rerolls) t << r.first << ":" << r.second << ";";
    return t;
}

void EphemerisProcessor::applyExtra(const juce::String& text)
{
    if (text == extraState()) return;
    bool again = false;
    {
        std::lock_guard<std::mutex> g(lock_);
        for (const juce::String& line : juce::StringArray::fromLines(text)) {
            const juce::String k = line.upToFirstOccurrenceOf("=", false, false), v = line.fromFirstOccurrenceOf("=", false, false);
            if (k == "seed") {
                const uint64_t seed = static_cast<uint64_t>(v.getLargeIntValue());
                again = again || seed != seed_;
                seed_ = seed;
            } else if (k == "concert") {
                concertMinutes_ = v.getFloatValue();
            } else if (k == "rerolls") {
                Curation c;
                for (const juce::String& item : juce::StringArray::fromTokens(v, ";", ""))
                    if (item.contains(":")) c.rerolls[item.upToLastOccurrenceOf(":", false, false).toStdString()] = item.fromLastOccurrenceOf(":", false, false).getIntValue();
                again = again || c.rerolls != curation_.rerolls;
                curation_ = c;
            }
        }
    }
    if (again) compose();
}

void EphemerisProcessor::beginStep(const juce::String& what) { history_.begin(what, values(), extraState(), true); }

void EphemerisProcessor::endStep() { history_.end(values(), extraState()); }

void EphemerisProcessor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting)
{
    // Only the panel's gestures, which come on the message thread: a controller's (perform(), the audio thread) and an
    // undo's own are not steps.
    if (restoring_ || !juce::MessageManager::existsAndIsCurrentThread()) return;
    if (gestureIsStarting) {
        const ParamStore& s = store();
        history_.begin(parameterIndex >= 0 && parameterIndex < s.count() ? juce::String(s.desc(parameterIndex).name) : juce::String("knob"),
                       values(), extraState(), false);
        history_.touch(parameterIndex);
    } else {
        history_.end(values(), extraState());
    }
}

void EphemerisProcessor::applyStep(const frame::UndoStep& step, bool after)
{
    const juce::ScopedValueSetter<bool> quiet(restoring_, true);
    for (const auto& [id, before, now] : step.values) setFromUi(id, after ? now : before);
    applyExtra(after ? step.extraAfter : step.extraBefore);
}

bool EphemerisProcessor::undo()
{
    if (const frame::UndoStep* s = history_.undo()) { applyStep(*s, false); return true; }
    return false;
}

bool EphemerisProcessor::redo()
{
    if (const frame::UndoStep* s = history_.redo()) { applyStep(*s, true); return true; }
    return false;
}

void EphemerisProcessor::resetToDefault(int id)
{
    if (id < 0 || id >= store().count()) return;
    beginStep(juce::String(store().desc(id).name) + " to its default");
    setFromUi(id, store().desc(id).defValue);
    endStep();
}

float EphemerisProcessor::playedNormalised(int id) const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    if (id < 0 || id >= s.count()) return std::numeric_limits<float>::quiet_NaN();
    const float v = engine_.playedNow(id), k = s.get(id);
    if (std::fabs(s.toNormalised(id, v) - s.toNormalised(id, k)) < 1.0e-4f) return std::numeric_limits<float>::quiet_NaN();
    return s.toNormalised(id, v);
}

// ------------------------------------------------------------------------------------------------------------- headset

void EphemerisProcessor::pollHeadset()
{
    frame::Settings& st = frame::Settings::of("Ephemeris");
    headset_.listen(st.headset() == frame::Settings::HeadsetMode::Off ? 0 : st.headsetPort());
    const frame::HeadsetEvents e = headset_.poll();
    const ParamStore& s = store();
    {
        // The hands' moves are performing, not editing: no steps of their own.
        const juce::ScopedValueSetter<bool> quiet(restoring_, true);
        if (e.playStop && wrapperType == wrapperType_Standalone) setPlaying(!isPlaying());
        if (e.action) {
            const int id = s.id(Module::Perform, 0, perform::Hold);
            setFromUi(id, s.getBool(id) ? 0.0f : 1.0f);
        }
        if (e.filterMoved) setFromUi(s.id(Module::Perform, 0, perform::Filter), e.filter * 2.0f);   // two octaves either way
        if (e.throwMoved) setFromUi(s.id(Module::Perform, 0, perform::Throw), e.throwAmount);
    }
    if (e.next) newSeed();
}

juce::AudioProcessorEditor* EphemerisProcessor::createEditor() { return new EphemerisEditor(*this); }

/** @brief The plugin's factory, called by the JUCE wrappers. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EphemerisProcessor(); }
