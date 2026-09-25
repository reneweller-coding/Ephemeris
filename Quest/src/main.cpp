/**
 * @file main.cpp
 * @brief Ephemeris for Meta Quest: the whole generator on the headset, played with the hands (PLAN 8.2).
 *
 * No game engine -- `NativeActivity` + `android_native_app_glue`, EGL, GLES 3, the Khronos OpenXR
 * loader, `XR_EXT_hand_tracking`, Oboe for audio, and the unchanged core from `../Core`. After
 * Phosphene's Quest app (Quest/src/main.cpp there): the OpenXR session, the swapchains, the point
 * renderer, the font, the hands and the audio stream are its; the player and the panel are new.
 *
 * **Three threads.**
 *  - *Audio* (Oboe): `PiecePlayer::process` runs `Engine::process` and the play/stop fade. It never
 *    allocates, never locks and never blocks: while the composer thread holds the player it writes
 *    silence and returns. It publishes the position and the level for the panel.
 *  - *Composer*: composes a whole piece (Composer.h) and loads it into the engine before the audio
 *    stream starts; "next piece" composes the next one here and swaps it in behind a fade. The engine
 *    allocates when it loads, so the load happens on this thread while the audio thread writes silence.
 *  - *Render* (the glue thread): OpenXR frame loop, hands, gestures, the picture.
 *
 * **Performer, not editor.** A head-locked panel of dots shows the piece, its section, the root and
 * the scale, the time, the level and the two hand controls, and beside it the orrery of the Rack
 * page: the rows as orbits around the root. The hands do the rest:
 *
 * | Gesture | Effect |
 * |---|---|
 * | left pinch | play / stop (a 15 ms fade, the music pauses where it is) |
 * | right pinch | next piece: composed from the next seed, swapped in behind a fade |
 * | left hand height | the rows' filters (perform.filter), two octaves either way, mid height = as composed |
 * | right hand height | the echo throw (perform.throw), from mid height up |
 *
 * A hand only moves its control while it is *not* pinching, and both are smoothed with a 0.15 s
 * one-pole: a hand at mid height plays exactly what the composer wrote, and nothing ever jumps.
 *
 * **`eph.cfg`** in `<externalDataPath>` (`/sdcard/Android/data/com.reneweller.ephemeris.quest/files`):
 * @code
 *   mute=1              start silent (the test rule; the engine still runs)
 *   seed=2026           the first piece's seed; the next piece takes the next seed
 *   minutes=12          length of a piece
 *   style=Cosmic        Cosmic, Doom, Melodic, Modern or Drift
 *   quality=quest       quest (default here: 3 singers per choir key) or desktop (6)
 *   set=compose.key=D;perform.throw=0     any knobs, repeatable
 * @endcode
 */

#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "eph/Engine.h"
#include "eph/compose/Composer.h"
#include "eph/compose/Form.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Ephemeris", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Ephemeris", __VA_ARGS__)

using namespace eph;

namespace {

// ---------------------------------------------------------------- small math

/** @brief Column-major 4x4 matrix, the layout GL wants. */
struct Mat4 { float m[16]; };
/** @brief A point or direction. */
struct Vec3 { float x, y, z; };

Mat4 identity() { Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            r.m[c * 4 + rr] = a.m[0 * 4 + rr] * b.m[c * 4 + 0] + a.m[1 * 4 + rr] * b.m[c * 4 + 1]
                            + a.m[2 * 4 + rr] * b.m[c * 4 + 2] + a.m[3 * 4 + rr] * b.m[c * 4 + 3];
    return r;
}

/** @brief Asymmetric projection from OpenXR's four half-angles. */
Mat4 projectionFromFov(const XrFovf& fov, float nearZ, float farZ)
{
    const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
    const float u = std::tan(fov.angleUp), d = std::tan(fov.angleDown);
    const float w = r - l, h = u - d;
    Mat4 p{};
    p.m[0] = 2.0f / w;  p.m[8] = (r + l) / w;
    p.m[5] = 2.0f / h;  p.m[9] = (u + d) / h;
    p.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    p.m[11] = -1.0f;
    p.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return p;
}

Mat4 rotationFromQuat(const XrQuaternionf& q)
{
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 r = identity();
    r.m[0] = 1 - 2 * (y * y + z * z); r.m[4] = 2 * (x * y - z * w);     r.m[8] = 2 * (x * z + y * w);
    r.m[1] = 2 * (x * y + z * w);     r.m[5] = 1 - 2 * (x * x + z * z); r.m[9] = 2 * (y * z - x * w);
    r.m[2] = 2 * (x * z - y * w);     r.m[6] = 2 * (y * z + x * w);     r.m[10] = 1 - 2 * (x * x + y * y);
    return r;
}

Vec3 rotate(const XrQuaternionf& q, Vec3 v)
{
    const Mat4 r = rotationFromQuat(q);
    return { r.m[0] * v.x + r.m[4] * v.y + r.m[8] * v.z,
             r.m[1] * v.x + r.m[5] * v.y + r.m[9] * v.z,
             r.m[2] * v.x + r.m[6] * v.y + r.m[10] * v.z };
}

/** @brief The view matrix of an eye pose: the inverse of the pose. */
Mat4 viewFromPose(const XrPosef& pose)
{
    const Mat4 r = rotationFromQuat(pose.orientation);
    Mat4 rt = identity();
    for (int c = 0; c < 3; ++c) for (int rr = 0; rr < 3; ++rr) rt.m[c * 4 + rr] = r.m[rr * 4 + c];
    Mat4 t = identity();
    t.m[12] = -pose.position.x; t.m[13] = -pose.position.y; t.m[14] = -pose.position.z;
    return multiply(rt, t);
}

float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// ---------------------------------------------------------------- 5x7 point font

/**
 * @brief One glyph as seven rows of five bits (bit 4 = left column), or the blank for the unknown.
 *
 * Text on the panel is drawn as points, like everything else: no texture, no atlas, one draw call
 * for the whole picture. (Table taken from Noctuary's Quest app, where it was measured legible at
 * arm's length; extended by '%', '#' and '*'.)
 */
const unsigned char* glyph(char c)
{
    static const unsigned char kFont[][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // A B C
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // D E F
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // G H I
        {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}, {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // J K L
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // M N O
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // P Q R
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // S T U
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // V W X
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},                                        // Y Z
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 0 1 2
        {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 3 4 5
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 6 7 8
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},                                                                              // 9
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // space - .
        {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, // : / <
        {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // > + =
        {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, {0x0A,0x1F,0x0A,0x0A,0x0A,0x1F,0x0A}, {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00}, // % # *
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00},                                                                              // unknown
    };
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return kFont[c - 'A'];
    if (c >= '0' && c <= '9') return kFont[26 + (c - '0')];
    switch (c) {
    case ' ': return kFont[36]; case '-': return kFont[37]; case '.': return kFont[38];
    case ':': return kFont[39]; case '/': return kFont[40]; case '<': return kFont[41];
    case '>': return kFont[42]; case '+': return kFont[43]; case '=': return kFont[44];
    case '%': return kFont[45]; case '#': return kFont[46]; case '*': return kFont[47];
    default: break;
    }
    return kFont[48];
}


// ---------------------------------------------------------------- config

/** @brief What `eph.cfg` can say. */
struct Config {
    bool mute = false;              ///< start silent (test rule); the engine still runs
    uint64_t seed = 1;              ///< the first piece's seed
    double minutes = 12.0;          ///< length of a piece
    int singers = 3;                ///< singers per choir key: 3 at the quality level `quest` (default here), 6 at `desktop`
    std::string sets;               ///< knob assignments, "key=value" separated by ';' or newlines
};

/** @brief Reads `<dir>/eph.cfg`; every key is optional. */
Config readConfig(const char* dir)
{
    Config c;
    if (dir == nullptr) return c;
    const std::string path = std::string(dir) + "/eph.cfg";
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) { LOGI("no config at %s", path.c_str()); return c; }
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "mute") c.mute = v != "0";
        else if (k == "seed") c.seed = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "minutes") c.minutes = std::max(4.0, std::atof(v.c_str()));
        else if (k == "quality") c.singers = v == "desktop" ? 6 : 3;
        else if (k == "style") { c.sets += "compose.style=" + v; c.sets += ";"; }
        else if (k == "set") { c.sets += v; c.sets += ";"; }
        else LOGE("eph.cfg: unknown key %s", k.c_str());
    }
    std::fclose(f);
    LOGI("config: mute %d, seed %llu, %.1f min", c.mute ? 1 : 0, static_cast<unsigned long long>(c.seed), c.minutes);
    return c;
}

