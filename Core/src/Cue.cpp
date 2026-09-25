/**
 * @file Cue.cpp
 * @brief Score cues for a visualiser (Cue.h).
 */
#include "eph/Cue.h"
#include "eph/Dsp.h"
#include "eph/Params.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace eph {

namespace {

const char* const kRootNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** @brief The English name of a section, from the composer's German marker ("Aufbau 2" -> "BUILD"). */
const char* sectionOf(const std::string& marker)
{
    struct Name { const char* de; const char* en; };
    static const Name names[] = { { "Atmo", "ATMOSPHERE" }, { "Einsatz", "ENTRY" }, { "Aufbau", "BUILD" }, { "Lead", "LEAD" },
                                  { "Hoehepunkt", "PEAK" }, { "Abbau", "BREAKDOWN" }, { "Bruecke", "BRIDGE" }, { "Ausklang", "CODA" } };
    // A concert's pieces carry "Stueck N: " before the section.
    const size_t colon = marker.find(": ");
    const std::string m = colon == std::string::npos ? marker : marker.substr(colon + 2);
    for (const Name& n : names) if (m.rfind(n.de, 0) == 0) return n.en;
    return nullptr;
}

void copyText(char (&dst)[16], const char* src)
{
    std::strncpy(dst, src, sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = 0;
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}

void putString(std::vector<uint8_t>& out, const char* s)
{
    const size_t n = std::strlen(s);
    out.insert(out.end(), s, s + n);
    for (size_t pad = 4 - n % 4; pad > 0; --pad) out.push_back(0);   // at least one zero, then to four bytes
}

} // namespace

std::vector<CueMark> cueMarksOf(const Score& score)
{
    std::vector<CueMark> marks;
    // Sections: every marker the composer wrote; the phase counts the entries.
    int phase = 0;
    for (const Marker& m : score.markers) {
        const char* name = sectionOf(m.text);
        if (name == nullptr) continue;
        if (std::strcmp(name, "ENTRY") == 0) ++phase;
        CueMark c;
        c.beat = m.beat;
        c.kind = CueKind::Phase;
        c.a = std::max(1, phase);
        copyText(c.text, name);
        marks.push_back(c);
    }
    // Keys: where the root the rows play on changes.
    int last = -1;
    for (const auto& shift : score.rootShifts) {
        const int root = pitchClass(score.keyRoot + shift.second);
        if (root == last) continue;
        last = root;
        CueMark c;
        c.beat = shift.first;
        c.kind = CueKind::Key;
        copyText(c.text, kRootNames[root]);
        marks.push_back(c);
    }
    // Conjunctions: every step (a 48th of a beat is fine enough for every division) where two or more running
    // rows that play notes stand on their first step together.
    struct Row { bool known = false, transposer = false, running = false; double period = 4.0, start = 0.0; };
    Row rows[kRows];
    size_t shape = 0, ev = 0;
    std::vector<RowShape> shapes = score.rowShapes;
    std::stable_sort(shapes.begin(), shapes.end(), [](const RowShape& a, const RowShape& b) { return a.from < b.from; });
    std::vector<RackEvent> rack;
    for (const RackEvent& e : score.rack) if (e.op == RackOp::Start || e.op == RackOp::Stop) rack.push_back(e);
    std::stable_sort(rack.begin(), rack.end(), [](const RackEvent& a, const RackEvent& b) { return a.beat < b.beat; });
    const int64_t steps = static_cast<int64_t>(std::ceil(score.lengthBeats * 48.0));
    for (int64_t t = 0; t < steps; t += 3) {   // sixteenth triplets and all coarser divisions land on multiples of 1/16
        const double beat = static_cast<double>(t) / 48.0;
        while (shape < shapes.size() && shapes[shape].from <= beat + 1e-9) {
            const RowShape& r = shapes[shape++];
            if (r.row < 0 || r.row >= kRows) continue;
            rows[r.row].known = true;
            rows[r.row].transposer = r.transposer;
            rows[r.row].period = r.length * r.divBeats;
        }
        while (ev < rack.size() && rack[ev].beat <= beat + 1e-9) {
            const RackEvent& e = rack[ev++];
            const int lo = e.row < 0 ? 0 : e.row, hi = e.row < 0 ? kRows - 1 : e.row;
            for (int i = lo; i <= hi && i < kRows; ++i) {
                rows[i].running = e.op == RackOp::Start;
                if (rows[i].running) rows[i].start = e.beat;
            }
        }
        // Rows that start on this step stand on their first step by starting: they do not count as meeting.
        int running = 0, meeting = 0;
        for (const Row& r : rows) {
            if (!r.known || r.transposer || !r.running || r.period <= 0.0) continue;
            ++running;
            if (beat - r.start < 1e-9) continue;
            const double q = (beat - r.start) / r.period;
            if (std::fabs(q - std::round(q)) < 1e-6) ++meeting;
        }
        if (meeting >= 2) {
            CueMark c;
            c.beat = beat;
            c.kind = CueKind::Conjunction;
            c.a = meeting;
            c.b = static_cast<float>(meeting) / static_cast<float>(running);
            marks.push_back(c);
        }
    }
    std::stable_sort(marks.begin(), marks.end(), [](const CueMark& x, const CueMark& y) { return x.beat < y.beat; });
    return marks;
}

int CueTap::scan(const std::vector<CueMark>& marks, double from, double to, float bpm, int64_t nowNanos,
                 int64_t leadNanos, int64_t blockNanos, CueRing& ring)
{
    if (!(to > from)) return 0;
    int lost = 0;
    auto at = [&](double beat) {
        return nowNanos + leadNanos + static_cast<int64_t>(static_cast<double>(blockNanos) * (beat - from) / (to - from));
    };
    // A jump backwards (a seek): start again from the first mark at or after the new position.
    if (cursor_ > 0 && cursor_ <= marks.size() && marks[cursor_ - 1].beat >= from) cursor_ = 0;
    while (cursor_ < marks.size() && marks[cursor_].beat < from) ++cursor_;
    // The beats: every whole beat in the range.
    for (double b = std::ceil(from); b < to; b += 1.0) {
        Cue c;
        c.dueNanos = at(b);
        c.kind = CueKind::Beat;
        c.a = static_cast<int32_t>(b);
        c.b = bpm;
        if (!ring.push(c)) ++lost;
    }
    for (; cursor_ < marks.size() && marks[cursor_].beat < to; ++cursor_) {
        const CueMark& m = marks[cursor_];
        Cue c;
        c.dueNanos = at(m.beat);
        c.kind = m.kind;
        c.a = m.a;
        c.b = m.b;
        std::memcpy(c.text, m.text, sizeof(c.text));
        if (!ring.push(c)) ++lost;
    }
    return lost;
}

std::vector<uint8_t> oscMessage(const char* address, const char* tags, const int32_t* ints, const float* floats, const char* const* strings)
{
    std::vector<uint8_t> out;
    putString(out, address);
    const std::string t = std::string(",") + tags;
    putString(out, t.c_str());
    int ni = 0, nf = 0, ns = 0;
    for (const char* p = tags; *p != 0; ++p) {
        if (*p == 'i') put32(out, static_cast<uint32_t>(ints[ni++]));
        else if (*p == 'f') { uint32_t u; std::memcpy(&u, &floats[nf++], 4); put32(out, u); }
        else if (*p == 's') putString(out, strings[ns++]);
    }
    return out;
}

std::vector<uint8_t> oscOf(const Cue& c)
{
    const char* text = c.text;
    switch (c.kind) {
    case CueKind::Beat:        return oscMessage("/eph/beat", "if", &c.a, &c.b, nullptr);
    case CueKind::Phase:       return oscMessage("/eph/phase", "si", &c.a, nullptr, &text);
    case CueKind::Key:         return oscMessage("/eph/key", "s", nullptr, nullptr, &text);
    case CueKind::Conjunction: return oscMessage("/eph/conjunction", "if", &c.a, &c.b, nullptr);
    }
    return {};
}

int64_t CueSender::nowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool CueSender::start(const std::string& host, int port)
{
    stop();
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) return false;
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    static_assert(sizeof(address_) >= sizeof(sockaddr_in), "room for the address");
    std::memcpy(address_, &addr, sizeof(addr));
    const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(_WIN32)
    if (s == INVALID_SOCKET) return false;
#else
    if (s < 0) return false;
#endif
    socket_ = static_cast<intptr_t>(s);
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void CueSender::stop()
{
    if (thread_.joinable()) {
        stop_ = true;
        thread_.join();
    }
    running_ = false;
    if (socket_ >= 0) {
#if defined(_WIN32)
        closesocket(static_cast<SOCKET>(socket_));
        WSACleanup();
#else
        close(static_cast<int>(socket_));
#endif
        socket_ = -1;
    }
}

void CueSender::loop()
{
    Cue c;
    while (!stop_.load(std::memory_order_acquire)) {
        if (!ring_.pop(c)) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        // Hold it back until the listener hears it (a cue that is late already goes at once).
        while (!stop_.load(std::memory_order_acquire)) {
            const int64_t wait = c.dueNanos - nowNanos();
            if (wait <= 0) break;
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<int64_t>(wait, 2000000)));
        }
        const std::vector<uint8_t> msg = oscOf(c);
        const auto sent = sendto(static_cast<decltype(socket(0, 0, 0))>(socket_), reinterpret_cast<const char*>(msg.data()),
                                 static_cast<int>(msg.size()), 0, reinterpret_cast<const sockaddr*>(address_), sizeof(sockaddr_in));
        if (sent < 0) dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace eph
