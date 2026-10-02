#pragma once
/**
 * @file Jam.h
 * @brief The family jam (02.10.2026): one instrument leads, the others follow -- the same file in all five.
 *
 * Ableton Link already gives the family one tempo and one bar line. The jam adds the music: the leader says its key,
 * its sections with their energy, its breaks and its drops; a follower
 *  - moves its root to the leader's at its next bar line (a transposition of what it plays, by the shortest way), and
 *    takes the leader's key and mode for whatever it composes next (its key and scale settings);
 *  - lifts and lowers its intensity with the energy of the leader's section;
 *  - thins out while the leader is in a break, and comes back with the leader's drop.
 * Settings > Family jam: Off, Lead or Follow (Frame.h, Settings::JamRole); off by default.
 *
 * **On the network.** UDP multicast to 239.255.77.77:9177 (a time-to-live of one: this network only), OSC 1.0 in
 * messages of the family's own, so a visualiser on the score cues' port never sees them:
 * @code
 *   /family/hello   s s i       instrument, instance, role (0 off, 1 lead, 2 follow)     every second
 *   /family/key     s i s       instance, root pitch class, mode ("Aeolian", "JI Minor")  on a change, and every 2 s
 *   /family/section s s f i i   instance, kind (Intro Groove Build Drop Break Outro), energy 0..1, drop, shared bar
 * @endcode
 * The shared bar is the bar of Ableton Link's timeline (the standalone) or of the host's play position (in a DAW):
 * the one bar line the instruments have in common, so a follower acts on exactly the bar the leader means. Without one
 * (-1) it acts when the message arrives.
 *
 * **Threads.** The audio thread posts (lead) and reads (follow) through wait-free rings and atomics; the bus's own
 * thread owns the socket, sends what was posted and decodes what arrives. Nothing here allocates or locks on the
 * audio thread, and a network that is down only means that nobody is heard.
 */
#include "Frame.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>

namespace frame {

/** @brief The kinds of section the jam knows (the score cues' table in KaleidoscopeEnhanced, without its two extras). */
enum class JamSection : uint8_t { Intro, Groove, Build, Drop, Break, Outro };

/** @brief A section kind's name on the wire ("Intro" ... "Outro"). @param s the kind */
const char* jamSectionName(JamSection s);

/**
 * @brief Maps an instrument's own name of a section to the jam's table: Totality's blocks ("Block 3", "Reduction",
 *        "Return"), Parhelion's sections ("Breakdown", "Main drop"), Ephemeris' phases ("ENTRY", "PEAK") and the jam's
 *        own names. Anything else is a groove.
 * @param name   the instrument's name of the section
 * @param energy receives its energy (0..1)
 * @param drop   receives whether it lands as a drop
 * @return its kind
 */
JamSection jamSectionOf(const char* name, float& energy, bool& drop);

/** @brief What a follower plays to at one moment: the leader's root, the energy of its section, whether it is in a break. */
struct JamState {
    int root = -1;           ///< the leader's root (pitch class 0..11); -1: no leader heard
    float energy = 0.5f;     ///< the energy of the leader's section (0.5 when there is none)
    bool rhythmOut = false;  ///< the leader is in a break: the follower thins out
};

/** @brief The jam's socket, its thread, and the state of the leader a follower hears. */
class JamBus : private juce::Thread {
public:
    static constexpr const char* kGroup = "239.255.77.77";   ///< the multicast group
    static constexpr int kPort = 9177;                        ///< its port

    /** @brief A bus for instrument @p app; it opens nothing until a role is set. */
    explicit JamBus(const juce::String& app);
    /** @brief Stops the thread and closes the socket. */
    ~JamBus() override;

    /** @brief Message thread: the role from the settings; Off closes the socket and forgets the leader. */
    void setRole(Settings::JamRole r);
    /** @brief The role set last. */
    Settings::JamRole role() const { return static_cast<Settings::JamRole>(role_.load(std::memory_order_relaxed)); }

    // ---- leading: the audio thread posts, the bus's thread sends
    /**
     * @brief Audio thread: a section of this instrument begins.
     * @param s      its kind
     * @param energy its energy (0..1)
     * @param drop   whether it lands as a drop
     * @param bar    the shared bar it begins on (-1: now)
     */
    void postSection(JamSection s, float energy, bool drop, int64_t bar);
    /**
     * @brief Audio thread: this instrument's key from now on.
     * @param root its root (pitch class 0..11)
     * @param mode its mode or scale ("Aeolian"; truncated to 23 characters)
     */
    void postKey(int root, const char* mode);