// ---------------------------------------------------------------- the piece player

/**
 * @brief Engine and composer, with a play/stop fade and a swap to the next piece.
 *
 * **Handover.** `state_` is the only synchronisation between the audio thread and the composer thread.
 * The audio thread takes it from Idle to Processing with one compare-and-exchange and puts it back; if
 * it cannot (the composer holds it for a load), it writes silence and returns. The composer waits for
 * Idle and takes it to Blocked. The audio thread therefore never blocks and never allocates.
 *
 * **What the panel reads.** Never the engine: its score and tempo map change when a piece is loaded.
 * The audio thread publishes beat, seconds and level as atomics after every block, and the composer
 * publishes the score as an immutable shared copy under a mutex the render thread takes for a pointer copy.
 */
class PiecePlayer {
public:
    /** @brief Composes the first piece and loads it. Composer thread, before the audio stream starts. */
    void prepare(int sampleRate, int block, const Config& cfg)
    {
        muted_ = cfg.mute;
        minutes_ = cfg.minutes;
        seed_ = cfg.seed;
        if (!cfg.sets.empty()) {
            std::string err;
            if (!engine_.params().parseText(cfg.sets, &err)) LOGE("eph.cfg set: %s", err.c_str());
        }
        gainCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sampleRate)));   // 15 ms
        levelCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.3 * sampleRate)));    // 300 ms
        engine_.prepare(sampleRate, block);
        engine_.setTapeSingers(cfg.singers);
        auto score = std::make_shared<Score>(composePiece(engine_.params(), seed_, minutes_));
        engine_.load(*score);
        publish(score);
        ready_.store(true, std::memory_order_release);
        LOGI("piece ready: seed %llu, %.0f s", static_cast<unsigned long long>(seed_), score->tempo.secondsAt(score->lengthBeats));
    }

    /** @brief Renders one block. Audio thread only. */
    void process(float* L, float* R, int n)
    {
        int expected = kIdle;
        if (!ready_.load(std::memory_order_acquire) || !state_.compare_exchange_strong(expected, kProcessing)) {
            std::fill(L, L + n, 0.0f);
            std::fill(R, R + n, 0.0f);
            return;
        }
        const float target = playing_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        if (target <= 0.0f && gain_ < 1.0e-4f) {
            // Faded out: the music waits where it is instead of running on silently.
            gain_ = 0.0f;
            std::fill(L, L + n, 0.0f);
            std::fill(R, R + n, 0.0f);
        } else {
            engine_.process(L, R, n);
            const float out = muted_ ? 0.0f : 1.0f;
            float level = level_.load(std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) {
                gain_ += (target - gain_) * gainCoef_;   // one pole: no step, no click
                level += (0.5f * (L[i] * L[i] + R[i] * R[i]) * gain_ * gain_ - level) * levelCoef_;
                L[i] *= gain_ * out;
                R[i] *= gain_ * out;
            }
            level_.store(level, std::memory_order_relaxed);
            beat_.store(engine_.beat(), std::memory_order_relaxed);
            seconds_.store(engine_.seconds(), std::memory_order_relaxed);
            if (engine_.seconds() > engine_.lengthSeconds() + 8.0) nextRequest_.store(true, std::memory_order_relaxed);
        }
        state_.store(kIdle, std::memory_order_release);
    }

    /** @brief Play or stop (any thread; takes effect over the fade). */
    void setPlaying(bool on) { playing_.store(on, std::memory_order_relaxed); }
    /** @brief Whether play is on. */
    bool playing() const { return playing_.load(std::memory_order_relaxed); }
    /** @brief Asks for the next piece (any thread). */
    void requestNextPiece() { nextRequest_.store(true, std::memory_order_relaxed); }
    /** @brief One round of composer work: a pending next piece. Composer thread. */
    void pump() { if (nextRequest_.exchange(false, std::memory_order_relaxed)) nextPiece(); }

    /** @brief Where the audio thread is, in beats and seconds (render thread). */
    double beat() const { return beat_.load(std::memory_order_relaxed); }
    double seconds() const { return seconds_.load(std::memory_order_relaxed); }   ///< @copydoc beat
    /** @brief The output's mean square, smoothed over 300 ms, as dBFS (render thread). */
    float levelDb() const { const float l = level_.load(std::memory_order_relaxed); return l > 1.0e-10f ? 10.0f * std::log10(l) : -100.0f; }
    /** @brief The piece that plays (render thread): an immutable copy. */
    std::shared_ptr<const Score> score() const { std::lock_guard<std::mutex> lock(scoreMutex_); return score_; }
    /** @brief Which piece plays, from 1. */
    int piece() const { return piece_.load(std::memory_order_relaxed); }
    /** @brief The knobs, for the hand controls (atomics: any thread). */
    ParamStore& params() { return engine_.params(); }
    /** @brief Whether prepare() has finished. */
    bool ready() const { return ready_.load(std::memory_order_acquire); }
    /** @brief Whether the output is muted by the config. */
    bool muted() const { return muted_; }

