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

}  // namespace

std::vector<std::vector<double>> loadWavImpulse(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (data.size() < 44) return {};
    if (std::memcmp(data.data(), "RIFF", 4) != 0) return {};
    if (std::memcmp(data.data() + 8, "WAVE", 4) != 0) return {};

    uint16_t format = 0, channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmSize = 0;
    bool isFloat = false;

    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const unsigned char* chunk = data.data() + pos;
        const uint32_t chunkSize = readU32(chunk + 4);
        const unsigned char* body = chunk + 8;
        if (pos + 8 + chunkSize > data.size()) break;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            format = readU16(body);
            channels = readU16(body + 2);
            sampleRate = readU32(body + 4);
            bitsPerSample = readU16(body + 14);
            if (format == 0xFFFE && chunkSize >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format GUID.
                const uint16_t subFormat = readU16(body + 24);
                if (subFormat == 3) isFloat = true;
                format = subFormat;
            } else if (format == 3) {
                isFloat = true;
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            pcm = body;
            pcmSize = chunkSize;
        }
        pos += 8 + chunkSize + (chunkSize & 1);
    }

    if (pcm == nullptr || channels == 0 || bitsPerSample == 0) return {};

    const size_t bytesPerSample = bitsPerSample / 8;
    const size_t frameBytes = bytesPerSample * channels;
    if (frameBytes == 0) return {};
    const size_t frames = pcmSize / frameBytes;

    std::vector<std::vector<double>> out(channels, std::vector<double>(frames, 0.0));

    for (size_t f = 0; f < frames; ++f) {
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
    (void)sampleRate;
    return out;
}

}  // namespace apolite
