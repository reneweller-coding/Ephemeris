/**
 * @file PluginProcessor.h
 * @brief The plugin (PLAN 8.1, Phase 5): the engine, the composer on a thread of its own, transport.
 *
 * **Parameters.** Every entry of the engine's ParamStore is a host parameter (StoreParameter, after
 * Phosphene's): the store is the only place a value lives, read and written as a relaxed atomic, so a
 * knob in the plugin and the same knob in eph_render stand at the same place.
 *
 * **Composing.** "Compose" snapshots the parameters, the seed and the rerolls and hands them to the
 * composer thread; the finished score waits until the message thread loads it into the engine with
 * processing suspended -- the engine allocates when it loads, and the audio thread never does.
 *
 * **Time.** In a host the playhead is the clock: while the host plays, the engine follows its position
 * in beats and jumps (Engine::seek) when the host does, and it plays at the host's tempo: the score is
 * loaded with the host's tempo as a constant (forPlayback), and a new host tempo loads it again on the
 * message thread, from the beat the engine was at. The piece's own tempo changes (a new tempo in a
 * bridge) are the standalone's and the export's; in a DAW the song's tempo track decides. The
 * standalone has its own play and stop.
 *
 * **Performing** (PLAN 8.1). The perform module's controls act on the piece as it plays (Params.h,
 * perform). MIDI reaches them: a key transposes the rows by its distance from middle C, as the
 * transposition key of a sequencer did (it holds until the next key); controllers move the controls
 * they are bound to -- the mod wheel the filter, the expression pedal the echo throw, the sustain pedal
 * the hold -- and any controller can be learned for any of them (learn()). The bindings are part of the
 * state.
 *
 * **Cues** (PLAN 8.3, Cue.h): with cue.enabled the score's beats, sections, keys and conjunctions go out as
 * OSC over UDP to `EPH_CUE_HOST` (default this machine) at cue.port, each at the moment it is heard.
 *
 * **Mute** (after Phosphene). The output can be muted: silence at the very end of processBlock, after the
 * meters and the test recording have read the block. `EPH_MUTE=1` -- and the screenshot mode `EPH_SHOT`
 * -- start the plugin muted, and then it never unmutes itself: an automated run makes no sound.
 * @author Rene Weller
 */
#pragma once
#include "eph/Presets.h"
#include "eph/Engine.h"
#include "eph/SetFile.h"
#include "Frame.h"
#include "LinkClock.h"
#include "Jam.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>

/** @brief One entry of the ParamStore as a host parameter. */
class StoreParameter final : public juce::RangedAudioParameter
{
public:
    /**
     * @brief Binds parameter @p id of @p store (which outlives this object) under the display name @p name.
     * @param[in,out] store  the parameter store
     * @param[in]     id     the parameter
     * @param[in]     name   the display name
     */
    StoreParameter( eph::ParamStore &store, int id, const juce::String &name );
    /**
     * the store's value, normalised
     * @return the store's value, normalised
     */
    float getValue() const override;
    /**
     * writes the store (relaxed atomic)
     * @param[in] newValue  the value, normalised
     */
    void setValue( float newValue ) override;
    /**
     * the descriptor's default, normalised
     * @return the default, normalised
     */
    float getDefaultValue() const override;
    /**
     * the display name (0: no limit)
     * @param[in] maximumStringLength  the longest name the host takes, 0 for no limit
     * @return the display name
     */
    juce::String getName( int maximumStringLength ) const override;
    /**
     * the unit
     * @return the unit
     */
    juce::String getLabel() const override;
    /**
     * steps of a discrete parameter
     * @return the steps of a discrete parameter
     */
    int getNumSteps() const override;
    /**
     * Int, Choice and Toggle are discrete
     * @return true for Int, Choice and Toggle
     */
    bool isDiscrete() const override;
    /**
     * a Toggle is boolean
     * @return true for a Toggle
     */
    bool isBoolean() const override;
    /**
     * the value as the panel shows it
     * @param[in] normalisedValue      the value, normalised
     * @param[in] maximumStringLength  the longest text the host takes
     * @return the value as the panel shows it
     */
    juce::String getText( float normalisedValue, int maximumStringLength ) const override;
    /**
     * parses a choice name, On/Off or a number
     * @param[in] text  a choice name, On/Off or a number
     * @return the value, normalised
     */
    float getValueForText( const juce::String &text ) const override;
    /**
     * the store's own mapping
     * @return the store's mapping
     */
    const juce::NormalisableRange<float> &getNormalisableRange() const override { return m_range; }
    /**
     * the id in the store
     * @return the id in the store
     */
    int paramId() const { return m_id; }

private:
    eph::ParamStore               &m_store;   ///< the store it reads and writes
    int                            m_id;      ///< the id in the store
    juce::String                   m_name;    ///< the display name
    juce::NormalisableRange<float> m_range;   ///< the store's mapping between the real and the normalised value
};