private:
    enum : int { kIdle = 0, kProcessing, kBlocked };

    /** @brief Composes the piece after this one and swaps it in behind a fade. Composer thread. */
    void nextPiece()
    {
        const bool wasPlaying = playing();
        setPlaying(false);
        const int next = piece_.load(std::memory_order_relaxed) + 1;
        // Composed while the fade runs and the old piece still waits: nothing is taken from the audio thread yet.
        auto score = std::make_shared<Score>(composePiece(engine_.params(), seed_ + static_cast<uint64_t>(next - 1), minutes_));
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        int expected = kIdle;
        while (!state_.compare_exchange_weak(expected, kBlocked)) {
            expected = kIdle;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        engine_.load(*score);      // allocates: here, while the audio thread writes silence
        gain_ = 0.0f;
        beat_.store(0.0, std::memory_order_relaxed);
        seconds_.store(0.0, std::memory_order_relaxed);
        state_.store(kIdle, std::memory_order_release);
        piece_.store(next, std::memory_order_relaxed);
        publish(score);
        setPlaying(wasPlaying);
        LOGI("piece %d: seed %llu", next, static_cast<unsigned long long>(seed_ + static_cast<uint64_t>(next - 1)));
    }

    void publish(std::shared_ptr<const Score> s) { std::lock_guard<std::mutex> lock(scoreMutex_); score_ = std::move(s); }

    Engine engine_;
    uint64_t seed_ = 1;
    double minutes_ = 12.0;
    std::atomic<int> state_{ kIdle };
    std::atomic<bool> ready_{ false }, playing_{ false }, nextRequest_{ false };
    std::atomic<int> piece_{ 1 };
    std::atomic<double> beat_{ 0.0 }, seconds_{ 0.0 };
    std::atomic<float> level_{ 0.0f };
    bool muted_ = false;
    float gain_ = 0.0f, gainCoef_ = 0.002f, levelCoef_ = 0.0001f;
    mutable std::mutex scoreMutex_;
    std::shared_ptr<const Score> score_;
};

// ---------------------------------------------------------------- audio

/** @brief Oboe output stream: a low-latency float stream straight into PiecePlayer::process. */
class Audio : public oboe::AudioStreamDataCallback {
public:
    explicit Audio(PiecePlayer& p) : player_(p) {}

    bool open()
    {
        oboe::AudioStreamBuilder b;
        b.setDirection(oboe::Direction::Output)
         ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
         ->setSharingMode(oboe::SharingMode::Exclusive)
         ->setFormat(oboe::AudioFormat::Float)
         ->setChannelCount(2)
         ->setSampleRate(48000)
         ->setDataCallback(this);
        if (b.openStream(stream_) != oboe::Result::OK) { LOGE("Oboe: cannot open stream"); return false; }
        sampleRate_ = stream_->getSampleRate();
        burst_ = stream_->getFramesPerBurst();
        stream_->setBufferSizeInFrames(burst_ * 2);
        bufL_.assign(4096, 0.0f);
        bufR_.assign(4096, 0.0f);
        LOGI("Oboe: %d Hz, burst %d", sampleRate_, burst_);
        return true;
    }
    bool start() { return stream_ && stream_->requestStart() == oboe::Result::OK; }
    void stop() { if (stream_) { stream_->requestStop(); stream_->close(); stream_.reset(); } }
    int sampleRate() const { return sampleRate_; }
    int burst() const { return burst_; }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void* data, int32_t frames) override
    {
        float* out = static_cast<float*>(data);
        int done = 0;
        while (done < frames) {
            const int n = std::min(frames - done, static_cast<int32_t>(bufL_.size()));
            player_.process(bufL_.data(), bufR_.data(), n);
            for (int i = 0; i < n; ++i) {
                out[(done + i) * 2] = bufL_[static_cast<size_t>(i)];
                out[(done + i) * 2 + 1] = bufR_[static_cast<size_t>(i)];
            }
            done += n;
        }
        return oboe::DataCallbackResult::Continue;
    }

private:
    PiecePlayer& player_;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::vector<float> bufL_, bufR_;
    int sampleRate_ = 48000, burst_ = 256;
};

// ---------------------------------------------------------------- GL scene

const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
layout(location = 2) in float aSize;
uniform mat4 uVP;
out vec4 vCol;
void main() {
    vec4 p = uVP * vec4(aPos, 1.0);
    gl_Position = p;
    gl_PointSize = aSize / max(p.w, 0.2);
    vCol = aCol;
})";

const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec4 vCol;
out vec4 o;
void main() {
    vec2 d = gl_PointCoord - vec2(0.5);
    float r = length(d) * 2.0;
    float a = smoothstep(1.0, 0.15, r);
    o = vec4(vCol.rgb * a * vCol.a, 1.0);
})";

/** @brief One soft round point: the only primitive the scene has. */
struct Point { float x, y, z; float r, g, b, a; float size; };

/** @brief A head-locked plane to lay text out on: origin plus a right and an up axis. */
struct Panel { Vec3 origin, right, up; };

GLuint compile(GLenum type, const char* src)
{
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); LOGE("shader: %s", log); }
    return s;
}

/**
 * @brief Everything visible, as additive points.
 *
 * The Kaleidoscope rules apply (docs/PLAN.md 8.2): nothing about the camera moves with the audio,
 * and every brightness is a continuous function of the bar position -- no step, no flash.
 */
class Scene {
public:
    /** @brief Display pixels per radian on the Quest 2's render target (about 1830 px over 90 deg). */
    static constexpr float kPixelsPerRadian = 1150.0f;

