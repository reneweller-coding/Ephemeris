/**
 * @file PluginProcessor.cpp
 * @brief The plugin's processor.
 */
#include "PluginProcessor.h"
#include "eph/Leveler.h"
#include <cstring>
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
                                 juce::AudioProcessorParameterWithIDAttributes().withLabel(store.desc(id).m_unit)),
      m_store(store), m_id(id), m_name(name)
{
    const ParamDesc& d = store.desc(id);
    ParamStore* s = &store;
    m_range = juce::NormalisableRange<float>(
        d.m_minValue, d.m_maxValue,
        [s, id](float, float, float n) { return s->fromNormalised(id, n); },
        [s, id](float, float, float v) { return s->toNormalised(id, v); },
        [s, id](float lo, float hi, float v) {
            const ParamDesc& dd = s->desc(id);
            const float c = juce::jlimit(lo, hi, v);
            return (dd.m_curve == CurveE::LINEAR || dd.m_curve == CurveE::LOG) ? c : std::round(c);
        });
}

float StoreParameter::getValue() const { return m_store.toNormalised(m_id, m_store.get(m_id)); }
void StoreParameter::setValue(float newValue) { m_store.setNormalised(m_id, newValue); }
float StoreParameter::getDefaultValue() const { return m_store.toNormalised(m_id, m_store.defaultValue(m_id)); }
// A length of 0 or less means no limit, as in JUCE's own parameters (the slider attachments ask with 0).
juce::String StoreParameter::getName(int maximumStringLength) const { return maximumStringLength > 0 ? m_name.substring(0, maximumStringLength) : m_name; }
juce::String StoreParameter::getLabel() const { return m_store.desc(m_id).m_unit; }

int StoreParameter::getNumSteps() const
{
    const ParamDesc& d = m_store.desc(m_id);
    if (d.m_curve == CurveE::TOGGLE) return 2;
    if (d.m_curve == CurveE::CHOICE || d.m_curve == CurveE::INT) return static_cast<int>(std::lround(d.m_maxValue - d.m_minValue)) + 1;
    return juce::AudioProcessor::getDefaultNumParameterSteps();
}

bool StoreParameter::isDiscrete() const
{
    const CurveE c = m_store.desc(m_id).m_curve;
    return c == CurveE::INT || c == CurveE::CHOICE || c == CurveE::TOGGLE;
}

bool StoreParameter::isBoolean() const { return m_store.desc(m_id).m_curve == CurveE::TOGGLE; }

juce::String StoreParameter::getText(float normalisedValue, int maximumStringLength) const
{
    const ParamDesc& d = m_store.desc(m_id);
    const float v = m_store.fromNormalised(m_id, normalisedValue);
    juce::String t;
    if (d.m_curve == CurveE::CHOICE && d.m_choices != nullptr) t = d.m_choices[juce::jlimit(0, static_cast<int>(d.m_maxValue), static_cast<int>(std::lround(v)))];
    else if (d.m_curve == CurveE::TOGGLE) t = v >= 0.5f ? "On" : "Off";
    else if (d.m_curve == CurveE::LINEAR && d.m_choices != nullptr) t = morphText(d, v);
    else if (d.m_curve == CurveE::INT) t = juce::String(static_cast<int>(std::lround(v)));
    // (juce::String(v, 0) would mean "as precise as it gets", not "no decimals": 159.126 ms)
    else t = std::fabs(v) >= 100.0f ? juce::String(juce::roundToInt(v)) : juce::String(v, std::fabs(v) >= 10.0f ? 1 : 2);
    return maximumStringLength > 0 ? t.substring(0, maximumStringLength) : t;
}

float StoreParameter::getValueForText(const juce::String& text) const
{
    const ParamDesc& d = m_store.desc(m_id);
    if (d.m_curve == CurveE::CHOICE && d.m_choices != nullptr)
        for (int c = 0; c <= static_cast<int>(d.m_maxValue); ++c)
            if (text.equalsIgnoreCase(d.m_choices[c])) return m_store.toNormalised(m_id, static_cast<float>(c));
    if (d.m_curve == CurveE::LINEAR && d.m_choices != nullptr)
        for (int c = 0; c <= static_cast<int>(d.m_maxValue); ++c)
            if (text.equalsIgnoreCase(d.m_choices[c])) return m_store.toNormalised(m_id, static_cast<float>(c));
    if (d.m_curve == CurveE::TOGGLE) return text.equalsIgnoreCase("on") ? 1.0f : 0.0f;
    return m_store.toNormalised(m_id, text.getFloatValue());
}

// ---------------------------------------------------------------------------------------------------
// The processor

EphemerisProcessor::EphemerisProcessor()
    : juce::AudioProcessor(busLayout()),
      juce::Thread("Ephemeris composer")
{
    ParamStore& s = store();
    for (int id = 0; id < s.count(); ++id) {
        auto* p = new StoreParameter(s, id, juce::String(s.key(id)).replaceCharacter('.', ' ') + " (" + s.desc(id).m_name + ")");
        m_params.push_back(p);
        addParameter(p);
        p->addListener(this);   // undo: the panel's gestures (parameterGestureChanged)
    }
    m_seed = static_cast<uint64_t>(juce::Time::currentTimeMillis() % 100000);
    if (const char* env = std::getenv("EPH_SEED")) m_seed = std::strtoull(env, nullptr, 10);
    m_autoPlay = std::getenv("EPH_PLAY") != nullptr;
    // EPH_MUTE=1 (and the screenshot mode): silent from the first sample, never unmuted from inside. The house
    // rule for every automated run -- tests, screenshots, the manual -- is that nothing makes a sound.
    m_forceMute = std::getenv("EPH_MUTE") != nullptr || std::getenv("EPH_SHOT") != nullptr;
    // EPH_SETS: knobs for an automated run, as eph_render's --set takes them ("compose.concert_minutes=60;...").
    if (const char* sets = std::getenv("EPH_SETS")) s.parseText(sets);
    m_mute = m_forceMute;
    // The performer's controllers, the same in every generator (01.10.2026): CC 74 (brightness) the rows' filters, the
    // expression pedal the throw, the sustain pedal holds the moves. The mod wheel (1) is left free.
    for (auto& c : m_ccMap) c = -1;
    m_ccMap[74] = s.id(ModuleE::PERFORM, 0, perform::FILTER);
    m_ccMap[11] = s.id(ModuleE::PERFORM, 0, perform::THROW);
    m_ccMap[64] = s.id(ModuleE::PERFORM, 0, perform::HOLD);
    startTimerHz(10);
    m_headsetTick.startTimerHz(30);
    compose();
}

EphemerisProcessor::~EphemerisProcessor()
{
    m_headsetTick.stopTimer();
    stopTimer();
    stopThread(10000);
    if (m_exporter && m_exporter->joinable()) m_exporter->join();
}

double EphemerisProcessor::concertMinutes() const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    return s.get(s.id(ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES));
}

