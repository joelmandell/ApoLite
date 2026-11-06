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
    owned_.reset();
}

void Engine::configure(double sampleRate, size_t inputChannels, size_t outputChannels,
                       unsigned channelMask, bool isCapture, bool isPreMix,
                       bool postMixInstalled, const std::string& deviceName,
                       const std::string& connectionName, const std::string& deviceGuid) {
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0;
    inputChannels_ = std::max<size_t>(1, inputChannels);
    outputChannels_ = std::max<size_t>(1, outputChannels);
    channelMask_ = channelMask;
    isCapture_ = isCapture;
    isPreMix_ = isPreMix;
    postMixInstalled_ = postMixInstalled;
    deviceName_ = deviceName;
    connectionName_ = connectionName;
    deviceGuid_ = deviceGuid;

    const size_t deviceChannels = isCapture ? inputChannels_ : outputChannels_;
    layout_ = ChannelLayout(deviceChannels, channelMask);

    std::lock_guard<std::mutex> lock(publishMutex_);
    published_.store(nullptr, std::memory_order_release);
    owned_.reset();
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

    // The chain may create channels beyond the device layout via Copy.
    const size_t channelCount = std::max(prepared->chain.channelNames.size(),
                                         std::max(outputChannels_, layout_.count()));
    prepared->buffersA.assign(channelCount, std::vector<Sample>(prepared->frames, 0.0f));
    prepared->buffersB.assign(channelCount, std::vector<Sample>(prepared->frames, 0.0f));

    for (ChainEntry& entry : prepared->chain.entries) {
        if (entry.filter) entry.filter->prepare(sampleRate_, prepared->frames, entry.inChannels.size());
    }

    const size_t maxChannels = std::max(channelCount, size_t{2});
    scratch_.assign(maxChannels, std::vector<Sample>(prepared->frames, 0.0f));
    scratchPtrs_.assign(maxChannels, nullptr);
    chainInPtrs_.assign(maxChannels, nullptr);
    chainOutPtrs_.assign(maxChannels, nullptr);
    scratchFrames_ = prepared->frames;

    {
        std::lock_guard<std::mutex> lock(publishMutex_);
        owned_ = prepared;
        published_.store(prepared.get(), std::memory_order_release);
    }

    if (log_) {
        log_("Loaded " + std::to_string(prepared->chain.entries.size()) + " filters from " + path);
    }
    return prepared->chain.entries.size();
}

void Engine::processPlanar(const float* const* in, float* const* out, size_t frames) {
    PreparedChain* pc = published_.load(std::memory_order_acquire);
    const size_t bypassChannels = std::min(inputChannels_, outputChannels_);

    if (pc == nullptr || frames == 0) {
        for (size_t c = 0; c < bypassChannels; ++c)
            if (in[c] != out[c]) std::memcpy(out[c], in[c], frames * sizeof(float));
        return;
    }

    // Chunk so the host may hand us any block size without exceeding the
    // buffers the chain was prepared with.
    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(scratchFrames_, frames - done);
        const size_t channelCount = pc->buffersA.size();

        for (size_t c = 0; c < channelCount; ++c) {
            Sample* dst = pc->buffersA[c].data();
            if (c < inputChannels_ && in[c] != nullptr) {
                std::memcpy(dst, in[c] + done, n * sizeof(Sample));
            } else {
                std::memset(dst, 0, n * sizeof(Sample));
            }
        }

        // Windows upmixes mono to stereo automatically; mirror that behaviour.
        if (inputChannels_ == 1 && channelCount >= 2)
            std::memcpy(pc->buffersA[1].data(), pc->buffersA[0].data(), n * sizeof(Sample));

        for (ChainEntry& entry : pc->chain.entries) {
            if (!entry.filter || entry.inChannels.empty()) continue;
            const size_t nIn = entry.inChannels.size();
            const size_t nOut = entry.outChannels.size();

            Sample** inPtrs = chainInPtrs_.data();
            Sample** outPtrs = chainOutPtrs_.data();
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

        const double limit = options_.limiterThreshold;
        for (size_t c = 0; c < outputChannels_; ++c) {
            if (c >= channelCount) {
                std::memset(out[c] + done, 0, n * sizeof(float));
                continue;
            }
            const Sample* src = pc->buffersA[c].data();
            if (limit > 0.0) {
                const float hi = static_cast<float>(limit);
                for (size_t i = 0; i < n; ++i) {
                    float v = src[i];
                    if (v > hi) v = hi;
                    else if (v < -hi) v = -hi;
                    out[c][done + i] = v;
                }
            } else {
                std::memcpy(out[c] + done, src, n * sizeof(float));
            }
        }
        done += n;
    }
}

void Engine::processInterleaved(float* buffer, size_t frames) {
    PreparedChain* pc = published_.load(std::memory_order_acquire);
    if (pc == nullptr || frames == 0) return;

    const size_t channels = outputChannels_;
    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(scratchFrames_, frames - done);
        float* chunk = buffer + done * channels;

        for (size_t c = 0; c < channels; ++c) {
            Sample* dst = scratch_[c].data();
            for (size_t i = 0; i < n; ++i) dst[i] = chunk[i * channels + c];
            scratchPtrs_[c] = scratch_[c].data();
        }

        processPlanar(const_cast<const float* const*>(scratchPtrs_.data()), scratchPtrs_.data(),
                      n);

        for (size_t c = 0; c < channels; ++c) {
            const Sample* src = scratch_[c].data();
            for (size_t i = 0; i < n; ++i) chunk[i * channels + c] = src[i];
        }
        done += n;
    }
}

}  // namespace apolite
