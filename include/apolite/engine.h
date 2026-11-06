// ApoLite - processing engine: deinterleave, run the filter chain, reinterleave.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "apolite/channel.h"
#include "apolite/config.h"
#include "apolite/filter.h"

namespace apolite {

// Options that control how the engine behaves.
struct EngineOptions {
    // Block size used for the internal per-channel buffers.
    size_t maxFrames = 1024;
    // Linear magnitude ceiling applied after the chain (0 disables it).
    double limiterThreshold = 0.0;
};

// Real-time audio processing engine. All configuration is prepared off the
// audio thread; the audio thread only swaps a prepared chain pointer.
class Engine {
public:
    Engine();
    ~Engine();

    // Configure the stream format. Safe to call from the control thread while
    // audio is running: the new chain is prepared and then published atomically.
    void configure(double sampleRate, size_t inputChannels, size_t outputChannels,
                   unsigned channelMask, bool isCapture, bool isPreMix,
                   bool postMixInstalled, const std::string& deviceName,
                   const std::string& connectionName, const std::string& deviceGuid);

    // Load and publish a new configuration. Returns the chain's entry count.
    size_t loadConfiguration(const std::string& path);

    // Process interleaved float audio in place.
    void processInterleaved(float* buffer, size_t frames);

    // Process deinterleaved channel pointers.
    void processPlanar(const float* const* in, float* const* out, size_t frames);

    double sampleRate() const { return sampleRate_; }
    size_t inputChannels() const { return inputChannels_; }
    size_t outputChannels() const { return outputChannels_; }
    unsigned channelMask() const { return channelMask_; }

    const ChannelLayout& layout() const { return layout_; }
    void setOptions(const EngineOptions& o) { options_ = o; }
    void setMaxFrames(size_t frames) {
        options_.maxFrames = frames > 0 ? frames : options_.maxFrames;
    }
    void setLogSink(LogSink sink) { log_ = std::move(sink); }

private:
    struct PreparedChain {
        FilterChain chain;
        std::vector<std::vector<Sample>> buffersA;
        std::vector<std::vector<Sample>> buffersB;
        size_t frames = 0;
    };

    double sampleRate_ = 48000.0;
    size_t inputChannels_ = 2;
    size_t outputChannels_ = 2;
    unsigned channelMask_ = 0;
    bool isCapture_ = false;
    bool isPreMix_ = false;
    bool postMixInstalled_ = true;
    std::string deviceName_;
    std::string connectionName_;
    std::string deviceGuid_;

    ChannelLayout layout_;
    LogSink log_;
    EngineOptions options_;

    std::atomic<PreparedChain*> published_{nullptr};
    std::shared_ptr<PreparedChain> owned_;
    std::mutex publishMutex_;

    std::vector<std::vector<Sample>> scratch_;
    std::vector<Sample*> scratchPtrs_;
    std::vector<Sample*> chainInPtrs_;
    std::vector<Sample*> chainOutPtrs_;
    size_t scratchFrames_ = 0;
};

}  // namespace apolite