Score EphemerisProcessor::composeNow(ParamStore& snapshot)
{
    snapshot.copyValuesFrom(store());
    Curation cur;
    uint64_t seed;
    {
        std::lock_guard<std::mutex> g(m_lock);
        cur = m_curation;
        seed = m_seed;
    }
    const double concert = snapshot.get(snapshot.id(ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES));
    const double minutes = snapshot.get(snapshot.id(ModuleE::COMPOSE, 0, compose::PIECE_MINUTES));
    if (const int k = m_jamComposeKey.load(); k >= 0) {   // following the family jam: the leader's key and mode
        snapshot.set(snapshot.id(ModuleE::COMPOSE, 0, compose::KEY), static_cast<float>(k));
        if (const int sc = m_jamComposeScale.load(); sc >= 0) snapshot.set(snapshot.id(ModuleE::COMPOSE, 0, compose::SCALE), static_cast<float>(sc));
    }
    return concert > 0.0 ? composeConcert(snapshot, seed, concert, &cur) : composePiece(snapshot, seed, minutes, 0, &cur);
}

void EphemerisProcessor::compose()
{
    // One at a time: a press while composing is remembered and composed when the first is done.
    if (m_composing.exchange(true)) { m_again = true; return; }
    m_newer = true;   // the last piece's measuring, if it still runs, is broken off
    if (isThreadRunning()) waitForThreadToExit(-1);
    m_newer = false;
    startThread();
}

void EphemerisProcessor::newSeed()
{
    beginStep("New seed");
    {
        std::lock_guard<std::mutex> g(m_lock);
        m_seed = juce::Random::getSystemRandom().nextInt64() & 0xFFFFFF;
        m_curation = Curation{};
    }
    endStep();
    compose();
}

void EphemerisProcessor::reroll(const juce::String& unit)
{
    beginStep("reroll " + juce::String(unit).replace("hands", "moves"));
    {
        std::lock_guard<std::mutex> g(m_lock);
        m_curation.reroll(unit.toStdString());
    }
    endStep();
    compose();
}

int EphemerisProcessor::chosenKind() const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    if (s.get(s.id(ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES)) <= 0.0f) return 0;
    return s.getBool(s.id(ModuleE::COMPOSE, 0, compose::NIGHT_SET)) ? 2 : 1;
}

void EphemerisProcessor::chooseKind(int kind)
{
    ParamStore& s = store();
    const int concert = s.id(ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES);
    const int was = chosenKind();
    static const char* const Kind[3] = { "Piece", "Concert", "Night set" };
    beginStep(Kind[std::clamp(kind, 0, 2)]);
    if (was != 0 && kind == 0) m_concertMinutes = s.get(concert);
    if ((was == 0) != (kind == 0)) setFromUi(concert, kind == 0 ? 0.0f : std::max(20.0f, m_concertMinutes));
    if (kind != 0) setFromUi(s.id(ModuleE::COMPOSE, 0, compose::NIGHT_SET), kind == 2 ? 1.0f : 0.0f);
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
    std::lock_guard<std::mutex> g(m_lock);
    juce::String t;
    for (const auto& r : m_curation.m_rerolls) t << juce::String(r.first).replace("hands", "moves") << " x" << r.second << "  ";
    return t.isEmpty() ? juce::String("nothing rerolled") : t.trimEnd();
}

void EphemerisProcessor::run()
{
    const bool adopt = m_adoptNext.exchange(false);
    ParamStore snapshot;
    Score s = composeNow(snapshot);
    const int kind = snapshot.get(snapshot.id(ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES)) <= 0.0f ? 0
                   : (snapshot.getBool(snapshot.id(ModuleE::COMPOSE, 0, compose::NIGHT_SET)) ? 2 : 1);
    uint64_t id = 0;
    {
        std::lock_guard<std::mutex> g(m_lock);
        m_pending = std::make_unique<Score>(s);
        m_pendingAdopt = adopt;
        m_pendingKind = kind;
        id = m_pendingId = ++m_composed;
    }
    m_composing = false;
    // Then its loudness (Leveler.h), while it already plays: 48 seconds rendered, some seconds of work. The correction
    // follows and glides in; a newer piece asked for, or the plugin closing, breaks the measuring off.
    if (levelScore(s, snapshot, 20.0, [this] { return m_newer.load() || threadShouldExit(); }).empty()) return;
    std::lock_guard<std::mutex> g(m_lock);
    m_trims.clear();
    for (const LevelMark& m : s.m_levels) m_trims.push_back(m.m_trimDb);
    m_trimsFor = id;
}

void EphemerisProcessor::takeTrims()
{
    std::vector<float> trims;
    {
        std::lock_guard<std::mutex> g(m_lock);
        if (m_trimsFor == 0 || m_trimsFor > m_playingId) return;   // (none, or for a piece not yet loaded)
        const bool mine = m_trimsFor == m_playingId;
        m_trimsFor = 0;
        if (!mine) return;
        trims.swap(m_trims);
        for (size_t i = 0; i < m_current.m_levels.size() && i < trims.size(); ++i) m_current.m_levels[i].m_trimDb = trims[i];
        m_levelled = true;
    }
    // A few numbers: the audio thread waits for them instead of losing a block (suspendProcessing would silence one).
    const juce::ScopedLock sl(getCallbackLock());
    m_engine.setLevelTrims(trims);
}

