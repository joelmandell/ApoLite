// ApoLite - real-time cost of the processing chain.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// Measures how much of a CPU core the chain uses, as a fraction of real time.
// The audio thread only needs to finish each block before the next one arrives,
// so a figure well under 1% is inaudible in a call.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apolite/engine.h"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr size_t kBlockFrames = 480;  // 10 ms at 48 kHz, a typical APO block
constexpr size_t kWarmupBlocks = 200;
constexpr size_t kMeasureBlocks = 20000;

struct Result {
    double realtimePercent = 0.0;
    double nsPerSample = 0.0;
};

// Runs `blocks` of audio through the engine and returns the cost as a
// percentage of the wall-clock time those blocks represent.
Result measure(const std::string& config, size_t channels) {
    apolite::Engine engine;
    engine.configure(kSampleRate, channels, channels, 0, /*isCapture=*/true, false, true,
                     "Microphone", "Microphone", "");
    if (engine.loadConfiguration(config) == 0) {
        std::fprintf(stderr, "warning: no filters loaded from %s\n", config.c_str());
    }

    std::vector<std::vector<float>> in(channels, std::vector<float>(kBlockFrames, 0.0f));
    std::vector<std::vector<float>> out(channels, std::vector<float>(kBlockFrames, 0.0f));
    std::vector<const float*> inPtrs(channels);
    std::vector<float*> outPtrs(channels);

    // Speech-like input so the compressor and limiter see realistic levels.
    for (size_t c = 0; c < channels; ++c) {
        for (size_t i = 0; i < kBlockFrames; ++i) {
            const double t = static_cast<double>(i) / kSampleRate;
            in[c][i] = 0.2f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * t));
        }
        inPtrs[c] = in[c].data();
        outPtrs[c] = out[c].data();
    }

    for (size_t i = 0; i < kWarmupBlocks; ++i)
        engine.processPlanar(inPtrs.data(), outPtrs.data(), kBlockFrames);

    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < kMeasureBlocks; ++i)
        engine.processPlanar(inPtrs.data(), outPtrs.data(), kBlockFrames);
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    const double audioSeconds = static_cast<double>(kMeasureBlocks * kBlockFrames) / kSampleRate;

    Result r;
    r.realtimePercent = 100.0 * seconds / audioSeconds;
    r.nsPerSample = seconds * 1e9 / static_cast<double>(kMeasureBlocks * kBlockFrames * channels);
    return r;
}

void report(const std::string& label, const Result& r) {
    std::printf("  %-30s %8.3f %%   %6.1f ns/sample\n", label.c_str(), r.realtimePercent,
                r.nsPerSample);
}

}  // namespace

int main() {
    std::printf("ApoLite processing cost, %.0f Hz, %zu-frame blocks\n\n", kSampleRate,
                kBlockFrames);
    std::printf("  %-30s %8s   %s\n", "configuration", "cost", "per sample");
    std::printf("  %-30s %8s   %s\n", "------------------------------", "--------",
                "------------");

    report("voice preset, mono", measure("config/voice-mic.txt", 1));
    report("voice preset, stereo", measure("config/voice-mic.txt", 2));

    // A heavy case: 20 peaking bands, far more than the preset uses, to show
    // how cost scales with the number of filters.
    std::string heavy = "Preamp: -6 dB\n";
    for (int i = 0; i < 20; ++i) {
        char line[128];
        std::snprintf(line, sizeof(line), "Filter: ON PK Fc %d Hz Gain -3 dB Q 1.0\n",
                      40 * (i + 1));
        heavy += line;
    }
    heavy += "Compressor: Threshold -20 Ratio 4 Attack 5 Release 100 Makeup 6 Knee 6 Limit -1\n";

    const char* tmp = "apolite_bench_heavy.txt";
    if (FILE* f = std::fopen(tmp, "wb")) {
        std::fwrite(heavy.data(), 1, heavy.size(), f);
        std::fclose(f);
    }
    report("20 bands + compressor", measure(tmp, 1));
    std::remove(tmp);

    std::printf("\nLower is better. Well under 1 %% is negligible in a call.\n");
    return 0;
}