/** @brief The Ephemeris processor. */
class EphemerisProcessor final : public juce::AudioProcessor,
                                 private juce::Thread,
                                 private juce::Timer,
                                 private juce::AudioProcessorParameter::Listener
{
public:
    EphemerisProcessor();             ///< registers every parameter and composes a first piece
    ~EphemerisProcessor() override;   ///< stops the composer and the exporter

    // Composing and curating.
    void compose();   ///< compose with the current settings, seed and rerolls
    void newSeed();   ///< a fresh seed, no rerolls, then compose
    /**
     * draw one unit again, then compose
     * @param[in] unit  the unit drawn again
     */
    void reroll( const juce::String &unit );
    /**
     * whether the composer thread is at work
     * @return true while the composer thread works
     */
    bool isComposing() const { return m_composing.load(); }
    /**
     * @brief What the knobs ask Compose for: 0 a piece, 1 a concert, 2 a night set (compose.concert_minutes,
     *        night_set).
     * @return 0 a piece, 1 a concert, 2 a night set
     */
    int chosenKind() const;
    /**
     * @brief A piece, a concert or a night set (message thread, 01.10.2026 -- as Parhelion and Totality have it): the
     *        concert's length to 0, or back to its last (60 min at first), the night switch; then composes, if what plays
     *        is not that already.
     * @param[in] kind  0 a piece, 1 a concert, 2 a night set
     * @pre called on the message thread
     */
    void chooseKind( int kind );
    /**
     * what plays: 0 a piece, 1 a concert, 2 a night set
     * @return 0 a piece, 1 a concert, 2 a night set
     */
    int playingKind() const { return m_playingKind.load(); }
    /**
     * @brief Sets store id @p id to the real value @p value as a hand on the panel does (one gesture, the host told).
     * @param[in] id     the store id
     * @param[in] value  the real value
     */
    void setFromUi( int id, float value );
    /**
     * the seed of what plays
     * @return the seed
     */
    uint64_t seed() const
    {
        std::lock_guard<std::mutex> g( m_lock );
        return m_seed;
    }
    /**
     * compose.concert_minutes
     * @return the concert's length in minutes, 0 for a piece
     */
    double concertMinutes() const;
    /**
     * the rerolls, for the panel
     * @return the rerolls as text
     */
    juce::String curationText() const;

    // Transport (the standalone's; a host drives its own).
    /**
     * play or stop (the standalone's transport)
     * @param[in] on  true to play
     */
    void setPlaying( bool on ) { m_playing = on; }
    /**
     * whether the standalone plays
     * @return true while it plays
     */
    bool isPlaying() const { return m_playing.load(); }
    /**
     * @brief The standalone's Ableton Link, as the settings menu says it: off, alone, or how many apps are with it.
     * @return the Link's state as text
     */
    juce::String linkStatus() const;
    /**
     * @brief The family jam, as the settings menu says it (Jam.h): off, leading, or whom it follows.
     * @return the jam's state as text
     */
    juce::String jamStatus() const { return m_jam.status(); }
    /**
     * jump to @p beat at the next block
     * @param[in] beat  the position in beats
     */
    void seekTo( double beat ) { m_seekRequest = beat; }
    /**
     * where the audio thread is, in beats
     * @return the position in beats
     */
    double positionBeats() const { return m_position.load(); }
    /**
     * @brief Counts the scores the engine has loaded: a display compares it to know when to copy again.
     * @return the number of scores loaded
     */
    int scoreVersion() const { return m_scoreVersion.load(); }
    /**
     * @brief A copy of the score the engine plays (message thread; for the orrery).
     * @param[out] out  the copy
     * @pre called on the message thread
     */
    void copyScore( eph::Score &out ) const
    {
        std::lock_guard<std::mutex> g( m_lock );
        out = m_current;
    }
    /**
     * @brief The factory preset the composer chose for instance @p instance of synth @p m where the piece now plays
     *        (Score::sounds, compose.pick_sounds): its index in eph::factoryPresets(m), -1 if none.
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @return the preset's index, -1 for none
     */
    int composedPreset( eph::ModuleE m, int instance ) const;
    /**
     * @brief A copy of the markers and the length, for the arrange view.
     * @param[out] markers      the markers
     * @param[out] lengthBeats  the length in beats
     * @param[out] seconds      the length in seconds
     */
    void arrangement( std::vector<eph::Marker> &markers, double &lengthBeats, double &seconds ) const;

    // Files.
    /**
     * writes seed, lengths, rerolls and parameters as an .ephset
     * @param[in] file  the file
     * @return false if it cannot be written
     */
    bool saveSet( const juce::File &file );
    /**
     * reads an .ephset and composes it
     * @param[in] file  the file
     * @return false if it cannot be read
     */
    bool loadSet( const juce::File &file );
    /**
     * @brief Renders the current score offline to WAV and MIDI beside each other, on a thread; returns at once.
     *        With @p stems also a 24-bit WAV per channel strip and one for the rooms (Engine::setStems) into the
     *        folder "\<name\>_stems" beside them: their sum is the mix before the master.
     * @param[in] wav    the WAV file
     * @param[in] stems  true for the stems too
     */
    void exportTo( const juce::File &wav, bool stems = false );
    /**
     * one line for the panel
     * @return the status line
     */
    juce::String status() const;
    /**
     * @brief The test mode (EPH_SEED, EPH_PLAY = seconds, EPH_RECORD = a WAV file): a fixed seed, play at once,
     *        record what the audio thread renders; recordingDone() when the seconds are full.
     * @return true when the test mode's seconds are recorded
     */
    bool recordingDone() const { return m_recordTarget > 0 && m_recordPos.load() >= m_recordTarget; }
    /**
     * writes the recording (message thread)
     * @pre called on the message thread
     */
    void writeRecording();
    /**
     * @brief The channel meters since the last call (message thread): per eph::Engine channel the peak and the RMS
     *        of what it put into the mix, 0 where nothing was played.
     * @param[out] peak  the peak per channel
     * @param[out] rms   the RMS per channel
     * @pre called on the message thread
     */
    void takeChannelMeters( float *peak, float *rms );

    // Muting.
    /**
     * the output is silenced
     * @return true while the output is silenced
     */
    bool muted() const { return m_mute.load( std::memory_order_relaxed ); }
    /**
     * @brief Mutes or unmutes; does nothing while `EPH_MUTE` forces it.
     * @param[in] on  true to mute
     */
    void setMuted( bool on )
    {
        if ( !m_forceMute ) m_mute.store( on, std::memory_order_relaxed );
    }
    /**
     * `EPH_MUTE` (or `EPH_SHOT`) was set: the switch is stuck on
     * @return true when the environment forces the mute
     */
    bool muteForced() const { return m_forceMute; }

    // Performing.
    /**
     * @brief Binds the next MIDI controller that arrives to store id @p id; -1 cancels.
     * @param[in] id  the store id, -1 to cancel
     */
    void learn( int id ) { m_learn = id; }
    /**
     * the store id waiting for a controller, or -1
     * @return the store id, or -1
     */
    int learning() const { return m_learn.load(); }
    /**
     * @brief The controller bound to store id @p id, or -1.
     * @param[in] id  the store id
     * @return the controller, or -1
     */
    int controllerFor( int id ) const;
    /**
     * @brief Unbinds store id @p id (the controls' right-click menu).
     * @param[in] id  the store id
     */
    void forget( int id )
    {
        for ( auto &c : m_ccMap )
            if ( c.load() == id ) c = -1;
    }

    // Undo (the frame, 01.10.2026): a knob turned on the panel, a preset, a new seed, a reroll, a loaded set or a choice of
    // piece, concert or night set is a step; a step holds only what it changed, so taking it back never takes back the
    // sounds the composer wrote on the knobs since. Knobs moved from MIDI or by a host's automation are not steps.
    /**
     * takes the last step back; false if there was none
     * @return false if there was no step
     */
    bool undo();
    /**
     * makes the last undone step again
     * @return false if there was no step
     */
    bool redo();
    /**
     * what undo takes back (empty: nothing)
     * @return the step's name, empty for none
     */
    juce::String undoName() const { return m_history.undoName(); }
    /**
     * what redo makes again
     * @return the step's name, empty for none
     */
    juce::String redoName() const { return m_history.redoName(); }
    /**
     * @brief Opens a step of several knobs (a preset, a reset): they are undone together. Close with endStep.
     * @param[in] what  the step's name
     */
    void beginStep( const juce::String &what );
    void endStep();   ///< closes beginStep's step
    /**
     * @brief Store id @p id back to its default (one step).
     * @param[in] id  the store id
     */
    void resetToDefault( int id );
    /**
     * @brief The value store id @p id plays at the moment (the moves' offsets), normalised -- NaN where it is the
     *        knob's own.
     * @param[in] id  the store id
     * @return the value played, normalised, or NaN
     */
    float playedNormalised( int id ) const;
    /**
     * @brief The headset (the frame): the hands of the Quest app in bridge mode, as OSC, while the settings do not say
     *        Off -- left pinch play and stop, both hands the next piece, right pinch holds the moves, the left hand's
     *        height the rows' filters, the right hand's the echo throw.
     * @return the headset
     */
    frame::Headset &headset() { return m_headset; }

    /**
     * the engine's parameters
     * @return the parameters
     */
    eph::ParamStore &store() { return m_engine.params(); }
    /** @brief The host parameter of store id @p id, or null. */
    /**
     * @brief Applies factory preset @p index of @p module (Presets.h) to instance @p instance, through the host's
     *        parameters.
     * @param[in] module    the synth
     * @param[in] instance  its instance
     * @param[in] index     the factory preset's index
     */
    void applyPreset( eph::ModuleE module, int instance, int index );
    /**
     * @brief Sets the knobs @p values (store id, real value) through the host's parameters as one undo step named @p what
     *        (15.10.2026: a value typed into the command palette).
     * @param[in] what    the undo step's name
     * @param[in] values  (store id, real value) of the knobs
     */
    void applyKnobs( const juce::String &what, const std::vector<std::pair<int, float>> &values );
    /**
     * @brief Plays @p values (store id, real value) while a preset is listened to in the browser -- in the store only: no
     *        undo step, nothing to the host --; endPreview() takes them back.
     * @param[in] values  (store id, real value) of the knobs
     */
    void previewKnobs( const std::vector<std::pair<int, float>> &values );
    /** @brief Back to the knobs as they were before previewKnobs (nothing when no preview plays). */
    void endPreview();
    /**
     * @brief Applies @p preset (a factory or a user preset) to instance @p instance of @p module, through the host.
     * @param[in] module    the synth
     * @param[in] instance  its instance
     * @param[in] preset    the preset
     */
    void applyPresetValues( eph::ModuleE module, int instance, const eph::SoundPreset &preset );
    /**
     * @brief The user's presets of a synth: the text files in its folder under the application data, by name.
     * @param[in] module  the synth
     * @return its user presets, by name
     */
    std::vector<eph::SoundPreset> userPresets( eph::ModuleE module ) const;
    /**
     * @brief Saves the synth's knobs on @p instance as a user preset named @p name; false if it cannot be written.
     * @param[in] module    the synth
     * @param[in] instance  its instance
     * @param[in] name      the preset's name
     * @return false if it cannot be written
     */
    bool saveUserPreset( eph::ModuleE module, int instance, const juce::String &name );
    /**
     * @brief Sets every `key=value` line of @p text (full keys, e.g. reverb.decay) through the host's parameters.
     * @param[in] text  the key=value lines
     */
    void applyKeyText( const juce::String &text );
    /**
     * @brief The host parameter of store id @p id, or null.
     * @param[in] id  the store id
     * @return its host parameter, or null
     */
    StoreParameter *parameter( int id )
    {
        return id >= 0 && id < static_cast<int>( m_params.size() ) ? m_params[static_cast<size_t>( id )] : nullptr;
    }

    // juce::AudioProcessor: a stereo instrument without MIDI, one program, the state as XML.
    /**
     * prepares the engine and reloads the score
     * @param[in] sampleRate       the sample rate
     * @param[in] samplesPerBlock  the largest block
     */
    void prepareToPlay( double sampleRate, int samplesPerBlock ) override;
    void releaseResources() override {}   ///< nothing to release
    /**
     * stereo out only
     * @param[in] layouts  the layout asked for
     * @return true for a stereo output
     */
    bool isBusesLayoutSupported( const BusesLayout &layouts ) const override;
    /**
     * plays, following the host's playhead
     * @param[out]    buffer  the block's audio
     * @param[in,out] midi    the block's MIDI in, then the composer's notes out
     */
    void processBlock( juce::AudioBuffer<float> &, juce::MidiBuffer & ) override;
    /**
     * the panel
     * @return a new panel
     */
    juce::AudioProcessorEditor *createEditor() override;
    /**
     * it has one
     * @return true
     */
    bool hasEditor() const override { return true; }
    /**
     * "Ephemeris"
     * @return the plugin's name
     */
    const juce::String getName() const override { return JucePlugin_Name; }
    /**
     * MIDI in: the performer's keys and controllers
     * @return true
     */
    bool acceptsMidi() const override { return true; }
    /**
     * MIDI out: the composer's notes as they play (02.10.2026)
     * @return true
     */
    bool producesMidi() const override { return true; }
    /**
     * the rooms ring on
     * @return the tail in seconds
     */
    double getTailLengthSeconds() const override { return 8.0; }
    /**
     * one program
     * @return 1
     */
    int getNumPrograms() override { return 1; }
    /**
     * always the one
     * @return 0
     */
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram( int ) override {}   ///< nothing to switch
    /**
     * "Set"
     * @return the program's name
     */
    const juce::String getProgramName( int ) override { return "Set"; }
    void changeProgramName( int, const juce::String & ) override {}   ///< not renameable
    /**
     * seed, rerolls and parameters as XML
     * @param[out] destData  the state
     */
    void getStateInformation( juce::MemoryBlock &destData ) override;
    /**
     * restores them and composes
     * @param[in] data         the state
     * @param[in] sizeInBytes  its size in bytes
     */
    void setStateInformation( const void *data, int sizeInBytes ) override;

private:
    /** @brief Nothing: a value alone is no step (the gestures are). */
    void parameterValueChanged( int, float ) override {}
    /**
     * a knob on the panel: a step
     * @param[in] parameterIndex     the host parameter's index
     * @param[in] gestureIsStarting  true when the gesture begins
     */
    void parameterGestureChanged( int parameterIndex, bool gestureIsStarting ) override;
    /**
     * every store value (undo)
     * @return every store value
     */
    std::vector<float> values() const;
    /**
     * seed, rerolls and the concert's length as text (undo)
     * @return the state beyond the knobs as text
     */
    juce::String extraState() const;
    /**
     * the inverse; composes if seed or rerolls changed
     * @param[in] text  the state as extraState() wrote it
     */
    void applyExtra( const juce::String &text );
    /**
     * @brief Puts undo step @p s back (@p after false) or makes it again (@p after true).
     * @param[in] s      the undo step
     * @param[in] after  true to make it again, false to take it back
     */
    void applyStep( const frame::UndoStep &s, bool after );
    /**
     * the hands' events, 30 times a second (message thread)
     * @pre called on the message thread
     */
    void pollHeadset();
    frame::UndoHistory m_history;                        ///< undo and redo (message thread)
    bool               m_restoring = false;              ///< an undo or the headset moves knobs: no step of their own
    std::vector<std::pair<int, float>> m_previewSaved;   ///< the knobs a preview replaced (store id, real value)
    frame::Headset                     m_headset;        ///< the Quest's hands (message thread)
    juce::TimedCallback m_headsetTick{ [this] { pollHeadset(); } };   ///< polls the headset 30 times a second
    void run() override;                                              ///< the composer thread
    /**
     * loads a finished score on the message thread
     * @pre called on the message thread
     */
    void timerCallback() override;
    /**
     * composes with the knobs as they are (copied into @p snapshot)
     * @param[out] snapshot  the knobs composed with
     * @return the score
     */
    eph::Score composeNow( eph::ParamStore &snapshot );
    /**
     * @brief Hands a piece's loudness corrections, measured while it plays, to the engine (message thread).
     * @pre called on the message thread
     */
    void takeTrims();
    /**
     * @brief @p s as the engine plays it here: in a host at the host's tempo (constant, the piece's tempo
     *        changes come only through the export and its MIDI file), in the standalone as composed.
     * @param[in] s  the score
     * @return the score as it plays here
     */
    eph::Score forPlayback( const eph::Score &s ) const;
    /**
     * @brief The performer's MIDI: keys transpose, controllers move what they are bound to (audio thread).
     * @param[in] midi  the block's MIDI
     * @pre called on the audio thread
     */
    void perform( const juce::MidiBuffer &midi );
    int m_keyboardSeen = 0;   ///< the keyboard target of the last block (audio thread): a change releases every key
    // MIDI out (02.10.2026): the composer's notes as the engine plays them, on the channels of the MIDI export (Midi.h).
    eph::NoteTap m_noteTap;   ///< what the engine played in the last process() call (audio thread)
    frame::KeyMemory
        m_keyMemory;   ///< where each held key went (the split, Scale Lock), so its release follows (02.10.2026)
    // The stems as outputs of their own (02.10.2026): a stereo bus per channel strip and one for the rooms (Engine::setStems),
    // off until the host switches one on; pre-master, as the export's stems.
    static constexpr int          StemOuts = eph::Engine::Channels + 1;   ///< the strips and the rooms
    std::vector<float>            m_stemBuf;                              ///< StemOuts x 2 x block (prepareToPlay)
    std::array<float *, StemOuts> m_stemL{};                              ///< per stem: its left channel in m_stemBuf
    std::array<float *, StemOuts> m_stemR{};                              ///< per stem: its right channel in m_stemBuf
    bool                          m_stemsOn = false;   ///< audio thread: the engine writes the stems now
    /**
     * @brief The buses: the main stereo output, then one stereo output per strip and one for the rooms, those off.
     * @return the buses
     */
    static BusesProperties busLayout();
    int64_t m_midiExpect = -1;        ///< the sample the next block should start at; another is a jump
    bool    m_midiSounding = false;   ///< a note-on went out since the last all-notes-off
    /**
     * @brief Writes the notes the engine played in the block from @p start (@p n samples) into @p midi.
     * @param[in,out] midi   the block's MIDI, added to
     * @param[in]     start  the block's first sample
     * @param[in]     n      the block's samples
     */
    void emitMidi( juce::MidiBuffer &midi, int64_t start, int n );
    /**
     * @brief All notes off on every channel, at the block's start, when a note sounded (a stop, a jump).
     * @param[in,out] midi  the block's MIDI, added to
     */
    void silenceMidi( juce::MidiBuffer &midi );
    /**
     * @brief Sets store id @p id to the real value @p value through its host parameter.
     * @param[in] id     the store id
     * @param[in] value  the real value
     */
    void setFromMidi( int id, float value );

    eph::Engine                   m_engine;     ///< the engine
    std::vector<StoreParameter *> m_params;     ///< the host parameters, one per store id (owned by the processor)
    uint64_t                      m_seed = 1;   ///< the seed of what plays
    eph::Curation                 m_curation;   ///< the rerolls and locks
    mutable std::mutex            m_lock;       ///< guards what the composer thread hands over
    std::unique_ptr<eph::Score>   m_pending;    ///< composed, waiting to be loaded
    eph::Score                    m_current;    ///< what the engine plays (for reloading and the arrange view)
    std::atomic<bool>             m_composing{ false };   ///< the composer thread works
    std::atomic<bool>             m_playing{ false };     ///< play is on
    std::atomic<bool>             m_exporting{ false };   ///< an export runs
    std::atomic<bool>             m_again{ false };   ///< compose was asked for while composing: once more when done
    // The loudness (Leveler.h): measured on the composer thread once the piece is handed over, while it plays.
    std::atomic<bool>  m_newer{ false };     ///< a newer piece is asked for: the measuring of the last one stops
    uint64_t           m_composed = 0;       ///< counts the compositions
    uint64_t           m_pendingId = 0;      ///< m_pending's number (m_lock)
    uint64_t           m_playingId = 0;      ///< m_current's number (m_lock)
    std::vector<float> m_trims;              ///< the corrections found for the composition m_trimsFor (m_lock)
    uint64_t           m_trimsFor = 0;       ///< the composition m_trims and bal_ belong to (m_lock)
    bool               m_levelled = false;   ///< m_current carries its corrections (m_lock)
    int                m_pendingKind = 0;    ///< what m_pending is: 0 a piece, 1 a concert, 2 a night set (m_lock)
    std::atomic<int>   m_playingKind{ 0 };   ///< ... and m_current (playingKind())
    float m_concertMinutes = 60.0f;          ///< the concert's length while a piece is chosen (chooseKind; the state)
    // The composer's sounds (Score::knobs, 26.09.2026): a new piece puts them on the knobs of the synths whose sound it
    // changes; after a restored state or a loaded set the knobs already hold them.
    std::atomic<bool> m_adoptNext{ false };     ///< the next composition's sounds are already on the knobs
    bool              m_pendingAdopt = false;   ///< ... and the one in m_pending was such a composition
    uint32_t          m_toldSounds = 0;         ///< the engine's soundsVersion() the host and the pages were last told
    /**
     * @brief Puts @p next's sounds at its start on the knobs of every synth whose sound differs from the playing
     *        score's.
     * @param[in] next  the score coming
     */
    void takeSounds( const eph::Score &next );
    /**
     * @brief Tells the host and the pages the values of the knobs @p s sets (the engine set them).
     * @param[in] s  the score
     */
    void tellSounds( const eph::Score &s );
    std::atomic<double>          m_position{ 0.0 };        ///< where the engine is, beats (audio thread writes)
    std::atomic<double>          m_seekRequest{ -1.0 };    ///< a jump asked for, beats; -1 none
    double                       m_sampleRate = 48000.0;   ///< the sample rate, Hz
    int                          m_blockSize = 512;        ///< the largest block, samples
    juce::String                 m_lastExport;             ///< the last export's result, for the Export tab
    std::unique_ptr<std::thread> m_exporter;               ///< the export thread while it runs
    std::vector<float>           m_record;   ///< interleaved, allocated in prepareToPlay in the test mode only
    size_t                       m_recordTarget = 0;   ///< TOT_RECORD: how many values m_record takes, 0 off
    std::atomic<size_t>          m_recordPos{ 0 };     ///< how many are written
    bool                         m_autoPlay = false;   ///< TOT_PLAY: play once the first score is loaded
    std::array<std::atomic<float>, eph::Engine::Channels>
        m_meterPeak{};   ///< audio thread raises, the editor takes (exchange 0)
    std::array<std::atomic<double>, eph::Engine::Channels>
                     m_meterSum{};          ///< sums of squares since the editor last took them
    std::atomic<int> m_meterCount{ 0 };     ///< samples in those sums
    std::atomic<int> m_scoreVersion{ 0 };   ///< scoreVersion()
    eph::CueSender   m_cues;                ///< the OSC cues' socket and thread (message thread starts and stops it)
    eph::CueTap      m_cueTap;              ///< audio thread: beat range -> cues
    int              m_cuePort = 0;         ///< the port the sender was started for, 0 = off (message thread)
    double           m_lastBeat = -1.0;     ///< the beat after the last block (audio thread), to see a jump
    std::array<std::atomic<int>, 128> m_ccMap{};             ///< controller number -> store id, -1 unbound
    std::atomic<int>                  m_learn{ -1 };         ///< learn()
    std::atomic<bool>                 m_mute{ false };       ///< muted()
    bool                              m_forceMute = false;   ///< muteForced()
    std::atomic<double> m_hostBpm{ 0.0 };   ///< the host's tempo as the audio thread last saw it, 0 outside a host
    std::atomic<double> m_playedBpm{
        0.0
    };   ///< the tempo the engine's score was loaded with (forPlayback), 0 as composed
    // Ableton Link in the standalone (02.10.2026, LinkClock.h; Settings > Ableton Link).
    frame::LinkClock  m_link;   ///< the session (joined on the timer when the setting is on)
    std::atomic<bool> m_linkFollowing{
        false
    };   ///< other apps are in the session: it rules tempo and phase as a host does
    double m_linkBeat = -1.0;   ///< audio thread: the session's beat at this block (the jam's timeline)
    // The family jam (02.10.2026, Jam.h; Settings > Family jam).
    /**
     * @brief Audio thread: as the jam's leader, the phases and roots of the coming beat go out, stamped with their bar.
     * @param[in] before  the block's first beat
     * @param[in] after   the beat after the block
     * @param[in] shared  the shared clock's beat
     * @pre called on the audio thread
     */
    void jamLead( double before, double after, double shared );
    /**
     * @brief Audio thread: as a follower, the engine takes the leader's root (on a bar line), energy and breaks.
     * @param[in] before  the block's first beat
     * @param[in] shared  the shared clock's beat
     * @pre called on the audio thread
     */
    void jamFollow( double before, double shared );
    /**
     * @brief Which of compose.scale's values a mode named @p mode is (-1: none of them).
     * @param[in] mode  the mode's name
     * @return the scale's value, -1 for none
     */
    static int scaleOfMode( const juce::String &mode );
    frame::JamBus    m_jam{ "Ephemeris" };       ///< the family jam's bus
    bool             m_jamLeadStarted = false;   ///< audio thread: the leader has said where it stands
    double           m_jamLeadLast = -1.0;       ///< audio thread: where the leader's last block ended (a jump)
    double           m_jamLastBefore = -1.0;     ///< audio thread: the previous block's start (its length in beats)
    int              m_jamTranspose = 0;         ///< audio thread: the transposition the engine plays now
    std::atomic<int> m_jamTransposeOut{ 0 };     ///< the same, for the message thread
    bool             m_jamFollowing = false;     ///< audio thread: the engine follows the jam (setJam was called)
    std::atomic<int> m_jamComposeKey{ -1 };      ///< a follower's next piece: the leader's root (-1: its own key)
    std::atomic<int> m_jamComposeScale{ -1 };    ///< and its mode as compose.scale (-1: its own)
    juce::String     m_jamLogged;                ///< the last line written to FAMILY_JAM_LOG
    bool             m_linkPlayed = false;       ///< audio thread: the transport last told to or taken from the session
    /**
     * @brief Whether a clock outside rules the tempo: a host's playhead, or a Link session with other apps in it.
     * @return true when a clock outside rules the tempo
     */
    bool followsClock() const
    {
        return wrapperType != wrapperType_Standalone || m_linkFollowing.load( std::memory_order_relaxed );
    }
};
