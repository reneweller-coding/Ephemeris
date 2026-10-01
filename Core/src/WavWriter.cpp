/**
 * @file WavWriter.cpp
 * @brief Streaming WAV writer.
 *
 * The header is written twice: a provisional one at open() so the data starts at a fixed offset,
 * and the final one at close(). Both are 'RIFF' with a 'JUNK' chunk of 28 bytes after 'WAVE'; if
 * the data turned out larger than a 32-bit size allows, close() rewrites 'RIFF' as 'RF64' and the
 * 'JUNK' chunk as the 'ds64' chunk that carries the 64-bit sizes (EBU Tech 3306).
 * @note Copied from Phosphene `Core/src/WavWriter.cpp` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#include "eph/WavWriter.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace eph {

namespace {
/** @brief Writes @p v little-endian to @p p. */
void le16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
/** @brief Writes @p v little-endian to @p p. */
void le32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
/** @brief Writes @p v little-endian to @p p. */
void le64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }
constexpr int kHeaderBytes = 12 + 36 + 24 + 8;   ///< RIFF/WAVE + JUNK/ds64 + fmt + data header
}

bool WavWriter::open(const char* path, int sampleRate, int channels, WavFormat format)
{
    close();
    f_ = std::fopen(path, "wb");
    if (f_ == nullptr) return false;
    sampleRate_ = sampleRate;
    channels_ = channels == 1 ? 1 : 2;
    format_ = format;
    frames_ = 0;
    clipped_ = 0;
    trailing_ = 0;
    rng_ = 0x9E3779B97F4A7C15ull;   // the dither starts over with every file: the same render writes the same bytes
    return writeHeader(false);
}

bool WavWriter::writeHeader(bool final)
{
    const int bytesPerSample = format_ == WavFormat::Float32 ? 4 : 3;
    const uint64_t dataBytes = frames_ * static_cast<uint64_t>(channels_ * bytesPerSample);
    const uint64_t riffBytes = dataBytes + kHeaderBytes - 8 + trailing_;   // the cue and tag chunks after the data (23.09.2026)
    const bool rf64 = final && riffBytes > 0xFFFFFFFFull;

    uint8_t h[kHeaderBytes] = {};
    std::memcpy(h, rf64 ? "RF64" : "RIFF", 4);
    le32(h + 4, rf64 ? 0xFFFFFFFFu : static_cast<uint32_t>(riffBytes));
    std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, rf64 ? "ds64" : "JUNK", 4);
    le32(h + 16, 28);
    if (rf64) {
        le64(h + 20, riffBytes);
        le64(h + 28, dataBytes);
        le64(h + 36, frames_);
        le32(h + 44, 0);
    }
    std::memcpy(h + 48, "fmt ", 4);
    le32(h + 52, 16);
    le16(h + 56, format_ == WavFormat::Float32 ? 3 : 1);
    le16(h + 58, static_cast<uint16_t>(channels_));
    le32(h + 60, static_cast<uint32_t>(sampleRate_));
    le32(h + 64, static_cast<uint32_t>(sampleRate_ * channels_ * bytesPerSample));
    le16(h + 68, static_cast<uint16_t>(channels_ * bytesPerSample));
    le16(h + 70, static_cast<uint16_t>(bytesPerSample * 8));
    std::memcpy(h + 72, "data", 4);
    le32(h + 76, rf64 ? 0xFFFFFFFFu : static_cast<uint32_t>(dataBytes));
    if (std::fseek(f_, 0, SEEK_SET) != 0) return false;
    return std::fwrite(h, 1, sizeof(h), f_) == sizeof(h);
}

bool WavWriter::write(const float* L, const float* R, int n)
{
    if (f_ == nullptr || n <= 0) return f_ != nullptr;
    if (std::fseek(f_, 0, SEEK_END) != 0) return false;
    const int bps = format_ == WavFormat::Float32 ? 4 : 3;
    std::vector<uint8_t> buf(static_cast<size_t>(n) * static_cast<size_t>(channels_ * bps));
    uint8_t* p = buf.data();
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < channels_; ++c) {
            const float v = (c == 0 || R == nullptr) ? L[i] : R[i];
            if (format_ == WavFormat::Float32) {
                std::memcpy(p, &v, 4);
                p += 4;
            } else {
                double x = static_cast<double>(v) * 8388608.0;
                // TPDF dither of one step (setDither): drawn for every sample, so the stream does not depend on
                // the signal, and left off exactly-zero samples, so silence stays digital silence.
                if (dither_) {
                    const double d = uniform() - uniform();
                    if (v != 0.0f) x += d;
                }
                if (x > 8388607.0) { x = 8388607.0; ++clipped_; }
                if (x < -8388608.0) { x = -8388608.0; ++clipped_; }
                const int32_t s = static_cast<int32_t>(std::lround(x));
                p[0] = uint8_t(s); p[1] = uint8_t(s >> 8); p[2] = uint8_t(s >> 16);
                p += 3;
            }
        }
    }
    frames_ += static_cast<uint64_t>(n);
    return std::fwrite(buf.data(), 1, buf.size(), f_) == buf.size();
}

