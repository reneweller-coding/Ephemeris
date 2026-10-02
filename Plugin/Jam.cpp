/**
 * @file Jam.cpp
 * @brief The family jam's bus (Jam.h): the OSC it speaks, its thread, and the state a follower reads.
 */
#include "Jam.h"
#include <cmath>
#include <cstring>

namespace frame {

namespace {

/** @brief The section kinds' names, in the order of JamSection. */
const char* const kSectionNames[] = { "Intro", "Groove", "Build", "Drop", "Break", "Outro" };

/** @brief An instrument's name of a section, and what it is in the jam. */
struct SectionName {
    const char* name;      ///< the instrument's name (a prefix where @c prefix is set)
    JamSection section;    ///< the kind it is
    float energy;          ///< its energy
    bool drop;             ///< whether it lands as a drop
    bool prefix;           ///< @c name is a prefix ("Block 3")
};

/** @brief Every name the family's instruments give their sections (their Cue.h), and the jam's own. */
const SectionName kNames[] = {
    { "Intro", JamSection::Intro, 0.30f, false, false },      { "Groove", JamSection::Groove, 0.55f, false, false },
    { "Build", JamSection::Build, 0.60f, false, false },      { "Drop", JamSection::Drop, 0.90f, true, false },
    { "Break", JamSection::Break, 0.35f, false, false },      { "Outro", JamSection::Outro, 0.30f, false, false },
    { "Block", JamSection::Groove, 0.60f, false, true },      { "Reduction", JamSection::Break, 0.35f, false, false },
    { "Return", JamSection::Drop, 0.90f, true, false },       { "Breakdown", JamSection::Break, 0.30f, false, false },
    { "Main drop", JamSection::Drop, 1.00f, true, false },    { "ATMOSPHERE", JamSection::Intro, 0.20f, false, false },
    { "ENTRY", JamSection::Groove, 0.45f, false, false },     { "BUILD", JamSection::Build, 0.60f, false, false },
    { "LEAD", JamSection::Drop, 0.75f, false, false },        { "PEAK", JamSection::Drop, 0.95f, true, false },
    { "BREAKDOWN", JamSection::Break, 0.35f, false, false },  { "BRIDGE", JamSection::Groove, 0.50f, false, false },
    { "CODA", JamSection::Outro, 0.30f, false, false },       { "Pdb", JamSection::Build, 0.70f, false, false },
    { "PDB", JamSection::Build, 0.70f, false, false },
    { "Cut", JamSection::Break, 0.25f, false, false },
};

/** @brief Appends an OSC string to @p b: the text, its terminator, nulls to the next multiple of four. */
void putString(juce::MemoryOutputStream& b, const char* s)
{
    const size_t n = std::strlen(s) + 1;
    b.write(s, n);
    for (size_t pad = (4 - (n & 3)) & 3; pad > 0; --pad) b.writeByte(0);
}

/** @brief Appends a big-endian int32. */
void putInt(juce::MemoryOutputStream& b, int32_t v) { b.writeIntBigEndian(v); }

/** @brief Appends a big-endian float32. */
void putFloat(juce::MemoryOutputStream& b, float v)
{
    int32_t i;
    std::memcpy(&i, &v, 4);
    b.writeIntBigEndian(i);
}

/** @brief Reads an OSC string at @p pos into @p out (cap bytes); false when it is unterminated or runs past @p size. */
bool getString(const char* d, int size, int& pos, char* out, int cap)
{
    int end = pos;
    while (end < size && d[end] != 0) ++end;
    if (end >= size) return false;
    const int len = std::min(end - pos, cap - 1);
    std::memcpy(out, d + pos, static_cast<size_t>(len));
    out[len] = 0;
    pos = (end + 4) & ~3;
    return pos <= size;
}

/** @brief Reads a big-endian int32 at @p pos; false past @p size. */
bool getInt(const char* d, int size, int& pos, int32_t& v)
{
    if (pos + 4 > size) return false;
    const auto* u = reinterpret_cast<const unsigned char*>(d + pos);
    v = static_cast<int32_t>((static_cast<uint32_t>(u[0]) << 24) | (static_cast<uint32_t>(u[1]) << 16) |
                             (static_cast<uint32_t>(u[2]) << 8) | static_cast<uint32_t>(u[3]));
    pos += 4;
    return true;
}

/** @brief Reads a big-endian float32 at @p pos; false past @p size. */
bool getFloat(const char* d, int size, int& pos, float& v)
{
    int32_t i;
    if (!getInt(d, size, pos, i)) return false;
    std::memcpy(&v, &i, 4);
    return true;
}

constexpr int64_t kLeaderTimeoutMs = 4000;   ///< a leader not heard for this long is gone: the follower plays its own way

} // namespace

const char* jamSectionName(JamSection s) { return kSectionNames[static_cast<int>(s) % 6]; }

JamSection jamSectionOf(const char* name, float& energy, bool& drop)
{
    for (const SectionName& n : kNames)
        if (n.prefix ? std::strncmp(name, n.name, std::strlen(n.name)) == 0 : std::strcmp(name, n.name) == 0) {
            energy = n.energy;
            drop = n.drop;
            return n.section;
        }
    energy = 0.5f;
    drop = false;
    return JamSection::Groove;
}

JamBus::JamBus(const juce::String& app)
    : juce::Thread(app + " jam"), app_(app), id_(juce::String::toHexString(juce::Random::getSystemRandom().nextInt()))
{
}

JamBus::~JamBus()
{
    stopThread(2000);
}

void JamBus::setRole(Settings::JamRole r)
{
    const int want = static_cast<int>(r);
    if (want == role_.load(std::memory_order_relaxed)) return;
    stopThread(2000);
    role_.store(want, std::memory_order_relaxed);
    leaderRoot_.store(-1, std::memory_order_relaxed);
    leaderSeenMs_.store(0, std::memory_order_relaxed);
    {
        const juce::ScopedLock g(lock_);
        leaderName_ = leaderId_ = leaderMode_ = {};
        peers_.clear();
    }
    if (r != Settings::JamRole::Off) startThread();
}

bool JamBus::open()
{
    socket_ = std::make_unique<juce::DatagramSocket>(false);
    socket_->setEnablePortReuse(true);   // every instrument on this machine listens on the same port
    if (!socket_->bindToPort(kPort) || !socket_->joinMulticast(kGroup)) {
        socket_.reset();
        return false;
    }
    socket_->setMulticastLoopbackEnabled(true);   // and the others on this machine hear it too
    return true;
}

void JamBus::send(const juce::MemoryBlock& bytes)
{
    if (socket_ != nullptr) socket_->write(kGroup, kPort, bytes.getData(), static_cast<int>(bytes.getSize()));
}

void JamBus::postSection(JamSection s, float energy, bool drop, int64_t bar)
{
    Out o;
    o.kind = 0;
    o.section = static_cast<uint8_t>(s);
    o.energy = energy;
    o.drop = drop;
    o.bar = bar;
    out_.push(o);
}

void JamBus::postKey(int root, const char* mode)
{
    Out o;
    o.kind = 1;
    o.root = static_cast<int8_t>(((root % 12) + 12) % 12);
    std::strncpy(o.mode, mode != nullptr ? mode : "", sizeof(o.mode) - 1);
    out_.push(o);
}

void JamBus::run()
{
    while (!threadShouldExit() && !open()) wait(1000);   // the port may be in use for a moment: try again
    int64_t lastHello = 0, lastRepeat = 0;
    char buf[1024];
    while (!threadShouldExit()) {
        const int64_t now = juce::Time::currentTimeMillis();
        const bool leading = role() == Settings::JamRole::Lead;
        // What the audio thread posted: a leader sends it, anyone else lets it go.
        Out o;
        while (out_.pop(o)) {
            if (!leading) continue;
            juce::MemoryOutputStream b;
            if (o.kind == 1) {
                putString(b, "/family/key"); putString(b, ",sis");
                putString(b, id_.toRawUTF8()); putInt(b, o.root); putString(b, o.mode);
                lastKeyRoot_ = o.root;
                std::memcpy(lastKeyMode_, o.mode, sizeof(lastKeyMode_));
            } else {
                putString(b, "/family/section"); putString(b, ",ssfii");
                putString(b, id_.toRawUTF8()); putString(b, kSectionNames[o.section % 6]); putFloat(b, o.energy);
                putInt(b, o.drop ? 1 : 0); putInt(b, static_cast<int32_t>(o.bar));
                lastSection_ = o;
                haveSection_ = true;
            }
            send(b.getMemoryBlock());
        }
        if (now - lastHello >= 1000) {   // who is here, and as what
            lastHello = now;
            juce::MemoryOutputStream b;
            putString(b, "/family/hello"); putString(b, ",ssi");
            putString(b, app_.toRawUTF8()); putString(b, id_.toRawUTF8()); putInt(b, role_.load(std::memory_order_relaxed));
            send(b.getMemoryBlock());
        }
        if (leading && now - lastRepeat >= 2000) {   // the key and the section once more, for a follower who joins late
            lastRepeat = now;
            if (lastKeyRoot_ >= 0) {
                juce::MemoryOutputStream b;
                putString(b, "/family/key"); putString(b, ",sis");
                putString(b, id_.toRawUTF8()); putInt(b, lastKeyRoot_); putString(b, lastKeyMode_);
                send(b.getMemoryBlock());
            }
            if (haveSection_) {
                juce::MemoryOutputStream b;
                putString(b, "/family/section"); putString(b, ",ssfii");
                putString(b, id_.toRawUTF8()); putString(b, kSectionNames[lastSection_.section % 6]);
                putFloat(b, lastSection_.energy); putInt(b, 0); putInt(b, -1);   // the state, not a new drop: at once
                send(b.getMemoryBlock());
            }
        }
        if (socket_ != nullptr && socket_->waitUntilReady(true, 20) == 1) {
            juce::String from;
            int port = 0;
            const int n = socket_->read(buf, sizeof(buf), false, from, port);
            if (n > 0) receive(buf, n);
        } else if (socket_ == nullptr) {
            wait(20);
        }
    }
    socket_.reset();
}

void JamBus::receive(const char* d, int size)
{
    int pos = 0;
    char address[32], tags[16], id[24];
    if (size <= 0 || (size & 3) || !getString(d, size, pos, address, sizeof(address)) || !getString(d, size, pos, tags, sizeof(tags)))
        return;
    const int64_t now = juce::Time::currentTimeMillis();
    if (std::strcmp(address, "/family/hello") == 0 && std::strcmp(tags, ",ssi") == 0) {
        char app[32];
        int32_t role = 0;
        if (!getString(d, size, pos, app, sizeof(app)) || !getString(d, size, pos, id, sizeof(id)) || !getInt(d, size, pos, role)) return;
        if (id_ == id) return;   // our own, back over the loopback
        const juce::ScopedLock g(lock_);
        peers_[juce::String(id)] = { role, now };
        if (role == static_cast<int>(Settings::JamRole::Lead) && (leaderId_.isEmpty() || leaderId_ == id || leaderGone(now))) {
            leaderId_ = id;
            leaderName_ = app;
            leaderSeenMs_.store(now, std::memory_order_relaxed);
        }
        return;
    }
    if (role() != Settings::JamRole::Follow) return;   // keys and sections are for followers
    if (std::strcmp(address, "/family/key") == 0 && std::strcmp(tags, ",sis") == 0) {
        int32_t root = -1;
        char mode[32];
        if (!getString(d, size, pos, id, sizeof(id)) || !getInt(d, size, pos, root) || !getString(d, size, pos, mode, sizeof(mode))) return;
        if (id_ == id) return;
        {
            const juce::ScopedLock g(lock_);
            if (leaderId_.isNotEmpty() && leaderId_ != id && !leaderGone(now)) return;   // one leader at a time
            leaderId_ = id;
            leaderMode_ = mode;
        }
        leaderRoot_.store(((root % 12) + 12) % 12, std::memory_order_relaxed);
        leaderSeenMs_.store(now, std::memory_order_relaxed);
        return;
    }
    if (std::strcmp(address, "/family/section") == 0 && std::strcmp(tags, ",ssfii") == 0) {
        char kind[16];
        float energy = 0.5f;
        int32_t drop = 0, bar = -1;
        if (!getString(d, size, pos, id, sizeof(id)) || !getString(d, size, pos, kind, sizeof(kind)) || !getFloat(d, size, pos, energy)
            || !getInt(d, size, pos, drop) || !getInt(d, size, pos, bar))
            return;
        if (id_ == id) return;
        {
            const juce::ScopedLock g(lock_);
            if (leaderId_.isNotEmpty() && leaderId_ != id && !leaderGone(now)) return;
            leaderId_ = id;
        }
        float e = 0.5f;
        bool dr = false;
        In in;
        in.section = static_cast<uint8_t>(jamSectionOf(kind, e, dr));
        in.energy = juce::jlimit(0.0f, 1.0f, energy);
        in.drop = drop != 0;
        in.bar = bar;
        in_.push(in);
        leaderSeenMs_.store(now, std::memory_order_relaxed);
    }
}

bool JamBus::leaderGone(int64_t now) const
{
    return now - leaderSeenMs_.load(std::memory_order_relaxed) >= kLeaderTimeoutMs;
}

JamState JamBus::stateAt(double sharedBeat)
{
    if (role() != Settings::JamRole::Follow) return {};
    In in;
    while (in_.pop(in)) {
        if (pendingCount_ < static_cast<int>(pending_.size())) pending_[static_cast<size_t>(pendingCount_++)] = in;
        else pending_[pending_.size() - 1] = in;   // a flood: the newest wins
    }
    const int64_t bar = sharedBeat >= 0.0 ? static_cast<int64_t>(std::floor(sharedBeat / 4.0 + 1.0e-6)) : -1;
    // Whatever is due -- its bar has come, or it has none, or there is no shared timeline -- in the order it came.
    int keep = 0;
    for (int i = 0; i < pendingCount_; ++i) {
        const In& p = pending_[static_cast<size_t>(i)];
        if (p.bar < 0 || bar < 0 || p.bar <= bar) {
            const auto s = static_cast<JamSection>(p.section);
            state_.energy = p.energy;
            if (s == JamSection::Break) state_.rhythmOut = true;
            else if (p.drop || s == JamSection::Drop || s == JamSection::Groove || s == JamSection::Build) state_.rhythmOut = false;
        } else {
            pending_[static_cast<size_t>(keep++)] = p;
        }
    }
    pendingCount_ = keep;
    const bool alive = juce::Time::currentTimeMillis() - leaderSeenMs_.load(std::memory_order_relaxed) < kLeaderTimeoutMs;
    if (!alive) state_ = {};   // the leader is gone: the follower plays its own way again
    else state_.root = leaderRoot_.load(std::memory_order_relaxed);
    return state_;
}

juce::String JamBus::leaderMode() const
{
    const juce::ScopedLock g(lock_);
    return leaderMode_;
}

juce::String JamBus::status() const
{
    const Settings::JamRole r = role();
    if (r == Settings::JamRole::Off) return "off";
    const juce::ScopedLock g(lock_);
    const int64_t now = juce::Time::currentTimeMillis();
    int followers = 0, others = 0;
    for (const auto& p : peers_)
        if (now - p.second.second < kLeaderTimeoutMs) {
            ++others;
            if (p.second.first == static_cast<int>(Settings::JamRole::Follow)) ++followers;
        }
    if (r == Settings::JamRole::Lead)
        return others == 0 ? juce::String("leading; nobody else heard") : "leading; " + juce::String(followers) + " following";
    const bool alive = now - leaderSeenMs_.load(std::memory_order_relaxed) < kLeaderTimeoutMs;
    if (!alive) return others == 0 ? juce::String("following; nobody heard") : juce::String("following; no leader heard");
    const int root = leaderRoot_.load(std::memory_order_relaxed);
    static const char* const kRoots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return "following " + leaderName_ + (root >= 0 ? " (" + juce::String(kRoots[root]) + " " + leaderMode_ + ")" : juce::String());
}

} // namespace frame