    // ---- following: the bus's thread receives, the audio thread reads
    /**
     * @brief Audio thread, once a block: the leader's state as it stands at @p sharedBeat.
     * @param sharedBeat the shared timeline's beat at the block's start, or a negative number when there is none
     *                   (then whatever arrived counts at once)
     */
    JamState stateAt(double sharedBeat);
    /** @brief Message thread: the leader's mode as it said it last ("" when none), for the key it composes next. */
    juce::String leaderMode() const;
    /** @brief The leader's root as it said it last (pitch class), or -1 while none is heard. */
    int leaderRoot() const { return leaderGone(juce::Time::currentTimeMillis()) ? -1 : leaderRoot_.load(std::memory_order_relaxed); }
    /** @brief Message thread: the line the settings menu shows (who leads, who follows, what is heard). */
    juce::String status() const;

private:
    /** @brief One posted message on its way to the socket. */
    struct Out {
        uint8_t kind = 0;        ///< 0 section, 1 key
        uint8_t section = 0;     ///< JamSection, for a section
        bool drop = false;       ///< for a section: whether it lands as a drop
        int8_t root = -1;        ///< for a key: its root
        float energy = 0.5f;     ///< for a section: its energy
        int64_t bar = -1;        ///< for a section: its shared bar
        char mode[24] = {};      ///< for a key: its mode
    };
    /** @brief One section heard from the leader, waiting for its bar. */
    struct In {
        uint8_t section = 0;     ///< JamSection
        bool drop = false;       ///< whether it lands as a drop
        float energy = 0.5f;     ///< its energy
        int64_t bar = -1;        ///< its shared bar; -1: at once
    };
    /** @brief A wait-free ring for one writer and one reader. @tparam T what it carries @tparam N its capacity (a power of two) */
    template <class T, int N>
    struct Ring {
        std::array<T, N> items{};               ///< the slots
        std::atomic<uint32_t> head { 0 };       ///< the next slot to write (the writer's)
        std::atomic<uint32_t> tail { 0 };       ///< the next slot to read (the reader's)
        /** @brief Writes @p t; false when full. */
        bool push(const T& t)
        {
            const uint32_t h = head.load(std::memory_order_relaxed);
            if (h - tail.load(std::memory_order_acquire) >= static_cast<uint32_t>(N)) return false;
            items[h % N] = t;
            head.store(h + 1, std::memory_order_release);
            return true;
        }
        /** @brief Reads into @p t; false when empty. */
        bool pop(T& t)
        {
            const uint32_t tl = tail.load(std::memory_order_relaxed);
            if (tl == head.load(std::memory_order_acquire)) return false;
            t = items[tl % N];
            tail.store(tl + 1, std::memory_order_release);
            return true;
        }
    };

    /** @brief The bus's thread: sends what was posted, reads what arrives, says hello every second. */
    void run() override;
    /** @brief Opens the socket on the group's port; false when it cannot. */
    bool open();
    /** @brief Sends one OSC message @p bytes to the group. */
    void send(const juce::MemoryBlock& bytes);
    /** @brief Decodes one datagram and takes what a follower needs from it. */
    void receive(const char* data, int size);
    /** @brief Whether the leader has not been heard for a while at @p now (ms): another may take its place. */
    bool leaderGone(int64_t now) const;

    juce::String app_;                                   ///< the instrument's name
    juce::String id_;                                    ///< this instance, among others of the same name
    std::atomic<int> role_ { 0 };                        ///< Settings::JamRole
    std::unique_ptr<juce::DatagramSocket> socket_;       ///< the bus's thread's
    Ring<Out, 64> out_;                                  ///< audio thread -> bus thread
    Ring<In, 64> in_;                                    ///< bus thread -> audio thread
    std::array<In, 16> pending_{};                       ///< audio thread: sections waiting for their bar
    int pendingCount_ = 0;                               ///< audio thread: how many of pending_ are used
    JamState state_;                                     ///< audio thread: the state as it stands
    std::atomic<int> leaderRoot_ { -1 };                 ///< bus thread -> audio thread: the leader's root
    std::atomic<int64_t> leaderSeenMs_ { 0 };            ///< when the leader was last heard (Time::currentTimeMillis)
    int lastKeyRoot_ = -1;                               ///< bus thread: the key a leader sent last, sent again every 2 s
    char lastKeyMode_[24] = {};                          ///< bus thread: its mode
    Out lastSection_;                                    ///< bus thread: the section a leader sent last, sent again every 2 s
    bool haveSection_ = false;                           ///< bus thread: whether lastSection_ is one
    mutable juce::CriticalSection lock_;                 ///< guards the strings below (bus thread and message thread)
    juce::String leaderName_;                            ///< the leading instrument's name, as its hello said
    juce::String leaderId_;                              ///< its instance
    juce::String leaderMode_;                            ///< its mode
    std::map<juce::String, std::pair<int, int64_t>> peers_;   ///< instance -> (role, last heard): for the status line
};

} // namespace frame