void EphemerisProcessor::takeSounds(const Score& next)
{
    // Per synth, its sound and its mix apart: the values the new piece sets at its start against those of the piece
    // playing. What stays (a synth's sound after a reroll of the rows, say) keeps its knobs as the player left them;
    // what is new is taken, as a program change.
    using Key = std::tuple<int, int, int>;
    auto startOf = [](const Score& s) {
        std::map<Key, std::vector<std::pair<int, float>>> m;
        for (const KnobSet& k : s.m_knobs) if (k.m_beat <= 1e-9) m[{ k.m_module, k.m_instance, k.m_kind }].push_back({ k.m_param, k.m_value });
        return m;
    };
    std::map<Key, std::vector<std::pair<int, float>>> was;
    if (m_engine.soundGroup() <= 1e-9) {   // (a concert's later piece on the knobs: every synth takes the new start)
        std::lock_guard<std::mutex> g(m_lock);
        was = startOf(m_current);
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
    for (const KnobSet& k : s.m_knobs) {
        if (k.m_param < 0 || k.m_param >= static_cast<int>(told.size()) || told[static_cast<size_t>(k.m_param)]) continue;
        told[static_cast<size_t>(k.m_param)] = true;
        if (StoreParameter* p = parameter(k.m_param)) p->sendValueChangedMessageToListeners(p->getValue());
    }
}

void EphemerisProcessor::timerCallback()
{
    // Ableton Link (02.10.2026): the standalone joins the session while the setting is on (or FAMILY_LINK=1); a DAW's
    // transport rules the plugin. The follow flag falls when the session goes.
    m_link.setEnabled(wrapperType == wrapperType_Standalone && (frame::Settings::of("Ephemeris").link() || frame::LinkClock::forcedOn()));
    m_link.tick();
    if (!m_link.enabled()) m_linkFollowing = false;
    // The family jam (02.10.2026, Jam.h): the role from the settings; a follower composes its next piece in the
    // leader's key and mode.
    m_jam.setRole(frame::Settings::of("Ephemeris").jamRole());
    if (m_jam.role() == frame::Settings::JamRoleE::FOLLOW && m_jam.leaderRoot() >= 0) {
        m_jamComposeKey = m_jam.leaderRoot();
        m_jamComposeScale = scaleOfMode(m_jam.leaderMode());
    } else {
        m_jamComposeKey = -1;
        m_jamComposeScale = -1;
    }
    // FAMILY_JAM_LOG=<file>: what the jam does here, a line whenever it changes (a test aid).
    if (const char* logFile = std::getenv("FAMILY_JAM_LOG")) {
        const juce::String line = m_jam.status() + "; transposed " + juce::String(m_jamTransposeOut.load());
        if (line != m_jamLogged) {
            m_jamLogged = line;
            juce::File(logFile).appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + "  " + line + "\n");
        }
    }
    // The cue sender follows its two parameters (message thread: the socket is opened and closed here).
    {
        const ParamStore& s = store();
        const int port = s.getBool(s.id(ModuleE::CUE, 0, cue::ENABLED)) ? s.getInt(s.id(ModuleE::CUE, 0, cue::PORT)) : 0;
        if (port != m_cuePort) {
            m_cues.stop();
            m_cuePort = port;
            if (port > 0) {
                const char* host = std::getenv("EPH_CUE_HOST");
                if (!m_cues.start(host != nullptr ? host : "127.0.0.1", port)) m_cuePort = 0;
            }
        }
    }
    std::unique_ptr<Score> next;
    bool adopt = false;
    uint64_t nextId = 0;
    int nextKind = 0;
    {
        std::lock_guard<std::mutex> g(m_lock);
        next = std::move(m_pending);
        adopt = m_pendingAdopt;
        nextId = m_pendingId;
        nextKind = m_pendingKind;
    }
    // A piece asked for again while it was composed is out of date: the next one comes (and inherits its word on the
    // sounds -- a state restored right after the start must not be overwritten by the first piece's).
    if (next && m_again && !m_composing) {
        next.reset();
        m_again = false;
        if (adopt) m_adoptNext = true;
        compose();
        return;
    }
    // A concert's next piece brought its sounds (the engine put them on the knobs): the host and the pages are told.
    if (const uint32_t v = m_engine.soundsVersion(); v != m_toldSounds) {
        m_toldSounds = v;
        Score s;
        {
            std::lock_guard<std::mutex> g(m_lock);
            s.m_knobs = m_current.m_knobs;
        }
        tellSounds(s);
    }
    if (!next) {
        // In a host the piece plays at the host's tempo. A new tempo means new sample positions for every
        // event, and the engine allocates when it loads: so the score is loaded again here, on the message
        // thread, and the engine goes on from the beat it was at.
        const double bpm = m_hostBpm.load();
        // A Link session the standalone followed and now leads again (the others left, or Link went off): back to the
        // composed tempo (02.10.2026).
        const bool back = !followsClock() && m_playedBpm.load() > 0.0;
        if (back || (followsClock() && bpm > 0.0 && std::fabs(bpm - m_playedBpm.load()) > 1.0e-3)) {
            Score s;
            {
                std::lock_guard<std::mutex> g(m_lock);
                s = forPlayback(m_current);
            }
            suspendProcessing(true);
            const double beat = m_engine.beat();
            m_engine.prepare(m_sampleRate, m_blockSize);
            m_engine.load(s, false);   // the knobs hold the sounds as they are
            m_engine.seek(beat);
            m_playedBpm = back ? 0.0 : bpm;
            suspendProcessing(false);
        }
        if (m_again && !m_composing) { m_again = false; compose(); }
        takeTrims();
        return;
    }
    // The engine allocates when it loads: never on the audio thread.
    suspendProcessing(true);
    if (!adopt) takeSounds(*next);
    m_engine.prepare(m_sampleRate, m_blockSize);
    m_engine.load(forPlayback(*next), false);
    m_toldSounds = m_engine.soundsVersion();
    m_playedBpm = followsClock() ? m_hostBpm.load() : 0.0;
    {
        std::lock_guard<std::mutex> g(m_lock);
        m_current = std::move(*next);
        m_playingId = nextId;
        m_levelled = m_current.m_levels.empty();
    }
    m_playingKind = nextKind;
    ++m_scoreVersion;
    m_position = 0.0;
    if (m_autoPlay) { m_autoPlay = false; m_playing = true; }
    suspendProcessing(false);
    takeTrims();
}

int EphemerisProcessor::composedPreset(ModuleE m, int instance) const
{
    const double beat = m_position.load();
    std::lock_guard<std::mutex> g(m_lock);
    int preset = -1;
    for (const SoundPick& k : m_current.m_sounds)   // in beat order: the last one begun is the one that holds
        if (k.m_module == static_cast<int>(m) && k.m_instance == instance && k.m_beat <= beat + 1e-9) preset = k.m_preset;
    return preset;
}

void EphemerisProcessor::arrangement(std::vector<Marker>& markers, double& lengthBeats, double& seconds) const
{
    std::lock_guard<std::mutex> g(m_lock);
    markers = m_current.m_markers;
    lengthBeats = m_current.m_lengthBeats;
    seconds = m_current.m_tempo.secondsAt(m_current.m_lengthBeats);
}

void EphemerisProcessor::takeChannelMeters(float* peak, float* rms)
{
    const int n = m_meterCount.exchange(0, std::memory_order_acquire);
    for (int c = 0; c < eph::Engine::Channels; ++c) {
        const size_t k = static_cast<size_t>(c);
        peak[c] = m_meterPeak[k].exchange(0.0f, std::memory_order_relaxed);
        const double s = m_meterSum[k].exchange(0.0, std::memory_order_relaxed);
        rms[c] = n > 0 ? static_cast<float>(std::sqrt(s / n)) : 0.0f;
    }
}

void EphemerisProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_sampleRate = sampleRate;
    m_blockSize = samplesPerBlock;
    m_engine.setLive(true);   // the keyboard and the composer switch act here, never in an export (01.10.2026)
    m_engine.prepare(sampleRate, samplesPerBlock);
    m_engine.setMetering(true);   // reading only: the mix is the same to the bit (Engine.h)
    m_engine.setNoteTap(&m_noteTap);   // MIDI out (02.10.2026)
    // The stems' buffers for their outputs (02.10.2026); the engine writes them only while a stem's bus is on.
    m_stemBuf.assign(static_cast<size_t>(StemOuts * 2 * samplesPerBlock), 0.0f);
    for (int s = 0; s < StemOuts; ++s) {
        m_stemL[static_cast<size_t>(s)] = m_stemBuf.data() + static_cast<size_t>(2 * s * samplesPerBlock);
        m_stemR[static_cast<size_t>(s)] = m_stemBuf.data() + static_cast<size_t>((2 * s + 1) * samplesPerBlock);
    }
    m_engine.setStems(nullptr, nullptr);
    m_stemsOn = false;
    if (const char* secs = std::getenv("EPH_PLAY"); secs != nullptr && std::getenv("EPH_RECORD") != nullptr) {
        m_recordTarget = static_cast<size_t>(std::atof(secs) * sampleRate) * 2;
        m_record.assign(m_recordTarget, 0.0f);
        m_recordPos = 0;
    }
    std::lock_guard<std::mutex> g(m_lock);
    m_engine.load(forPlayback(m_current), false);   // the knobs hold the sounds as they are
    m_playedBpm = followsClock() ? m_hostBpm.load() : 0.0;
}