    bool init()
    {
        program_ = glCreateProgram();
        glAttachShader(program_, compile(GL_VERTEX_SHADER, kVertexShader));
        glAttachShader(program_, compile(GL_FRAGMENT_SHADER, kFragmentShader));
        glLinkProgram(program_);
        GLint ok = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) { LOGE("program link failed"); return false; }
        uVP_ = glGetUniformLocation(program_, "uVP");
        glGenBuffers(1, &vbo_);
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Point), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Point), reinterpret_cast<void*>(12));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Point), reinterpret_cast<void*>(28));
        glBindVertexArray(0);
        return true;
    }

    void begin() { points_.clear(); }

    /** @brief One point in world space. */
    void add(float x, float y, float z, float r, float g, float b, float a, float size)
    {
        points_.push_back({ x, y, z, r, g, b, a, size });
    }

    /** @brief One point on a panel, at panel coordinates @p u (right) and @p v (up). */
    void addOn(const Panel& p, float u, float v, float r, float g, float b, float a, float size)
    {
        add(p.origin.x + p.right.x * u + p.up.x * v,
            p.origin.y + p.right.y * u + p.up.y * v,
            p.origin.z + p.right.z * u + p.up.z * v, r, g, b, a, size);
    }

    /**
     * @brief Text on a panel, one point per lit pixel of the 5x7 font; @p cell is the pixel pitch.
     *
     * The point size is `cell * kPixelsPerRadian / w`: a cell of `cell` metres at `w` metres
     * subtends `cell / w` radians, and the Quest 2 renders about 1150 pixels per radian (roughly
     * 1830 pixels over 90 degrees per eye), so the dots of a glyph just touch at any distance.
     */
    void addText(const Panel& p, float u, float v, float cell, const char* text,
                 float r, float g, float b, float a)
    {
        for (const char* c = text; *c; ++c) {
            const unsigned char* gl = glyph(*c);
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (gl[row] & (0x10 >> col))
                        addOn(p, u + static_cast<float>(col) * cell, v - static_cast<float>(row) * cell,
                              r, g, b, a, cell * kPixelsPerRadian);
            u += 6.0f * cell;
        }
    }

    void upload()
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(points_.size() * sizeof(Point)), points_.data(), GL_DYNAMIC_DRAW);
    }

    void draw(const Mat4& vp, int width, int height)
    {
        glViewport(0, 0, width, height);
        glClearColor(0.015f, 0.012f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);            // additive: points add light, they never occlude
        glUseProgram(program_);
        glUniformMatrix4fv(uVP_, 1, GL_FALSE, vp.m);
        glBindVertexArray(vao_);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(points_.size()));
        glBindVertexArray(0);
    }

private:
    GLuint program_ = 0, vbo_ = 0, vao_ = 0;
    GLint uVP_ = -1;
    std::vector<Point> points_;
};

// ---------------------------------------------------------------- hands

/**
 * @brief Hand state and the mapping to the two macros and the two buttons.
 *
 * Height is measured against the head, not the floor: `(palm.y - (head.y - 1.0)) / 0.8`, so the
 * mapping is the same whether the runtime gave us a STAGE space (floor at y = 0) or a LOCAL one
 * (origin wherever the session started), and it is the same for a tall and a short player.
 *
 * The pinch is the thumb tip to index tip distance with a Schmitt trigger (closed under 22 mm, open
 * over 38 mm), so a hand held near the threshold does not rattle between the two states.
 */
class Hands {
public:
    /** @brief One hand's measurement of this frame. */
    struct Hand {
        bool valid = false;
        XrVector3f palm{};
        float pinch = 0.0f;     ///< 0 open .. 1 closed
        bool closed = false;    ///< after the Schmitt trigger
        float height = 0.5f;    ///< 0..1 against the head
        float macro = 0.5f;     ///< the smoothed value the macro follows
        bool everSeen = false;  ///< this hand has been tracked at least once
    };

    /** @brief Feeds one hand; @p headY is the head's height in the same space. */
    void setHand(int h, const XrVector3f& palm, float thumbToIndex, float headY)
    {
        Hand& s = hand_[h];
        s.valid = true;
        s.everSeen = true;
        s.palm = palm;
        s.pinch = clamp01(1.0f - (thumbToIndex - 0.015f) / 0.035f);
        if (thumbToIndex < 0.022f) s.closed = true;
        else if (thumbToIndex > 0.038f) s.closed = false;
        s.height = clamp01((palm.y - (headY - 1.0f)) / 0.8f);
    }
    /** @brief Marks a hand as not tracked this frame (its macro then holds its value). */
    void lost(int h) { hand_[h].valid = false; hand_[h].closed = false; }

    /**
     * @brief Advances the smoothing and reports the two rising pinch edges.
     * @param dt         seconds since the last frame
     * @param leftPinch  receives true on the frame the left hand closes
     * @param rightPinch receives true on the frame the right hand closes
     *
     * A macro follows its hand only while that hand is open: the pinch that starts a track must not
     * also drag the gain with it.
     */
    void update(double dt, bool& leftPinch, bool& rightPinch)
    {
        // 0.15 s one pole. dt is capped at 0.1 s so that a long frame -- the first one after the
        // session resumes, say -- cannot make the coefficient 1 and snap the macro to the hand.
        const float k = 1.0f - std::exp(-static_cast<float>(std::min(dt, 0.1)) / 0.15f);
        for (int h = 0; h < 2; ++h) {
            Hand& s = hand_[h];
            if (s.valid && !s.closed) s.macro += (s.height - s.macro) * k;
        }
        leftPinch = hand_[0].closed && !wasClosed_[0];
        rightPinch = hand_[1].closed && !wasClosed_[1];
        wasClosed_[0] = hand_[0].closed;
        wasClosed_[1] = hand_[1].closed;
    }

    const Hand& hand(int h) const { return hand_[h]; }

private:
    Hand hand_[2];
    bool wasClosed_[2] = { false, false };
};

// ---------------------------------------------------------------- the app

/** @brief One eye's swapchain and the framebuffer it is rendered through. */
struct SwapchainTarget {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int width = 0, height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    GLuint fbo = 0, depth = 0;
};

class App {
public:
    explicit App(android_app* app) : app_(app) {}

    bool init()
    {
        dataDir_ = app_->activity->externalDataPath ? app_->activity->externalDataPath : "";
        config_ = readConfig(dataDir_.c_str());
        if (!initLoader()) return false;
        if (!initInstance()) return false;
        if (!initEgl()) return false;
        if (!initSession()) return false;
        if (!initHands()) LOGE("hand tracking unavailable: the macros will not move");
        if (!scene_.init()) return false;
        // The stream is opened first so the engine is prepared for the rate the device really gives.
        if (!audio_.open()) return false;
        composerThread_ = std::thread([this] { composerLoop(); });
        return true;
    }

    void run()
    {
        while (!app_->destroyRequested) {
            int events;
            android_poll_source* source;
            const int timeout = (sessionRunning_ || app_->window == nullptr) ? 0 : -1;
            while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
                if (source) source->process(app_, source);
                if (app_->destroyRequested) break;
            }
            pollXrEvents();
            if (quit_) break;
            if (sessionRunning_) frame();
        }
    }

    void shutdown()
    {
        stopComposer_.store(true);
        if (composerThread_.joinable()) composerThread_.join();
        audio_.stop();
        for (SwapchainTarget& t : targets_) if (t.swapchain != XR_NULL_HANDLE) xrDestroySwapchain(t.swapchain);
        for (int h = 0; h < 2; ++h)
            if (handTracker_[h] != XR_NULL_HANDLE && pfnDestroyHandTracker_) pfnDestroyHandTracker_(handTracker_[h]);
        if (viewSpace_ != XR_NULL_HANDLE) xrDestroySpace(viewSpace_);
        if (stageSpace_ != XR_NULL_HANDLE) xrDestroySpace(stageSpace_);
        if (session_ != XR_NULL_HANDLE) xrDestroySession(session_);
        if (instance_ != XR_NULL_HANDLE) xrDestroyInstance(instance_);
    }

