// ApoLite - minimal WAV impulse-response loader.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace apolite {

// What a WAV header says about the impulse it holds, read without decoding any
// samples. Lets a caller reject a file that is too long before allocating it.
struct WavImpulseInfo {
    bool ok = false;         // the header parsed and a data chunk was found
    size_t channels = 0;     // channel count
    size_t frames = 0;       // sample frames, the tap count
    size_t bytesPerSample = 0;
};

// Read only the header of a WAV file. Cheap: no sample data is read or decoded,
// so a huge file costs one small read.
WavImpulseInfo inspectWavImpulse(const std::string& path);

// Load a PCM or IEEE-float WAV file and return one tap vector per channel.
// Returns an empty vector on failure. Only the formats needed for impulse
// responses are supported (PCM 8/16/24/32-bit and float 32/64-bit).
//
// `maxFrames` rejects a file whose frame count exceeds it, before any samples
// are allocated. Pass 0 for no limit. The header is parsed first, so a rejected
// file costs one small read rather than the full allocation.
std::vector<std::vector<double>> loadWavImpulse(const std::string& path, size_t maxFrames = 0);

}  // namespace apolite
