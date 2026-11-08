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
//
// Reloading a configuration is safe while audio runs. The prepared chain and
// every buffer it needs are immutable once published, and the chain it replaces
// is not freed until the audio thread is observed outside process().
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
    size_t inputChannels() const { return inputChannels_.load(std::memory_order_relaxed); }
    size_t outputChannels() const { return outputChannels_.load(std::memory_order_relaxed); }
    unsigned channelMask() const { return channelMask_; }

    const ChannelLayout& layout() const { return layout_; }
    void setOptions(const EngineOptions& o) { options_ = o; }
    void setMaxFrames(size_t frames) {
        options_.maxFrames = frames > 0 ? frames : options_.maxFrames;
    }
    void setLogSink(LogSink sink) { log_ = std::move(sink); }

private:
    // Immutable once published. Every buffer the audio thread touches lives
    // here, so a reload cannot resize state under a running process() call.
    struct PreparedChain {
        FilterChain chain;
        std::vector<std::vector<Sample>> buffersA;
        std::vector<std::vector<Sample>> buffersB;
        std::vector<std::vector<Sample>> interleave;  // deinterleave staging
        std::vector<Sample*> interleavePtrs;
        std::vector<Sample*> chainInPtrs;
        std::vector<Sample*> chainOutPtrs;
        size_t frames = 0;
        size_t inputChannels = 0;
        size_t outputChannels = 0;
        double limiter = 0.0;
    };

    // Marks the audio thread as inside a read of published_. The count is
    // incremented before the pointer is loaded and again after the last use, so
    // an even value means no reader can still hold a retired chain.
    class ReaderGuard {
    public:
        explicit ReaderGuard(std::atomic<uint64_t>& seq) : seq_(&seq) {
            seq_->fetch_add(1, std::memory_order_acq_rel);
        }
        ~ReaderGuard() { seq_->fetch_add(1, std::memory_order_acq_rel); }
        ReaderGuard(const ReaderGuard&) = delete;
        ReaderGuard& operator=(const ReaderGuard&) = delete;

    private:
        std::atomic<uint64_t>* seq_;
    };

    void processPlanarWith(PreparedChain* pc, const float* const* in, float* const* out,
                           size_t frames);
    // Caller must hold publishMutex_.
    void retireLocked();
    void reclaimLocked();
    bool readersQuiescent();

    double sampleRate_ = 48000.0;
    std::atomic<size_t> inputChannels_{2};
    std::atomic<size_t> outputChannels_{2};
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
    // Chains replaced but not yet provably unused. Freed on a later publish.
    std::vector<std::shared_ptr<PreparedChain>> retired_;
    std::mutex publishMutex_;
    std::atomic<uint64_t> readerSeq_{0};
};

}  // namespace apolite