Score EphemerisProcessor::forPlayback(const Score& s) const
{
    Score out = s;
    const double bpm = m_hostBpm.load();
    if (followsClock() && bpm > 0.0) out.m_tempo.setConstant(bpm);
    return out;
}

juce::AudioProcessor::BusesProperties EphemerisProcessor::busLayout()
{
    BusesProperties b = BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
    for (int s = 0; s < StemOuts; ++s) {
        const juce::String name(s < Engine::Channels ? Engine::channelName(s) : "rooms");
        b = b.withOutput(name.substring(0, 1).toUpperCase() + name.substring(1), juce::AudioChannelSet::stereo(), false);
    }
    return b;
}

bool EphemerisProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int b = 1; b < layouts.outputBuses.size(); ++b) {   // a stem's bus: stereo, or off
        const juce::AudioChannelSet& set = layouts.outputBuses.getReference(b);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void EphemerisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    perform(midi);
    midi.clear();
    const int n = buffer.getNumSamples();
    bool play = m_playing.load();
    // In a host, the host's playhead is the clock.
    if (wrapperType != wrapperType_Standalone) {
        play = false;
        if (auto* ph = getPlayHead()) {
            if (const auto pos = ph->getPosition()) {
                play = pos->getIsPlaying();
                if (const auto bpm = pos->getBpm(); bpm && *bpm > 0.0) m_hostBpm = *bpm;
                // Until the message thread has loaded the score at a new host tempo (timerCallback), the
                // positions drift apart by design: only a real jump of the host's playhead is followed then.
                const bool tempoPending = std::fabs(m_hostBpm.load() - m_playedBpm.load()) > 1.0e-3;
                if (play) {
                    if (const auto ppq = pos->getPpqPosition()) {
                        if (std::fabs(*ppq - m_engine.beat()) > (tempoPending ? 4.0 : 0.05)) m_engine.seek(*ppq);
                    }
                }
            }
        }
    } else if (m_link.enabled()) {
        // Ableton Link (02.10.2026, LinkClock.h): with other apps in the session the standalone follows it as a DAW's
        // playhead -- its tempo (the score reloaded at it on the message thread), its bar phase (the bars line up, the
        // place in the piece stays), its start and stop; alone in it, the standalone leads (the tempo goes out below).
        const double latency = static_cast<double>(getLatencySamples() + n) / m_sampleRate;
        const frame::LinkClock::Now ln = m_link.capture(latency, 4.0);
        m_linkFollowing = ln.m_peers > 0;
        if (ln.m_peers > 0) {
            if (play != m_linkPlayed) {
                m_link.setPlaying(play, latency, 4.0);   // Play or Stop pressed here: to the session
            } else if (ln.m_playing != play) {
                play = ln.m_playing;                      // pressed elsewhere: here too
                m_playing = play;
            }
            m_hostBpm = ln.m_bpm;
            m_linkBeat = ln.m_beat;
            const bool tempoPending = std::fabs(m_hostBpm.load() - m_playedBpm.load()) > 1.0e-3;
            if (play && !tempoPending) {
                double d = std::fmod(ln.m_beat - m_engine.beat(), 4.0);
                if (d > 2.0) d -= 4.0;
                else if (d <= -2.0) d += 4.0;
                if (std::fabs(d) > 0.05) m_engine.seek(m_engine.beat() + d);
            }
        }
        m_linkPlayed = play;
    } else {
        m_linkFollowing = false;
    }
    const double seek = m_seekRequest.exchange(-1.0);
    if (seek >= 0.0) m_engine.seek(seek);
    // The family jam's shared timeline (Jam.h): the host's beat in a DAW, Link's with others in the session; none alone.
    double shared = -1.0;
    if (play && wrapperType != wrapperType_Standalone) shared = m_engine.beat();
    else if (play && m_linkFollowing.load(std::memory_order_relaxed)) shared = m_linkBeat;
    {   // Stopped, or another keyboard target: every played key is released (01.10.2026).
        const int target = store().getInt(store().id(ModuleE::PERFORM, 0, perform::KEYBOARD_PART));
        if (!play || target != m_keyboardSeen) m_engine.liveAllOff();
        m_keyboardSeen = target;
    }
    if (!play || buffer.getNumChannels() < 2) {
        silenceMidi(midi);
        buffer.clear();
        return;
    }
    const double before = m_engine.beat();
    const int64_t start = m_engine.samplePosition();
    if (start != m_midiExpect) silenceMidi(midi);   // a jump: whatever sounded is released
    m_noteTap.clear();
    {   // The stems' outputs (02.10.2026): the engine writes the stems while the host has one of their buses on.
        bool want = false;
        for (int b = 1; b < getBusCount(false) && !want; ++b)
            if (const auto* bus = getBus(false, b)) want = bus->isEnabled();
        want = want && n <= m_blockSize;   // the stems' buffers hold the prepared block
        if (want != m_stemsOn) {
            m_engine.setStems(want ? m_stemL.data() : nullptr, want ? m_stemR.data() : nullptr);
            m_stemsOn = want;
        }
    }
    jamFollow(before, shared);
    m_engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    jamLead(before, m_engine.beat(), shared);
    emitMidi(midi, start, n);
    if (m_stemsOn)
        for (int b = 1; b < getBusCount(false) && b - 1 < StemOuts; ++b) {
            const auto* bus = getBus(false, b);
            if (bus == nullptr || !bus->isEnabled()) continue;
            auto out = getBusBuffer(buffer, false, b);
            if (out.getNumChannels() < 2 || n > m_blockSize) continue;
            out.copyFrom(0, 0, m_stemL[static_cast<size_t>(b - 1)], n);
            out.copyFrom(1, 0, m_stemR[static_cast<size_t>(b - 1)], n);
        }
    if (m_link.enabled() && !m_linkFollowing.load(std::memory_order_relaxed) && n > 0)   // alone in the session: it takes our tempo
        m_link.proposeTempo(60.0 * (m_engine.beat() - before) * m_sampleRate / static_cast<double>(n), 0.0);
    m_midiExpect = m_engine.samplePosition();
    m_position = m_engine.beat();
    if (m_cues.running()) {
        // The cues of this block, stamped with the moment it is heard (Cue.h); a jump starts the marks again.
        if (std::fabs(before - m_lastBeat) > 1.0e-6) m_cueTap.reset();
        const double blockSeconds = static_cast<double>(n) / m_sampleRate;
        m_cueTap.scan(m_engine.cueMarks(), before, m_engine.beat(), static_cast<float>(m_current.m_tempo.bpmAt(before)),
                     eph::CueSender::nowNanos(), static_cast<int64_t>(blockSeconds * 1.0e9), static_cast<int64_t>(blockSeconds * 1.0e9), m_cues.ring());
    }
    m_lastBeat = m_engine.beat();
    {
        // The channel meters: raised here, taken by the mixer page (takeChannelMeters). One writer, so a load and a
        // store will do; a block the editor takes in between is at worst counted in the next reading.
        float pk[eph::Engine::Channels];
        double ss[eph::Engine::Channels];
        const int got = m_engine.takeMeters(pk, ss);
        if (got > 0) {
            for (int c = 0; c < eph::Engine::Channels; ++c) {
                const size_t k = static_cast<size_t>(c);
                if (pk[c] > m_meterPeak[k].load(std::memory_order_relaxed)) m_meterPeak[k].store(pk[c], std::memory_order_relaxed);
                m_meterSum[k].store(m_meterSum[k].load(std::memory_order_relaxed) + ss[c], std::memory_order_relaxed);
            }
            m_meterCount.fetch_add(got, std::memory_order_release);
        }
    }
    if (m_recordTarget > 0) {
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);
        size_t pos = m_recordPos.load();
        for (int i = 0; i < n && pos + 1 < m_recordTarget; ++i) { m_record[pos++] = l[i]; m_record[pos++] = r[i]; }
        m_recordPos = pos + 1 >= m_recordTarget ? m_recordTarget : pos;
    }
    if (m_mute.load(std::memory_order_relaxed)) buffer.clear();   // after the meters and the recording
    // The standalone stops at the end of the piece, with the rooms rung out.
    if (wrapperType == wrapperType_Standalone && m_engine.seconds() > m_engine.lengthSeconds() + 8.0) m_playing = false;
}