private:
    // ------------------------------------------------ the composer thread

    /**
     * @brief Composes and loads the first piece, starts the stream, then waits for "next piece".
     *
     * A piece is composed whole before it plays (Composer.h): a few seconds on the headset's small cores,
     * during which the panel says COMPOSING and the audio stream is not yet started.
     */
    void composerLoop()
    {
        player_.prepare(audio_.sampleRate(), audio_.burst() * 2, config_);
        const ParamStore& p = player_.params();
        filterId_ = p.id(Module::Perform, 0, perform::Filter);
        throwId_ = p.id(Module::Perform, 0, perform::Throw);
        if (!audio_.start()) LOGE("Oboe: cannot start");
        else LOGI("audio started%s", config_.mute ? " (muted)" : "");
        while (!stopComposer_.load()) {
            player_.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // ------------------------------------------------ OpenXR setup

    bool initLoader()
    {
        PFN_xrInitializeLoaderKHR initLoader = nullptr;
        xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", reinterpret_cast<PFN_xrVoidFunction*>(&initLoader));
        if (initLoader == nullptr) { LOGE("no xrInitializeLoaderKHR"); return false; }
        XrLoaderInitInfoAndroidKHR li{ XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
        li.applicationVM = app_->activity->vm;
        li.applicationContext = app_->activity->clazz;
        return XR_SUCCEEDED(initLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&li)));
    }

    bool initInstance()
    {
        const char* exts[] = { XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                               XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
                               XR_EXT_HAND_TRACKING_EXTENSION_NAME };
        XrInstanceCreateInfoAndroidKHR android{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
        android.applicationVM = app_->activity->vm;
        android.applicationActivity = app_->activity->clazz;
        XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO, &android };
        std::strncpy(ci.applicationInfo.applicationName, "Ephemeris", XR_MAX_APPLICATION_NAME_SIZE - 1);
        ci.applicationInfo.applicationVersion = 1;
        std::strncpy(ci.applicationInfo.engineName, "EphemerisCore", XR_MAX_ENGINE_NAME_SIZE - 1);
        ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        ci.enabledExtensionCount = 3;
        ci.enabledExtensionNames = exts;
        if (XR_FAILED(xrCreateInstance(&ci, &instance_))) { LOGE("xrCreateInstance failed"); return false; }

        XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (XR_FAILED(xrGetSystem(instance_, &sgi, &system_))) { LOGE("xrGetSystem failed"); return false; }
        XrSystemHandTrackingPropertiesEXT ht{ XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
        XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES, &ht };
        xrGetSystemProperties(instance_, system_, &sp);
        handsSupported_ = ht.supportsHandTracking == XR_TRUE;
        LOGI("system: %s, hand tracking %d", sp.systemName, handsSupported_ ? 1 : 0);
        return true;
    }

    bool initEgl()
    {
        PFN_xrGetOpenGLESGraphicsRequirementsKHR getReq = nullptr;
        xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&getReq));
        XrGraphicsRequirementsOpenGLESKHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
        if (getReq == nullptr || XR_FAILED(getReq(instance_, system_, &req))) { LOGE("GLES requirements failed"); return false; }

        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (!eglInitialize(display_, &major, &minor)) { LOGE("eglInitialize failed"); return false; }
        const EGLint attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_DEPTH_SIZE, 0, EGL_NONE };
        EGLint count = 0;
        if (!eglChooseConfig(display_, attribs, &eglConfig_, 1, &count) || count == 0) { LOGE("eglChooseConfig failed"); return false; }
        const EGLint pbuf[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
        surface_ = eglCreatePbufferSurface(display_, eglConfig_, pbuf);
        const EGLint ctx[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        context_ = eglCreateContext(display_, eglConfig_, EGL_NO_CONTEXT, ctx);
        if (context_ == EGL_NO_CONTEXT || !eglMakeCurrent(display_, surface_, surface_, context_)) { LOGE("EGL context failed"); return false; }
        LOGI("EGL %d.%d, GL %s", major, minor, glGetString(GL_VERSION));
        return true;
    }

    bool initSession()
    {
        XrGraphicsBindingOpenGLESAndroidKHR gb{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
        gb.display = display_; gb.config = eglConfig_; gb.context = context_;
        XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO, &gb };
        sci.systemId = system_;
        if (XR_FAILED(xrCreateSession(instance_, &sci, &session_))) { LOGE("xrCreateSession failed"); return false; }

        XrReferenceSpaceCreateInfo rs{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        rs.poseInReferenceSpace.orientation.w = 1.0f;
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        if (XR_FAILED(xrCreateReferenceSpace(session_, &rs, &stageSpace_))) {
            rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            if (XR_FAILED(xrCreateReferenceSpace(session_, &rs, &stageSpace_))) { LOGE("no reference space"); return false; }
        }
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        xrCreateReferenceSpace(session_, &rs, &viewSpace_);

        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        std::vector<XrViewConfigurationView> cfg(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, cfg.data());
        views_.assign(viewCount, { XR_TYPE_VIEW });

        uint32_t fmtCount = 0;
        xrEnumerateSwapchainFormats(session_, 0, &fmtCount, nullptr);
        std::vector<int64_t> formats(fmtCount);
        xrEnumerateSwapchainFormats(session_, fmtCount, &fmtCount, formats.data());
        int64_t format = formats.empty() ? GL_RGBA8 : formats[0];
        for (int64_t f : formats) if (f == GL_SRGB8_ALPHA8) { format = f; break; }

        targets_.resize(viewCount);
        for (uint32_t v = 0; v < viewCount; ++v) {
            SwapchainTarget& t = targets_[v];
            XrSwapchainCreateInfo sc{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
            sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            sc.format = format;
            sc.sampleCount = 1;
            sc.width = cfg[v].recommendedImageRectWidth;
            sc.height = cfg[v].recommendedImageRectHeight;
            sc.faceCount = 1; sc.arraySize = 1; sc.mipCount = 1;
            if (XR_FAILED(xrCreateSwapchain(session_, &sc, &t.swapchain))) { LOGE("xrCreateSwapchain failed"); return false; }
            t.width = static_cast<int>(sc.width);
            t.height = static_cast<int>(sc.height);
            uint32_t imgCount = 0;
            xrEnumerateSwapchainImages(t.swapchain, 0, &imgCount, nullptr);
            t.images.assign(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
            xrEnumerateSwapchainImages(t.swapchain, imgCount, &imgCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(t.images.data()));
            glGenFramebuffers(1, &t.fbo);
            glGenRenderbuffers(1, &t.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, t.width, t.height);
            LOGI("view %u: %dx%d, %u images", v, t.width, t.height, imgCount);
        }
        return true;
    }

    bool initHands()
    {
        if (!handsSupported_) return false;
        xrGetInstanceProcAddr(instance_, "xrCreateHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateHandTracker_));
        xrGetInstanceProcAddr(instance_, "xrLocateHandJointsEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnLocateHandJoints_));
        xrGetInstanceProcAddr(instance_, "xrDestroyHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyHandTracker_));
        if (!pfnCreateHandTracker_ || !pfnLocateHandJoints_) return false;
        for (int h = 0; h < 2; ++h) {
            XrHandTrackerCreateInfoEXT hci{ XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
            hci.hand = (h == 0) ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
            hci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
            if (XR_FAILED(pfnCreateHandTracker_(session_, &hci, &handTracker_[h]))) return false;
        }
        return true;
    }

    void pollXrEvents()
    {
        XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
        while (xrPollEvent(instance_, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto* sc = reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
                sessionState_ = sc->state;
                switch (sessionState_) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (XR_SUCCEEDED(xrBeginSession(session_, &bi))) sessionRunning_ = true;
                    break;
                }
                case XR_SESSION_STATE_STOPPING:
                    xrEndSession(session_);
                    sessionRunning_ = false;
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    quit_ = true;
                    break;
                default: break;
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                quit_ = true;
            }
            ev = { XR_TYPE_EVENT_DATA_BUFFER };
        }
    }

    // ------------------------------------------------ per frame

    void updateHead(XrTime time)
    {
        headValid_ = false;
        if (viewSpace_ == XR_NULL_HANDLE) return;
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_FAILED(xrLocateSpace(viewSpace_, stageSpace_, time, &loc))) return;
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((loc.locationFlags & need) != need) return;
        headPose_ = loc.pose;
        headValid_ = true;
    }

    void updateHands(XrTime time)
    {
        for (int h = 0; h < 2; ++h) {
            if (handTracker_[h] == XR_NULL_HANDLE) { hands_.lost(h); continue; }
            XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
            XrHandJointLocationsEXT locs{ XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
            locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locs.jointLocations = joints;
            XrHandJointsLocateInfoEXT li{ XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
            li.baseSpace = stageSpace_;
            li.time = time;
            if (XR_FAILED(pfnLocateHandJoints_(handTracker_[h], &li, &locs)) || !locs.isActive) { hands_.lost(h); continue; }
            const XrHandJointLocationEXT& palm = joints[XR_HAND_JOINT_PALM_EXT];
            const XrHandJointLocationEXT& thumb = joints[XR_HAND_JOINT_THUMB_TIP_EXT];
            const XrHandJointLocationEXT& index = joints[XR_HAND_JOINT_INDEX_TIP_EXT];
            const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if ((palm.locationFlags & need) != need) { hands_.lost(h); continue; }
            const float dx = thumb.pose.position.x - index.pose.position.x;
            const float dy = thumb.pose.position.y - index.pose.position.y;
            const float dz = thumb.pose.position.z - index.pose.position.z;
            hands_.setHand(h, palm.pose.position, std::sqrt(dx * dx + dy * dy + dz * dz),
                           headValid_ ? headPose_.position.y : 1.6f);
        }
    }

    /**
     * @brief Turns the gestures into the perform controls (Params.h, perform).
     *
     * Left height is the hand on the rows' filters: perform.filter over its whole range, so mid height is
     * 0 octaves, the composed sound. Right height is the echo throw: nothing below mid height, all of it
     * with the hand raised. The composer's own gestures ride on top as offsets, so they keep working.
     */
    void applyMacros()
    {
        if (!player_.ready() || filterId_ < 0) return;
        ParamStore& p = player_.params();
        // A hand that has never been tracked writes nothing: without hand tracking the controls keep
        // whatever eph.cfg set them to.
        const Hands::Hand& left = hands_.hand(0);
        const Hands::Hand& right = hands_.hand(1);
        if (left.everSeen) p.setNormalised(filterId_, left.macro);
        if (right.everSeen) p.set(throwId_, clamp01(2.0f * (right.macro - 0.5f)));
    }

    /**
     * @brief Geometry of the panel.
     *
     * A glyph is 7 cells high and a character 6 cells wide, so at kCell and kPanelDistance a line of
     * 20 characters spans 0.50 m -- about 29 degrees -- and a glyph stands 1.7 degrees tall, roughly
     * print at reading distance. Every line drawn below stays inside 20 characters. These are design
     * values and untuned: nobody has put the headset on yet.
     */
    static constexpr float kCell = 0.0042f;         ///< pitch of one font pixel, metres
    static constexpr float kRow = 0.040f;           ///< distance between text rows, metres
    static constexpr float kPanelDistance = 1.0f;   ///< how far in front of the eyes the panel sits

    /** @brief The head-locked panel: in front of the eyes, following the yaw only. */
    Panel headPanel() const
    {
        Panel p;
        const Vec3 fwd = rotate(headPose_.orientation, { 0.0f, 0.0f, -1.0f });
        const float len = std::fmax(std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z), 1.0e-3f);
        const Vec3 f{ fwd.x / len, 0.0f, fwd.z / len };
        p.right = { -f.z, 0.0f, f.x };
        p.up = { 0.0f, 1.0f, 0.0f };
        // Shifted half a 16-character line to the left (the lines are left aligned and 13 to 20
        // characters long), so the block of text sits roughly centred in front of the eyes.
        const float half = 0.5f * 16.0f * 6.0f * kCell;
        p.origin = { headPose_.position.x + f.x * kPanelDistance - p.right.x * half,
                     headPose_.position.y + 0.12f,
                     headPose_.position.z + f.z * kPanelDistance - p.right.z * half };
        return p;
    }

    /** @brief A row of the piece at one beat, for the orrery (as EditorOrrery.cpp computes it). */
    struct Orbit { bool known = false, running = false, transposer = false; int length = 16; double divBeats = 0.25, start = 0.0, cycle = 0.0; };

    /** @brief The rows of @p s at @p beat: shapes, start and stop, the place in the cycle. */
    static void orbitsAt(const Score& s, double beat, Orbit (&rows)[kRows])
    {
        for (Orbit& o : rows) o = Orbit{};
        for (const RowShape& r : s.rowShapes) {
            if (r.from > beat || r.row < 0 || r.row >= kRows) continue;
            Orbit& o = rows[r.row];
            o.known = true; o.length = r.length; o.divBeats = r.divBeats; o.transposer = r.transposer;
        }
        for (const RackEvent& e : s.rack) {
            if (e.beat > beat) break;
            if (e.op != RackOp::Start && e.op != RackOp::Stop) continue;
            const int lo = e.row < 0 ? 0 : e.row, hi = e.row < 0 ? kRows - 1 : e.row;
            for (int i = lo; i <= hi && i < kRows; ++i) {
                rows[i].running = e.op == RackOp::Start;
                if (rows[i].running) rows[i].start = e.beat;
            }
        }
        for (Orbit& o : rows) {
            const double period = o.length * o.divBeats;
            if (o.known && o.running && period > 0.0) { const double t = (beat - o.start) / period; o.cycle = t - std::floor(t); }
        }
    }

    /** @brief The section at @p beat in English, from the marker the composer wrote (German, Form.cpp). */
    static const char* sectionAt(const Score& s, double beat)
    {
        const char* text = "";
        for (const Marker& m : s.markers) { if (m.beat > beat) break; text = m.text.c_str(); }
        struct Name { const char* de; const char* en; };
        static const Name names[] = { { "Atmo", "ATMOSPHERE" }, { "Einsatz", "ENTRY" }, { "Aufbau", "BUILD" }, { "Lead", "LEAD" },
                                      { "Hoehepunkt", "PEAK" }, { "Abbau", "BREAKDOWN" }, { "Bruecke", "BRIDGE" }, { "Ausklang", "CODA" } };
        for (const Name& n : names) if (std::strncmp(text, n.de, std::strlen(n.de)) == 0) return n.en;
        return "";
    }

    /**
     * @brief The performer panel (PLAN 8.2) and the two hands: text on the left, the orrery on the right.
     *
     * The orrery is the Rack page's (EditorOrrery.h): every row that plays notes is a planet on an orbit,
     * one turn a cycle of the row, step 1 at the top; the sun is the transposer's root. Nothing flashes and
     * nothing steps: the planets move continuously, and a conjunction is a column of points that fades in.
     */
    void buildScene()
    {
        // The floor ring: a fixed horizon, never moved by the audio.
        for (int i = 0; i < 72; ++i) {
            const float a = static_cast<float>(i) / 72.0f * 6.2831853f;
            scene_.add(2.5f * std::sin(a), 0.02f, -2.5f * std::cos(a), 0.30f, 0.24f, 0.14f, 0.5f, 40.0f);
        }
        for (int h = 0; h < 2; ++h) {
            const Hands::Hand& s = hands_.hand(h);
            if (!s.valid) continue;
            const float t = s.pinch;
            scene_.add(s.palm.x, s.palm.y, s.palm.z, 0.95f, 0.75f - 0.4f * t, 0.35f + 0.4f * t, 1.0f, 130.0f);
        }
        if (!headValid_) return;

        const Panel p = headPanel();
        char line[64];
        if (!player_.ready()) {
            scene_.addText(p, 0.0f, 0.0f, kCell * 1.8f, "EPHEMERIS", 0.95f, 0.75f, 0.40f, 1.0f);
            scene_.addText(p, 0.0f, -0.08f, kCell, "COMPOSING", 0.6f, 0.7f, 0.9f, 0.8f);
            return;
        }
        const std::shared_ptr<const Score> sp = player_.score();
        if (!sp) return;
        const Score& s = *sp;
        const double beat = player_.beat();
        const ParamStore& par = player_.params();

        float row = 0.0f;
        auto text = [&](const char* t, float r, float g, float b, float a) {
            scene_.addText(p, 0.0f, row, kCell, t, r, g, b, a);
            row -= kRow;
        };
        static const char* const kKeys[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int styleId = par.id(Module::Compose, 0, compose::Style), scaleId = par.id(Module::Compose, 0, compose::Scale);
        const ParamDesc& sd = par.desc(styleId);
        const ParamDesc& cd = par.desc(scaleId);
        const int style = std::clamp(par.getInt(styleId), 0, static_cast<int>(sd.maxValue));
        const int scale = std::clamp(par.getInt(scaleId), 0, static_cast<int>(cd.maxValue));
        std::snprintf(line, sizeof(line), "PIECE %d  %s", player_.piece(), sd.choices != nullptr ? sd.choices[style] : "");
        text(line, 0.95f, 0.78f, 0.45f, 1.0f);
        text(sectionAt(s, beat), 0.95f, 0.78f, 0.45f, 0.9f);
        const int root = pitchClass(s.keyRoot + s.rootAt(beat));
        std::snprintf(line, sizeof(line), "%s %s", kKeys[root], cd.choices != nullptr ? cd.choices[scale] : "");
        text(line, 0.70f, 0.85f, 1.00f, 0.9f);
        const double now = player_.seconds(), total = s.tempo.secondsAt(s.lengthBeats);
        std::snprintf(line, sizeof(line), "%02d:%02d / %02d:%02d  %.0f BPM", static_cast<int>(now) / 60, static_cast<int>(now) % 60,
                      static_cast<int>(total) / 60, static_cast<int>(total) % 60, s.tempo.bpmAt(beat));
        text(line, 0.95f, 0.85f, 0.60f, 0.9f);
        const float db = player_.levelDb();
        std::snprintf(line, sizeof(line), "%+.1f DB  %s", static_cast<double>(std::max(db, -99.0f)),
                      player_.muted() ? "MUTED" : (player_.playing() ? "PLAY" : "STOP"));
        text(line, 0.95f, 0.85f, 0.60f, 0.9f);
        std::snprintf(line, sizeof(line), "FILTER %+.1f OCT", static_cast<double>(par.get(filterId_)));
        text(line, 0.60f, 0.75f, 0.95f, 0.75f);
        std::snprintf(line, sizeof(line), "THROW %.2f", static_cast<double>(par.get(throwId_)));
        text(line, 0.60f, 0.75f, 0.95f, 0.75f);

        row -= kRow * 0.3f;
        // Four beat lamps: a raised cosine of the distance to the beat, a continuous pulse.
        const double inBar = beat / kBeatsPerBar - std::floor(beat / kBeatsPerBar);
        const float phase = static_cast<float>(inBar) * 4.0f;
        for (int b = 0; b < 4; ++b) {
            float dist = phase - static_cast<float>(b);
            if (dist < 0.0f) dist += 4.0f;
            const float x = dist < 1.0f ? 0.5f + 0.5f * std::cos(3.14159265f * dist) : 0.0f;
            const float level = player_.playing() ? 0.22f + 0.78f * x : 0.18f;
            scene_.addOn(p, 0.026f * static_cast<float>(b), row, 1.0f, 0.55f + 0.35f * x, 0.25f, level, 150.0f);
        }

        // The orrery, right of the text.
        Orbit rows[kRows];
        orbitsAt(s, beat, rows);
        const float cx = 0.62f, cy = -0.13f, outer = 0.17f, sun = 0.028f;
        int order[kRows], n = 0;
        for (int r = 0; r < kRows; ++r) if (rows[r].known && !rows[r].transposer) order[n++] = r;
        int running = 0, onFirst = 0;
        static const float kColours[8][3] = { { 0.88f, 0.64f, 0.35f }, { 0.85f, 0.51f, 0.36f }, { 0.79f, 0.64f, 0.36f }, { 0.62f, 0.75f, 0.44f },
                                              { 0.44f, 0.72f, 0.68f }, { 0.44f, 0.56f, 0.72f }, { 0.65f, 0.54f, 0.76f }, { 0.77f, 0.48f, 0.61f } };
        for (int k = 0; k < n; ++k) {
            const Orbit& o = rows[order[k]];
            const float* col = kColours[order[k] & 7];
            const float radius = sun + (outer - sun) * static_cast<float>(k + 1) / static_cast<float>(std::max(1, n));
            const int dots = 24 + 6 * k;
            for (int i = 0; i < dots; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(dots);
                scene_.addOn(p, cx + radius * std::sin(a), cy + radius * std::cos(a), col[0], col[1], col[2], o.running ? 0.28f : 0.10f, 30.0f);
            }
            if (!o.running) continue;
            ++running;
            if (static_cast<int>(o.cycle * o.length) == 0) ++onFirst;
            const float a = 6.2831853f * static_cast<float>(o.cycle);
            scene_.addOn(p, cx + radius * std::sin(a), cy + radius * std::cos(a), col[0], col[1], col[2], 1.0f, 170.0f);
        }
        // A conjunction: running rows together on their first step, a column of light from the sun upwards.
        if (running >= 2 && onFirst >= 2) {
            const float strength = static_cast<float>(onFirst) / static_cast<float>(running);
            for (int i = 0; i < 16; ++i) {
                const float t = static_cast<float>(i) / 15.0f;
                scene_.addOn(p, cx, cy + sun + (outer - sun) * t, 1.0f, 0.8f, 0.45f, (0.3f + 0.6f * strength) * (1.0f - 0.7f * t), 70.0f);
            }
        }
        bool moving = false;
        for (const Orbit& o : rows) if (o.known && o.transposer && o.running) moving = true;
        scene_.addOn(p, cx, cy, 1.0f, 0.72f, 0.35f, moving ? 1.0f : 0.6f, 900.0f);
        scene_.addText(p, cx - 0.012f * static_cast<float>(std::strlen(kKeys[root])), cy - sun - 0.035f, kCell, kKeys[root], 1.0f, 0.8f, 0.45f, 0.9f);
    }

    void frame()
    {
        XrFrameWaitInfo wi{ XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState fs{ XR_TYPE_FRAME_STATE };
        if (XR_FAILED(xrWaitFrame(session_, &wi, &fs))) return;
        XrFrameBeginInfo bi{ XR_TYPE_FRAME_BEGIN_INFO };
        xrBeginFrame(session_, &bi);

        const double dt = lastTime_ == 0 ? 1.0 / 72.0 : static_cast<double>(fs.predictedDisplayTime - lastTime_) * 1.0e-9;
        lastTime_ = fs.predictedDisplayTime;
        updateHead(fs.predictedDisplayTime);
        updateHands(fs.predictedDisplayTime);
        bool leftPinch = false, rightPinch = false;
        hands_.update(dt, leftPinch, rightPinch);
        if (player_.ready()) {
            if (leftPinch) player_.setPlaying(!player_.playing());
            if (rightPinch) player_.requestNextPiece();
            applyMacros();
        }

        std::vector<XrCompositionLayerProjectionView> projViews;
        XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        const XrCompositionLayerBaseHeader* layers[1] = { reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer) };
        uint32_t layerCount = 0;

        if (fs.shouldRender) {
            XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = fs.predictedDisplayTime;
            vli.space = stageSpace_;
            XrViewState vs{ XR_TYPE_VIEW_STATE };
            uint32_t viewCount = 0;
            xrLocateViews(session_, &vli, &vs, static_cast<uint32_t>(views_.size()), &viewCount, views_.data());
            if ((vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                scene_.begin();
                buildScene();
                scene_.upload();
                projViews.resize(viewCount, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
                for (uint32_t v = 0; v < viewCount; ++v) {
                    SwapchainTarget& t = targets_[v];
                    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                    uint32_t index = 0;
                    xrAcquireSwapchainImage(t.swapchain, &ai, &index);
                    XrSwapchainImageWaitInfo swi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                    swi.timeout = XR_INFINITE_DURATION;
                    xrWaitSwapchainImage(t.swapchain, &swi);

                    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.images[index].image, 0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
                    scene_.draw(multiply(projectionFromFov(views_[v].fov, 0.05f, 100.0f), viewFromPose(views_[v].pose)), t.width, t.height);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);

                    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                    xrReleaseSwapchainImage(t.swapchain, &ri);

                    projViews[v].pose = views_[v].pose;
                    projViews[v].fov = views_[v].fov;
                    projViews[v].subImage.swapchain = t.swapchain;
                    projViews[v].subImage.imageRect = { { 0, 0 }, { t.width, t.height } };
                    projViews[v].subImage.imageArrayIndex = 0;
                }
                layer.space = stageSpace_;
                layer.viewCount = viewCount;
                layer.views = projViews.data();
                layerCount = 1;
            }
        }

        XrFrameEndInfo ei{ XR_TYPE_FRAME_END_INFO };
        ei.displayTime = fs.predictedDisplayTime;
        ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        ei.layerCount = layerCount;
        ei.layers = layers;
        xrEndFrame(session_, &ei);
    }

    android_app* app_;
    std::string dataDir_;
    Config config_;
    PiecePlayer player_;
    Audio audio_{ player_ };
    std::thread composerThread_;
    std::atomic<bool> stopComposer_{ false };
    Scene scene_;
    Hands hands_;
    int filterId_ = -1, throwId_ = -1;   ///< perform.filter and perform.throw, the hands' controls

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig eglConfig_ = nullptr;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace stageSpace_ = XR_NULL_HANDLE, viewSpace_ = XR_NULL_HANDLE;
    XrSessionState sessionState_ = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning_ = false, quit_ = false, handsSupported_ = false, headValid_ = false;
    std::vector<XrView> views_;
    std::vector<SwapchainTarget> targets_;
    XrPosef headPose_{ { 0, 0, 0, 1 }, { 0, 0, 0 } };
    XrTime lastTime_ = 0;

    PFN_xrCreateHandTrackerEXT pfnCreateHandTracker_ = nullptr;
    PFN_xrLocateHandJointsEXT pfnLocateHandJoints_ = nullptr;
    PFN_xrDestroyHandTrackerEXT pfnDestroyHandTracker_ = nullptr;
    XrHandTrackerEXT handTracker_[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
};

void handleCmd(android_app*, int32_t) {}

} // namespace

/** @brief NativeActivity entry point (android_native_app_glue). */
void android_main(android_app* app)
{
    app->onAppCmd = handleCmd;
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    {
        // On the heap: the engine and the composer's plans are far more than the glue thread's stack.
        auto a = std::make_unique<App>(app);
        if (a->init()) a->run();
        else LOGE("init failed");
        a->shutdown();
    }
    app->activity->vm->DetachCurrentThread();
}
