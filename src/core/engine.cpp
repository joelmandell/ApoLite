// ApoLite - processing engine.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace apolite {

Engine::Engine() = default;

Engine::~Engine() {
    std::lock_guard<std::mutex> lock(publishMutex_);
    published_.store(nullptr, std::memory_order_release);
    // Nothing is reading any more: the owner is destroying the engine.
    retired_.clear();
    owned_.reset();
}

bool Engine::readersQuiescent() {
    // A reader adds one before loading published_ and one after its last use, so
    // an even count means no reader can still be inside process(). seq_cst keeps
    // a reader that is between its increment and its load from being missed: if
    // this read does not see that increment, the reader's load is ordered after
    // the store this reclamation follows, so it cannot observe the freed chain.
    return (readerSeq_.load(std::memory_order_seq_cst) & 1u) == 0;
}

void Engine::retireLocked() {
    if (owned_) retired_.push_back(std::move(owned_));
}

void Engine::reclaimLocked() {
    // The most recently retired chain is kept as a grace buffer and freed on a
    // later publish, by which time the audio thread has run many callbacks. That
    // bounds the retained memory to one chain while leaving a full publish
    // interval of margin, on top of the quiescence check.
    if (retired_.size() <= 1 || !readersQuiescent()) return;
    retired_.erase(retired_.begin(), retired_.end() - 1);
}

void Engine::configure(double sampleRate, size_t inputChannels, size_t outputChannels,
                       unsigned channelMask, bool isCapture, bool isPreMix,
                       bool postMixInstalled, const std::string& deviceName,
                       const std::string& connectionName, const std::string& deviceGuid) {
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0;
    inputChannels_.store(std::max<size_t>(1, inputChannels), std::memory_order_relaxed);
    outputChannels_.store(std::max<size_t>(1, outputChannels), std::memory_order_relaxed);
    channelMask_ = channelMask;
    isCapture_ = isCapture;
    isPreMix_ = isPreMix;
    postMixInstalled_ = postMixInstalled;
    deviceName_ = deviceName;
    connectionName_ = connectionName;
    deviceGuid_ = deviceGuid;

    const size_t deviceChannels = isCapture ? inputChannels_.load(std::memory_order_relaxed)
                                           : outputChannels_.load(std::memory_order_relaxed);
    layout_ = ChannelLayout(deviceChannels, channelMask);

    // A format change invalidates the prepared chain. Retire it rather than
    // destroying it, in case the audio thread is still inside process().
    std::lock_guard<std::mutex> lock(publishMutex_);
    published_.store(nullptr, std::memory_order_release);
    retireLocked();
    reclaimLocked();
}

size_t Engine::loadConfiguration(const std::string& path) {
    DeviceContext ctx;
    ctx.sampleRate = sampleRate_;
    ctx.channelCount = layout_.count();
    ctx.channelMask = channelMask_;
    ctx.isCapture = isCapture_;
    ctx.isPreMix = isPreMix_;
    ctx.postMixInstalled = postMixInstalled_;
    ctx.deviceName = deviceName_;
    ctx.connectionName = connectionName_;
    ctx.deviceGuid = deviceGuid_;
    ctx.deviceString = " " + deviceName_ + " " + connectionName_ + " " + deviceGuid_ + " ";
    ctx.stageName = isCapture_ ? "capture" : (isPreMix_ ? "pre-mix" : "post-mix");

    ConfigLoader loader(ctx, log_);
    FilterChain chain = loader.loadFile(path);

    auto prepared = std::make_shared<PreparedChain>();
    prepared->chain = std::move(chain);
    prepared->frames = std::max<size_t>(1, options_.maxFrames);
    prepared->inputChannels = inputChannels_.load(std::memory_order_relaxed);
    prepared->outputChannels = outputChannels_.load(std::memory_order_relaxed);
    prepared->limiter = options_.limiterThreshold;

    // The chain may create channels beyond the device layout via Copy.
    const size_t channelCount = std::max(prepared->chain.channelNames.size(),
                                         std::max(outputChannels_.load(std::memory_order_relaxed),
                                                  layout_.count()));
    prepared->buffersA.assign(channelCount, std::vector<Sample>(prepared->frames, 0.0f));
    prepared->buffersB.assign(channelCount, std::vector<Sample>(prepared->frames, 0.0f));
    prepared->chainInPtrs.assign(channelCount, nullptr);
    prepared->chainOutPtrs.assign(channelCount, nullptr);

    for (ChainEntry& entry : prepared->chain.entries) {
        if (entry.filter) entry.filter->prepare(sampleRate_, prepared->frames, entry.inChannels.size());
    }

    {
        std::lock_guard<std::mutex> lock(publishMutex_);
        retireLocked();
        owned_ = prepared;
        published_.store(prepared.get(), std::memory_order_release);
        reclaimLocked();
    }

    if (log_) {
        log_("Loaded " + std::to_string(prepared->chain.entries.size()) + " filters from " + path);
    }
    return prepared->chain.entries.size();
}