void EphemerisProcessor::silenceMidi(juce::MidiBuffer& midi)
{
    m_midiExpect = -1;
    if (!m_midiSounding) return;
    m_midiSounding = false;
    for (int ch = 1; ch <= 16; ++ch) midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
}

void EphemerisProcessor::emitMidi(juce::MidiBuffer& midi, int64_t start, int n)
{
    for (int i = 0; i < m_noteTap.m_count; ++i) {
        const eph::NoteTap::Note& nt = m_noteTap.m_notes[i];
        const int at = static_cast<int>(std::clamp<int64_t>(nt.m_sample - start, 0, n - 1));
        const int channel = eph::midiChannelOf(static_cast<eph::PartE>(nt.m_part)) + 1;
        if (nt.m_velocity == 0) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, nt.m_pitch), at);
        } else {
            midi.addEvent(juce::MidiMessage::noteOn(channel, nt.m_pitch, static_cast<juce::uint8>(nt.m_velocity)), at);
            m_midiSounding = true;
        }
    }
}

int EphemerisProcessor::scaleOfMode(const juce::String& mode)
{
    const juce::String m = mode.trim();
    for (int i = 0; i < 8; ++i)   // compose.scale's eight (ScaleNames, Params.cpp)
        if (m.equalsIgnoreCase(eph::ScaleNames[i])) return i;
    if (m.containsIgnoreCase("penta")) return scaleOfMode("Minor Pentatonic");
    if (m.containsIgnoreCase("harmonic")) return scaleOfMode("Harmonic Minor");
    if (m.containsIgnoreCase("minor")) return scaleOfMode("Aeolian");   // a minor, the just minor too
    if (m.containsIgnoreCase("major") || m.containsIgnoreCase("ionian")) return scaleOfMode("Mixolydian");   // the nearest it has
    return -1;
}

void EphemerisProcessor::jamFollow(double before, double shared)
{
    if (m_jam.role() != frame::Settings::JamRoleE::FOLLOW) {
        if (m_jamFollowing) m_engine.setJam(0, -1.0f, false);
        m_jamFollowing = false;
        m_jamTranspose = 0;
        m_jamTransposeOut = 0;
        m_jamLastBefore = before;
        return;
    }
    const double step = (m_jamLastBefore >= 0.0 && before > m_jamLastBefore && before - m_jamLastBefore < 1.0) ? before - m_jamLastBefore : 0.0;
    m_jamLastBefore = before;
    const frame::JamState js = m_jam.stateAt(shared >= 0.0 ? shared + step : -1.0);
    int want = 0;
    if (js.m_root >= 0) {
        // From the piece's own key: its transposer goes on moving the rows above it, the jam moves the whole.
        const int d = ((js.m_root - m_current.m_keyRoot) % 12 + 12) % 12;
        want = d > 6 ? d - 12 : d;
    }
    const bool barLine = step <= 0.0 || std::floor((before + step) / 4.0) > std::floor(before / 4.0);
    if (want != m_jamTranspose && barLine) m_jamTranspose = want;
    m_jamTransposeOut = m_jamTranspose;
    m_engine.setJam(m_jamTranspose, js.m_root >= 0 ? js.m_energy : -1.0f, js.m_root >= 0 && js.m_rhythmOut);
    m_jamFollowing = true;
}

void EphemerisProcessor::jamLead(double before, double after, double shared)
{
    if (m_jam.role() != frame::Settings::JamRoleE::LEAD) {
        m_jamLeadStarted = false;
        return;
    }
    static const char* const Roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto rootOf = [](const char* name) {
        for (int i = 11; i >= 0; --i) if (std::strcmp(name, Roots[i]) == 0) return i;
        static const char* const Flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        for (int i = 11; i >= 0; --i) if (std::strcmp(name, Flats[i]) == 0) return i;
        return -1;
    };
    const char* mode = eph::ScaleNames[juce::jlimit(0, 7, store().getInt(store().id(ModuleE::COMPOSE, 0, compose::SCALE)))];
    const auto& marks = m_engine.cueMarks();
    const auto barOf = [&](double beat) {
        return shared >= 0.0 ? static_cast<int64_t>(std::llround((shared + beat - before) / 4.0)) : int64_t(-1);
    };
    if (!m_jamLeadStarted || std::fabs(before - m_jamLeadLast) > 1.0e-6) {
        const CueMark* phase = nullptr;
        const CueMark* key = nullptr;
        for (const CueMark& m : marks) {
            if (m.m_beat > before + 1.0) break;
            if (m.m_kind == CueKindE::PHASE) phase = &m;
            else if (m.m_kind == CueKindE::KEY) key = &m;
        }
        const int root = key != nullptr ? rootOf(key->m_text) : m_current.m_keyRoot;
        m_jam.postKey(root >= 0 ? root : m_current.m_keyRoot, mode);
        if (phase != nullptr) {
            float e = 0.5f;
            bool drop = false;
            const frame::JamSectionE s = frame::jamSectionOf(phase->m_text, e, drop);
            m_jam.postSection(s, e, false, -1);
        }
        m_jamLeadStarted = true;
    } else {
        for (const CueMark& m : marks) {   // one beat ahead: a follower acts on the very bar line meant
            if (m.m_beat < before + 1.0) continue;
            if (m.m_beat >= after + 1.0) break;
            if (m.m_kind == CueKindE::PHASE) {
                float e = 0.5f;
                bool drop = false;
                const frame::JamSectionE s = frame::jamSectionOf(m.m_text, e, drop);
                m_jam.postSection(s, e, drop, barOf(m.m_beat));
            } else if (m.m_kind == CueKindE::KEY) {
                const int r = rootOf(m.m_text);
                if (r >= 0) m_jam.postKey(r, mode);
            }
        }
    }
    m_jamLeadLast = after;
}