std::vector<uint8_t> WavWriter::trailingChunks() const
{
    std::vector<uint8_t> out;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
    auto putId = [&](const char* id) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(id[i])); };
    // A text as a chunk body: zero-terminated, padded to an even length (RIFF's word alignment).
    auto text = [](const std::string& s) { std::vector<uint8_t> b(s.begin(), s.end()); b.push_back(0); if (b.size() % 2) b.push_back(0); return b; };
    if (!cues_.empty()) {
        putId("cue ");
        put32(static_cast<uint32_t>(4 + 24 * cues_.size()));
        put32(static_cast<uint32_t>(cues_.size()));
        for (size_t i = 0; i < cues_.size(); ++i) {
            put32(static_cast<uint32_t>(i + 1));                   // dwName: the id the label refers to
            put32(static_cast<uint32_t>(cues_[i].first));          // dwPosition
            putId("data");                                         // fccChunk
            put32(0);                                              // dwChunkStart
            put32(0);                                              // dwBlockStart
            put32(static_cast<uint32_t>(cues_[i].first));          // dwSampleOffset
        }
        std::vector<uint8_t> adtl;
        for (size_t i = 0; i < cues_.size(); ++i) {
            const std::vector<uint8_t> t = text(cues_[i].second);
            const char* id = "labl";
            for (int k = 0; k < 4; ++k) adtl.push_back(uint8_t(id[k]));
            const uint32_t sz = static_cast<uint32_t>(4 + cues_[i].second.size() + 1);   // the size excludes the pad byte
            for (int k = 0; k < 4; ++k) adtl.push_back(uint8_t(sz >> (8 * k)));
            for (int k = 0; k < 4; ++k) adtl.push_back(uint8_t((i + 1) >> (8 * k)));
            adtl.insert(adtl.end(), t.begin(), t.end());
        }
        putId("LIST");
        put32(static_cast<uint32_t>(4 + adtl.size()));
        putId("adtl");
        out.insert(out.end(), adtl.begin(), adtl.end());
    }
    if (!info_.empty()) {
        std::vector<uint8_t> body;
        for (const auto& kv : info_) {
            const std::vector<uint8_t> t = text(kv.second);
            for (int k = 0; k < 4; ++k) body.push_back(uint8_t(kv.first[static_cast<size_t>(k)]));
            const uint32_t sz = static_cast<uint32_t>(kv.second.size() + 1);
            for (int k = 0; k < 4; ++k) body.push_back(uint8_t(sz >> (8 * k)));
            body.insert(body.end(), t.begin(), t.end());
        }
        putId("LIST");
        put32(static_cast<uint32_t>(4 + body.size()));
        putId("INFO");
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

bool WavWriter::close()
{
    if (f_ == nullptr) return true;
    // The markers and tags after the audio (23.09.2026). The data chunk is padded to an even length first, as
    // RIFF wants every chunk to start on a word; the pad byte is not part of the data size, only of the RIFF size.
    if (!cues_.empty() || !info_.empty()) {
        const int bps = format_ == WavFormat::Float32 ? 4 : 3;
        const uint64_t dataBytes = frames_ * static_cast<uint64_t>(channels_ * bps);
        std::vector<uint8_t> tail;
        if (dataBytes % 2) tail.push_back(0);
        const std::vector<uint8_t> chunks = trailingChunks();
        tail.insert(tail.end(), chunks.begin(), chunks.end());
        if (std::fseek(f_, 0, SEEK_END) == 0 && std::fwrite(tail.data(), 1, tail.size(), f_) == tail.size()) trailing_ = tail.size();
    }
    bool ok = writeHeader(true);
    ok = std::fclose(f_) == 0 && ok;
    f_ = nullptr;
    // Markers and tags belong to the file just closed; the next open() starts without any.
    cues_.clear();
    info_.clear();
    return ok;
}

} // namespace eph