void Engine::runChain(PreparedChain* pc, size_t n) {
    const size_t channelCount = pc->buffersA.size();

    for (ChainEntry& entry : pc->chain.entries) {
        if (!entry.filter || entry.inChannels.empty()) continue;
        const size_t nIn = entry.inChannels.size();
        const size_t nOut = entry.outChannels.size();

        Sample** inPtrs = pc->chainInPtrs.data();
        Sample** outPtrs = pc->chainOutPtrs.data();
        for (size_t i = 0; i < nIn; ++i) inPtrs[i] = pc->buffersA[entry.inChannels[i]].data();
        for (size_t i = 0; i < nOut; ++i) {
            const size_t ch = entry.outChannels[i];
            outPtrs[i] = entry.filter->inPlace() ? pc->buffersA[ch].data()
                                                 : pc->buffersB[ch].data();
        }

        entry.filter->process(const_cast<const Sample* const*>(inPtrs), outPtrs, n, nOut);

        if (!entry.filter->inPlace()) {
            for (size_t i = 0; i < nOut; ++i) {
                const size_t ch = entry.outChannels[i];
                std::swap(pc->buffersA[ch], pc->buffersB[ch]);
            }
        }
    }

    // The limiter is applied in place, so callers only have to move the result
    // to the host buffer once.
    const double limit = pc->limiter;
    if (limit > 0.0) {
        const float hi = static_cast<float>(limit);
        const size_t channels = std::min(pc->outputChannels, channelCount);
        for (size_t c = 0; c < channels; ++c) {
            Sample* d = pc->buffersA[c].data();
            for (size_t i = 0; i < n; ++i) {
                float v = d[i];
                if (v > hi) v = hi;
                else if (v < -hi) v = -hi;
                d[i] = v;
            }
        }
    }
}

void Engine::processPlanarWith(PreparedChain* pc, const float* const* in, float* const* out,
                               size_t frames) {
    const size_t inputChannels = pc->inputChannels;
    const size_t outputChannels = pc->outputChannels;

    if (frames == 0) return;

    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(pc->frames, frames - done);
        const size_t channelCount = pc->buffersA.size();

        for (size_t c = 0; c < channelCount; ++c) {
            Sample* dst = pc->buffersA[c].data();
            if (c < inputChannels && in[c] != nullptr) {
                std::memcpy(dst, in[c] + done, n * sizeof(Sample));
            } else {
                std::memset(dst, 0, n * sizeof(Sample));
            }
        }

        // Windows upmixes mono to stereo automatically; mirror that behaviour.
        if (inputChannels == 1 && channelCount >= 2)
            std::memcpy(pc->buffersA[1].data(), pc->buffersA[0].data(), n * sizeof(Sample));

        runChain(pc, n);

        for (size_t c = 0; c < outputChannels; ++c) {
            if (c >= channelCount) {
                std::memset(out[c] + done, 0, n * sizeof(float));
                continue;
            }
            std::memcpy(out[c] + done, pc->buffersA[c].data(), n * sizeof(float));
        }
        done += n;
    }
}

void Engine::processPlanar(const float* const* in, float* const* out, size_t frames) {
    ReaderGuard guard(readerSeq_);
    PreparedChain* pc = published_.load(std::memory_order_acquire);
    if (pc == nullptr || frames == 0) {
        // No configuration is loaded: pass audio through untouched. The counts
        // are atomic, so this is safe even mid-reconfigure.
        const size_t bypass =
            std::min(inputChannels_.load(std::memory_order_relaxed),
                     outputChannels_.load(std::memory_order_relaxed));
        for (size_t c = 0; c < bypass; ++c)
            if (in[c] != out[c]) std::memcpy(out[c], in[c], frames * sizeof(float));
        return;
    }
    processPlanarWith(pc, in, out, frames);
}

void Engine::processInterleaved(float* buffer, size_t frames) {
    ReaderGuard guard(readerSeq_);
    PreparedChain* pc = published_.load(std::memory_order_acquire);
    if (pc == nullptr || frames == 0) return;

    const size_t channels = pc->outputChannels;
    const size_t channelCount = pc->buffersA.size();
    if (channels == 0 || channelCount == 0) return;

    // Deinterleave straight into the published buffers, run the chain in place
    // and interleave back. The staging buffers are not needed, so an interleaved
    // block costs one deinterleave and one interleave pass, not three.
    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(pc->frames, frames - done);
        float* chunk = buffer + done * channels;

        for (size_t c = 0; c < channels; ++c) {
            Sample* dst = c < channelCount ? pc->buffersA[c].data() : nullptr;
            if (dst == nullptr) continue;
            for (size_t i = 0; i < n; ++i) dst[i] = chunk[i * channels + c];
        }
        // Channels the buffer does not carry must be silent, not stale.
        for (size_t c = channels; c < channelCount; ++c)
            std::memset(pc->buffersA[c].data(), 0, n * sizeof(Sample));

        runChain(pc, n);

        for (size_t c = 0; c < channels; ++c) {
            if (c >= channelCount) {
                for (size_t i = 0; i < n; ++i) chunk[i * channels + c] = 0.0f;
                continue;
            }
            const Sample* src = pc->buffersA[c].data();
            for (size_t i = 0; i < n; ++i) chunk[i * channels + c] = src[i];
        }
        done += n;
    }
}

}  // namespace apolite