juce::String EphemerisProcessor::linkStatus() const
{
    if (!m_link.enabled()) return "off";
    const int n = m_link.peers();
    return n == 0 ? "alone in the session: it takes this tempo" : juce::String(n) + (n == 1 ? " other app" : " other apps");
}

bool EphemerisProcessor::saveSet(const juce::File& file)
{
    SetFile sf;
    {
        std::lock_guard<std::mutex> g(m_lock);
        sf.m_seed = m_seed;
        sf.m_curation = m_curation;
    }
    sf.m_minutes = store().get(store().id(ModuleE::COMPOSE, 0, compose::PIECE_MINUTES));
    sf.m_concert = concertMinutes();
    return eph::saveSet(file.getFullPathName().toRawUTF8(), sf, store());
}

bool EphemerisProcessor::loadSet(const juce::File& file)
{
    SetFile sf;
    std::string err;
    beginStep("load " + file.getFileName());
    if (!eph::loadSet(file.getFullPathName().toRawUTF8(), sf, store(), &err)) { endStep(); return false; }
    m_adoptNext = sf.m_soundsInParams;   // its parameters hold the sounds as they were played
    {
        std::lock_guard<std::mutex> g(m_lock);
        m_seed = sf.m_seed;
        m_curation = sf.m_curation;
    }
    if (sf.m_minutes > 0.0) store().set(store().id(ModuleE::COMPOSE, 0, compose::PIECE_MINUTES), static_cast<float>(sf.m_minutes));
    store().set(store().id(ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES), static_cast<float>(sf.m_concert));
    endStep();
    compose();
    return true;
}

