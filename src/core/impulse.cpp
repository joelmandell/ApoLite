// ApoLite - minimal WAV impulse-response loader.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/impulse.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace apolite {

namespace {

uint32_t readU32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readU16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Everything the header yields, before any sample is decoded.
struct Header {
    bool ok = false;
    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const unsigned char* pcm = nullptr;  // available sample bytes
    size_t pcmSize = 0;                  // sample bytes present in the buffer
    size_t declaredPcmSize = 0;          // size the data chunk header claims
    bool isFloat = false;
};

// Parse RIFF/fmt/data from a buffer holding the header. The returned pointers
// index `data`, so the caller keeps that buffer alive.
Header parseHeader(const unsigned char* data, size_t size) {
    Header h;
    if (size < 44) return h;
    if (std::memcmp(data, "RIFF", 4) != 0) return h;
    if (std::memcmp(data + 8, "WAVE", 4) != 0) return h;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const unsigned char* chunk = data + pos;
        const uint32_t chunkSize = readU32(chunk + 4);
        const unsigned char* body = chunk + 8;
        // Bytes of this chunk that are actually present. A truncated file, or a
        // bounded header probe, can cut a chunk short; reads below must stay
        // inside this many bytes.
        const size_t available = size - (pos + 8);

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16 && available >= 16) {
            h.format = readU16(body);
            h.channels = readU16(body + 2);
            h.sampleRate = readU32(body + 4);
            h.bitsPerSample = readU16(body + 14);
            if (h.format == 0xFFFE && chunkSize >= 40 && available >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format GUID.
                const uint16_t subFormat = readU16(body + 24);
                if (subFormat == 3) h.isFloat = true;
                h.format = subFormat;
            } else if (h.format == 3) {
                h.isFloat = true;
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            h.pcm = body;
            h.declaredPcmSize = chunkSize;
            // The chunk header gives the true length even when the caller handed
            // over only the first part of the file, which is the case when the
            // header is read from a bounded probe. The length is what the budget
            // is checked against, so it must not be limited by the probe size.
            h.pcmSize = std::min<size_t>(chunkSize, available);
        }

        if (pos + 8 + chunkSize > size) break;
        pos += 8 + chunkSize + (chunkSize & 1);
    }

    if (h.pcm == nullptr || h.channels == 0 || h.bitsPerSample == 0) return h;
    h.ok = true;
    return h;
}

size_t framesIn(const Header& h) {
    const size_t bytesPerSample = h.bitsPerSample / 8;
    const size_t frameBytes = bytesPerSample * h.channels;
    if (frameBytes == 0) return 0;
    return h.declaredPcmSize / frameBytes;
}

// Header chunks sit at the front of a WAV file, so a bounded prefix is enough
// to read them without touching the sample data.
constexpr size_t kHeaderProbeBytes = 1u << 20;

}  // namespace

WavImpulseInfo inspectWavImpulse(const std::string& path) {
    WavImpulseInfo info;
    std::ifstream in(path, std::ios::binary);
    if (!in) return info;

    std::vector<unsigned char> probe(kHeaderProbeBytes);
    in.read(reinterpret_cast<char*>(probe.data()), static_cast<std::streamsize>(probe.size()));
    probe.resize(static_cast<size_t>(in.gcount()));

    const Header h = parseHeader(probe.data(), probe.size());
    if (!h.ok) return info;

    info.ok = true;
    info.channels = h.channels;
    info.frames = framesIn(h);
    info.bytesPerSample = h.bitsPerSample / 8;
    return info;
}

std::vector<std::vector<double>> loadWavImpulse(const std::string& path, size_t maxFrames) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());

    const Header h = parseHeader(data.data(), data.size());
    if (!h.ok) return {};

    const size_t bytesPerSample = h.bitsPerSample / 8;
    const size_t frameBytes = bytesPerSample * h.channels;
    if (frameBytes == 0) return {};
    const size_t frames = framesIn(h);

    // Refuse before allocating the decoded taps. The caller normally checks the
    // header first, so a file rejected here was handed over by a direct caller.
    if (maxFrames != 0 && frames > maxFrames) return {};

    // A file truncated on disk declares more frames than it holds. Decode only
    // the frames actually present, so the read stays inside the buffer.
    const size_t availableFrames = h.pcmSize / frameBytes;
    const size_t decodeFrames = std::min(frames, availableFrames);
    if (decodeFrames == 0) return {};

    const uint16_t channels = h.channels;
    const unsigned char* pcm = h.pcm;
    const bool isFloat = h.isFloat;
    const uint16_t bitsPerSample = h.bitsPerSample;

    std::vector<std::vector<double>> out(channels, std::vector<double>(decodeFrames, 0.0));

    for (size_t f = 0; f < decodeFrames; ++f) {
        for (size_t c = 0; c < channels; ++c) {
            const unsigned char* s = pcm + f * frameBytes + c * bytesPerSample;
            double v = 0.0;
            if (isFloat) {
                if (bitsPerSample == 32) {
                    float fv;
                    std::memcpy(&fv, s, 4);
                    v = fv;
                } else if (bitsPerSample == 64) {
                    double dv;
                    std::memcpy(&dv, s, 8);
                    v = dv;
                }
            } else {
                switch (bitsPerSample) {
                    case 8:
                        v = (static_cast<int>(s[0]) - 128) / 128.0;
                        break;
                    case 16: {
                        const int16_t iv = static_cast<int16_t>(readU16(s));
                        v = iv / 32768.0;
                        break;
                    }
                    case 24: {
                        int32_t iv = (static_cast<int32_t>(s[2]) << 16) |
                                     (static_cast<int32_t>(s[1]) << 8) | static_cast<int32_t>(s[0]);
                        if (iv & 0x800000) iv |= ~0xFFFFFF;
                        v = iv / 8388608.0;
                        break;
                    }
                    case 32: {
                        const int32_t iv = static_cast<int32_t>(readU32(s));
                        v = iv / 2147483648.0;
                        break;
                    }
                    default:
                        break;
                }
            }
            out[c][f] = v;
        }
    }
    return out;
}

}  // namespace apolite
