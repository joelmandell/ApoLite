// ApoLite - minimal WAV impulse-response loader.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <string>
#include <vector>

namespace apolite {

// Load a PCM or IEEE-float WAV file and return one tap vector per channel.
// Returns an empty vector on failure. Only the formats needed for impulse
// responses are supported (PCM 8/16/24/32-bit and float 32/64-bit).
std::vector<std::vector<double>> loadWavImpulse(const std::string& path);

}  // namespace apolite