void EphemerisProcessor::exportTo(const juce::File& wav, bool stems)
{
    if (m_exporting.exchange(true)) return;
    if (m_exporter && m_exporter->joinable()) m_exporter->join();
    Score score;
    bool levelled = true;
    {
        std::lock_guard<std::mutex> g(m_lock);
        score = m_current;
        levelled = m_levelled;
    }
    auto params = std::make_shared<ParamStore>();
    params->copyValuesFrom(store());
    const bool fresh = m_engine.soundGroup() > 1e-9;   // a concert's later piece on the knobs: the export starts with the first's
    const juce::File mid = wav.withFileExtension(".mid");
    m_exporter = std::make_unique<std::thread>([this, score, params, wav, mid, stems, fresh, levelled]() mutable {
        if (!levelled) levelScore(score, *params);   // exported before its measuring was done: measured here
        Engine e;
        e.params().copyValuesFrom(*params);
        e.prepare(48000.0, 512);
        e.load(score, fresh);   // the knobs as they are, the piece's sounds on them -- unless a concert's later piece holds them
        WavWriter w;
        bool ok = w.open(wav.getFullPathName().toRawUTF8(), 48000, 2, WavFormatE::PCM24);
        // The stems: a WAV per channel strip and one for the rooms, in a folder beside the mix.
        constexpr int Stems = Engine::Channels + 1;
        std::vector<std::vector<float>> stemBuf(stems ? 2 * Stems : 0, std::vector<float>(512));
        std::vector<float*> stemL(Stems), stemR(Stems);
        std::vector<WavWriter> stemWav(stems ? Stems : 0);
        if (stems && ok) {
            const juce::File dir = wav.getParentDirectory().getChildFile(wav.getFileNameWithoutExtension() + "_stems");
            dir.createDirectory();
            for (int c = 0; c < Stems; ++c) {
                stemL[static_cast<size_t>(c)] = stemBuf[static_cast<size_t>(2 * c)].data();
                stemR[static_cast<size_t>(c)] = stemBuf[static_cast<size_t>(2 * c + 1)].data();
                const juce::String name = juce::String(c + 1).paddedLeft('0', 2) + "_"
                                        + juce::String(c < Engine::Channels ? Engine::channelName(c) : "Rooms").replaceCharacter(' ', '_') + ".wav";
                ok = ok && stemWav[static_cast<size_t>(c)].open(dir.getChildFile(name).getFullPathName().toRawUTF8(), 48000, 2, WavFormatE::PCM24);
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
            std::lock_guard<std::mutex> g(m_lock);
            m_lastExport = ok ? "exported " + wav.getFileName() + " and " + mid.getFileName() + (stems ? " with stems" : "")
                             : "could not write " + wav.getFileName();
        }
        m_exporting = false;
    });
}

void EphemerisProcessor::writeRecording()
{
    const char* path = std::getenv("EPH_RECORD");
    if (path == nullptr || m_record.empty()) return;
    WavWriter w;
    if (!w.open(path, static_cast<int>(m_sampleRate), 2, WavFormatE::FLOAT32)) return;
    std::vector<float> L(m_record.size() / 2), R(m_record.size() / 2);
    for (size_t i = 0; i < L.size(); ++i) { L[i] = m_record[2 * i]; R[i] = m_record[2 * i + 1]; }
    w.write(L.data(), R.data(), static_cast<int>(L.size()));
    w.close();
    m_recordTarget = 0;
}

juce::String EphemerisProcessor::status() const
{
    if (m_composing) return "composing ...";
    if (m_exporting) return "exporting ...";
    std::vector<Marker> m;
    double beats = 0.0, secs = 0.0;
    arrangement(m, beats, secs);
    // What plays, in words: a single piece, or a concert or a night set of so many (their markers say "Stueck N").
    int pieces = 0;
    for (const Marker& mk : m)
        if (mk.m_text.rfind("Stueck ", 0) == 0) pieces = std::max(pieces, std::atoi(mk.m_text.c_str() + 7));
    const int kind = m_playingKind.load();
    juce::String t;
    std::lock_guard<std::mutex> g(m_lock);
    t << "seed " << juce::String(static_cast<juce::int64>(m_seed)) << "   ";
    if (kind == 0) t << "single piece";
    else t << (kind == 2 ? "night set of " : "concert of ") << pieces << (pieces == 1 ? " piece" : " pieces");
    t << ", " << juce::String(secs / 60.0, 1) << " min";
    if (m_lastExport.isNotEmpty()) t << "   " << m_lastExport;
    return t;
}

int EphemerisProcessor::controllerFor(int id) const
{
    for (int c = 0; c < 128; ++c) if (m_ccMap[static_cast<size_t>(c)].load() == id) return c;
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

void EphemerisProcessor::applyKnobs(const juce::String& what, const std::vector<std::pair<int, float>>& values)
{
    endPreview();
    beginStep(what);
    {
        const juce::ScopedValueSetter<bool> one(m_restoring, true);   // the knobs' gestures join the step
        for (const auto& [id, v] : values) {
            StoreParameter* p = parameter(id);
            if (p == nullptr) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost(store().toNormalised(id, v));
            p->endChangeGesture();
        }
    }
    endStep();
}

void EphemerisProcessor::previewKnobs(const std::vector<std::pair<int, float>>& values)
{
    endPreview();
    for (const auto& [id, v] : values) {
        if (id < 0 || id >= store().count()) continue;
        m_previewSaved.emplace_back(id, store().get(id));
        store().set(id, v);
    }
}

void EphemerisProcessor::endPreview()
{
    for (const auto& [id, v] : m_previewSaved) store().set(id, v);
    m_previewSaved.clear();
}

void EphemerisProcessor::applyPreset(ModuleE module, int instance, int index)
{
    const std::vector<SoundPreset>& list = factoryPresets(module);
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    applyPresetValues(module, instance, list[static_cast<size_t>(index)]);
}

namespace {
/** @brief Where a synth's user presets live: <application data>/Ephemeris/Presets/<synth>. */
juce::File presetFolder(ModuleE module)
{
    const char* name = module == ModuleE::VOICE ? "Voices" : module == ModuleE::LEAD ? "Lead" : module == ModuleE::DRONE ? "Drone"
                     : module == ModuleE::TAPE ? "Tape Keys" : module == ModuleE::STRINGS ? "Strings" : module == ModuleE::DRUMS ? "Drums" : "Atmosphere";
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Ephemeris").getChildFile("Presets").getChildFile(name);
}
} // namespace

std::vector<SoundPreset> EphemerisProcessor::userPresets(ModuleE module) const
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

bool EphemerisProcessor::saveUserPreset(ModuleE module, int instance, const juce::String& name)
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
    const juce::ScopedValueSetter<bool> one(m_restoring, true);   // the knobs' gestures join the step, not steps of their own
    struct End { EphemerisProcessor& m_p; ~End() { m_p.m_restoring = false; m_p.endStep(); } } end{ *this };
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

void EphemerisProcessor::applyPresetValues(ModuleE module, int instance, const SoundPreset& preset)
{
    beginStep(juce::String("preset ") + preset.m_name);
    const juce::ScopedValueSetter<bool> one(m_restoring, true);   // the knobs' gestures join the step, not steps of their own
    struct End { EphemerisProcessor& m_p; ~End() { m_p.m_restoring = false; m_p.endStep(); } } end{ *this };
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
    const bool keys = s.getInt(s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_PART)) != perform::keys::OFF;
    for (const auto meta : midi) {
        const juce::MidiMessage m = meta.getMessage();
        if (m.isAllNotesOff() || m.isAllSoundOff()) {
            m_engine.liveAllOff();
            m_keyMemory.clear();
        } else if (keys && (m.isNoteOn() || m.isNoteOff())) {
            // The keyboard's options (02.10.2026): the keys under the split play the lower voice, Scale Lock moves a key
            // to the nearest note of the piece's key and scale as it sounds (the transposer's root and the Transpose knob
            // with it; not on the drums, whose keys are their instruments), the velocity goes through its curve. Where a
            // press went, its release goes too.
            const int ch = m.getChannel() - 1, key = m.getNoteNumber();
            if (m.isNoteOn()) {
                const int lower = s.getInt(s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_LOWER));
                const int split = 36 + 12 * s.getInt(s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_SPLIT));
                const int target = lower != perform::keys::OFF && key < split ? lower : -1;
                int pitch = key;
                if (s.getBool(s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_SCALE))
                    && (target >= 0 ? target : m_engine.keyTargetFor(ch)) != perform::keys::DRUMS) {
                    const Score& sc = m_engine.score();
                    const double beat = m_engine.beat();
                    const int root = sc.m_keyRoot + sc.rootAt(beat) + s.getInt(s.id(ModuleE::PERFORM, 0, perform::TRANSPOSE));
                    const int scale = std::clamp(sc.scaleAt(beat, s.getInt(s.id(ModuleE::COMPOSE, 0, compose::SCALE))), 0, 7);
                    pitch = frame::snapToScale(key, ((root % 12) + 12) % 12, frame::scaleMask(ScaleNames[scale]));
                }
                const int velocity = frame::shapeVelocity(m.getVelocity(), s.getInt(s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_VELOCITY)));
                m_keyMemory.press(ch, key, target, pitch);
                m_engine.queueLive(meta.samplePosition, pitch, velocity, ch, true, target);
            } else {
                int target = -1, pitch = key;
                m_keyMemory.release(ch, key, target, pitch);
                m_engine.queueLive(meta.samplePosition, pitch, 0, ch, false, target);
            }
        } else if (m.isNoteOn()) {
            // The transposition key: the distance from middle C, an octave either way at most.
            setFromMidi(s.id(ModuleE::PERFORM, 0, perform::TRANSPOSE), static_cast<float>(std::clamp(m.getNoteNumber() - 60, -12, 12)));
        } else if (m.isController()) {
            const int cc = m.getControllerNumber();
            if (cc < 0 || cc > 127) continue;
            const int learning = m_learn.exchange(-1);
            if (learning >= 0) {
                for (auto& c : m_ccMap) if (c.load() == learning) c = -1;   // one controller per control
                m_ccMap[static_cast<size_t>(cc)] = learning;
            }
            const int id = m_ccMap[static_cast<size_t>(cc)].load();
            if (id < 0) continue;
            const ParamDesc& d = s.desc(id);
            const float u = static_cast<float>(m.getControllerValue()) / 127.0f;
            // A bipolar control has its middle on the controller's middle (64), exactly.
            float v = d.m_minValue + u * (d.m_maxValue - d.m_minValue);
            if (d.m_minValue < 0.0f && d.m_maxValue > 0.0f && m.getControllerValue() == 64) v = 0.0f;
            setFromMidi(id, v);
        }
    }
}

void EphemerisProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("Ephemeris");
    {
        std::lock_guard<std::mutex> g(m_lock);
        xml.setAttribute("seed", juce::String(static_cast<juce::int64>(m_seed)));
        juce::String rerolls;
        for (const auto& r : m_curation.m_rerolls) rerolls << r.first << "=" << r.second << ";";
        xml.setAttribute("rerolls", rerolls);
    }
    xml.setAttribute("params", juce::String(store().toText(true)));
    xml.setAttribute("sounds", "knobs");   // the parameters hold the composer's sounds (26.09.2026)
    juce::String cc;
    for (int c = 0; c < 128; ++c)
        if (const int id = m_ccMap[static_cast<size_t>(c)].load(); id >= 0) cc << c << "=" << juce::String(store().key(id)) << ";";
    xml.setAttribute("controllers", cc);
    xml.setAttribute("concertMinutes", static_cast<double>(m_concertMinutes));
    copyXmlToBinary(xml, destData);
}

void EphemerisProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName("Ephemeris")) return;
    store().resetDefaults();
    store().parseText(xml->getStringAttribute("params").toStdString());
    if (xml->hasAttribute("concertMinutes")) m_concertMinutes = static_cast<float>(xml->getDoubleAttribute("concertMinutes", 60.0));
    if (xml->hasAttribute("controllers")) {
        for (auto& c : m_ccMap) c = -1;
        for (const auto& item : juce::StringArray::fromTokens(xml->getStringAttribute("controllers"), ";", "")) {
            const int c = item.upToFirstOccurrenceOf("=", false, false).getIntValue();
            const int id = store().find(item.fromFirstOccurrenceOf("=", false, false).toStdString());
            if (c >= 0 && c < 128 && id >= 0) m_ccMap[static_cast<size_t>(c)] = id;
        }
    }
    {
        std::lock_guard<std::mutex> g(m_lock);
        m_seed = static_cast<uint64_t>(xml->getStringAttribute("seed").getLargeIntValue());
        m_curation = Curation{};
        for (const auto& item : juce::StringArray::fromTokens(xml->getStringAttribute("rerolls"), ";", ""))
            if (item.contains("=")) m_curation.m_rerolls[item.upToFirstOccurrenceOf("=", false, false).toStdString()] = item.fromFirstOccurrenceOf("=", false, false).getIntValue();
    }
    m_adoptNext = xml->getStringAttribute("sounds") == "knobs";   // the knobs as they were saved, the sounds among them
    m_history.clear();   // a host's state is a new beginning
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
    std::lock_guard<std::mutex> g(m_lock);
    juce::String t;
    t << "seed=" << juce::String(static_cast<juce::int64>(m_seed)) << "\nconcert=" << juce::String(m_concertMinutes) << "\nrerolls=";
    for (const auto& r : m_curation.m_rerolls) t << r.first << ":" << r.second << ";";
    return t;
}

void EphemerisProcessor::applyExtra(const juce::String& text)
{
    if (text == extraState()) return;
    bool again = false;
    {
        std::lock_guard<std::mutex> g(m_lock);
        for (const juce::String& line : juce::StringArray::fromLines(text)) {
            const juce::String k = line.upToFirstOccurrenceOf("=", false, false), v = line.fromFirstOccurrenceOf("=", false, false);
            if (k == "seed") {
                const uint64_t seed = static_cast<uint64_t>(v.getLargeIntValue());
                again = again || seed != m_seed;
                m_seed = seed;
            } else if (k == "concert") {
                m_concertMinutes = v.getFloatValue();
            } else if (k == "rerolls") {
                Curation c;
                for (const juce::String& item : juce::StringArray::fromTokens(v, ";", ""))
                    if (item.contains(":")) c.m_rerolls[item.upToLastOccurrenceOf(":", false, false).toStdString()] = item.fromLastOccurrenceOf(":", false, false).getIntValue();
                again = again || c.m_rerolls != m_curation.m_rerolls;
                m_curation = c;
            }
        }
    }
    if (again) compose();
}

void EphemerisProcessor::beginStep(const juce::String& what) { m_history.begin(what, values(), extraState(), true); }

void EphemerisProcessor::endStep() { m_history.end(values(), extraState()); }

void EphemerisProcessor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting)
{
    // Only the panel's gestures, which come on the message thread: a controller's (perform(), the audio thread) and an
    // undo's own are not steps.
    if (m_restoring || !juce::MessageManager::existsAndIsCurrentThread()) return;
    if (gestureIsStarting) {
        const ParamStore& s = store();
        m_history.begin(parameterIndex >= 0 && parameterIndex < s.count() ? juce::String(s.desc(parameterIndex).m_name) : juce::String("knob"),
                       values(), extraState(), false);
        m_history.touch(parameterIndex);
    } else {
        m_history.end(values(), extraState());
    }
}

void EphemerisProcessor::applyStep(const frame::UndoStep& step, bool after)
{
    const juce::ScopedValueSetter<bool> quiet(m_restoring, true);
    for (const auto& [id, before, now] : step.m_values) setFromUi(id, after ? now : before);
    applyExtra(after ? step.m_extraAfter : step.m_extraBefore);
}

bool EphemerisProcessor::undo()
{
    if (const frame::UndoStep* s = m_history.undo()) { applyStep(*s, false); return true; }
    return false;
}

bool EphemerisProcessor::redo()
{
    if (const frame::UndoStep* s = m_history.redo()) { applyStep(*s, true); return true; }
    return false;
}

void EphemerisProcessor::resetToDefault(int id)
{
    if (id < 0 || id >= store().count()) return;
    beginStep(juce::String(store().desc(id).m_name) + " to its default");
    setFromUi(id, store().desc(id).m_defValue);
    endStep();
}

float EphemerisProcessor::playedNormalised(int id) const
{
    const ParamStore& s = const_cast<EphemerisProcessor*>(this)->store();
    if (id < 0 || id >= s.count()) return std::numeric_limits<float>::quiet_NaN();
    const float v = m_engine.playedNow(id), k = s.get(id);
    if (std::fabs(s.toNormalised(id, v) - s.toNormalised(id, k)) < 1.0e-4f) return std::numeric_limits<float>::quiet_NaN();
    return s.toNormalised(id, v);
}

// ------------------------------------------------------------------------------------------------------------- headset

void EphemerisProcessor::pollHeadset()
{
    frame::Settings& st = frame::Settings::of("Ephemeris");
    m_headset.listen(st.headset() == frame::Settings::HeadsetModeE::OFF ? 0 : st.headsetPort());
    const frame::HeadsetEvents e = m_headset.poll();
    const ParamStore& s = store();
    {
        // The hands' moves are performing, not editing: no steps of their own.
        const juce::ScopedValueSetter<bool> quiet(m_restoring, true);
        if (e.m_playStop && wrapperType == wrapperType_Standalone) setPlaying(!isPlaying());
        if (e.m_action) {
            const int id = s.id(ModuleE::PERFORM, 0, perform::HOLD);
            setFromUi(id, s.getBool(id) ? 0.0f : 1.0f);
        }
        if (e.m_filterMoved) setFromUi(s.id(ModuleE::PERFORM, 0, perform::FILTER), e.m_filter * 2.0f);   // two octaves either way
        if (e.m_throwMoved) setFromUi(s.id(ModuleE::PERFORM, 0, perform::THROW), e.m_throwAmount);
    }
    if (e.m_next) newSeed();
}

juce::AudioProcessorEditor* EphemerisProcessor::createEditor() { return new EphemerisEditor(*this); }

/** @brief The plugin's factory, called by the JUCE wrappers. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EphemerisProcessor(); }
