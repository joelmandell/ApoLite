// ApoLite - concrete filter implementations.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/filter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace apolite {

namespace {
constexpr size_t kConvolverBlock = 256;

// -log2(10) / 20: turns a decibel gain into an exp2 exponent, since
// 10^(-x/20) == 2^(x * kNegLog2TenOver20). exp2 is a single instruction where
// pow is a library call.
constexpr float kNegLog2TenOver20 = -0.16609640474f;
}

void PreampFilter::prepare(double sampleRate, size_t maxFrames, size_t channels) {
    (void)sampleRate;
    (void)maxFrames;
    (void)channels;
    gain_ = static_cast<float>(std::pow(10.0, gainDb_ / 20.0));
}

void PreampFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                           size_t channels) {
    for (size_t c = 0; c < channels; ++c) {
        const Sample* src = in[c];
        Sample* dst = out[c];
        if (dst == src) {
            for (size_t i = 0; i < frames; ++i) dst[i] *= gain_;
        } else {
            for (size_t i = 0; i < frames; ++i) dst[i] = src[i] * gain_;
        }
    }
}

BiquadFilter::BiquadFilter(FilterType type, double gainDb, double freq, double q, ParamKind kind,
                           bool isCornerFreq)
    : type_(type), gainDb_(gainDb), freq_(freq), q_(q), kind_(kind), isCornerFreq_(isCornerFreq) {}

void BiquadFilter::prepare(double sampleRate, size_t maxFrames, size_t channels) {
    (void)maxFrames;
    const Biquad proto = Biquad::design(type_, gainDb_, freq_, sampleRate, q_, kind_, isCornerFreq_);
    biquads_.assign(channels, proto);
    for (auto& b : biquads_) b.reset();
}

void BiquadFilter::reset() {
    for (auto& b : biquads_) b.reset();
}

void BiquadFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                           size_t channels) {
    for (size_t c = 0; c < channels; ++c) {
        Biquad& bq = biquads_[c];
        const Sample* src = in[c];
        Sample* dst = out[c];
        for (size_t i = 0; i < frames; ++i) dst[i] = static_cast<Sample>(bq.process(src[i]));
    }
}

IirFilter::IirFilter(std::vector<double> coefficients) {
    const size_t total = coefficients.size();
    const size_t order = total / 2 - 1;
    const double a0 = coefficients[order + 1];
    b_.resize(order + 1);
    a_.resize(order + 1);
    for (size_t i = 0; i <= order; ++i) b_[i] = coefficients[i] / a0;
    for (size_t i = 0; i <= order; ++i) a_[i] = coefficients[order + 1 + i] / a0;
}

void IirFilter::prepare(double sampleRate, size_t maxFrames, size_t channels) {
    (void)sampleRate;
    (void)maxFrames;
    state_.assign(channels, std::vector<double>(b_.size(), 0.0));
}

void IirFilter::reset() {
    for (auto& s : state_) std::fill(s.begin(), s.end(), 0.0);
}

void IirFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                        size_t channels) {
    const size_t order = b_.size();
    for (size_t c = 0; c < channels; ++c) {
        std::vector<double>& z = state_[c];
        const Sample* src = in[c];
        Sample* dst = out[c];
        for (size_t i = 0; i < frames; ++i) {
            const double x = src[i];
            const double y = b_[0] * x + z[0];
            for (size_t k = 1; k < order; ++k) {
                const double zNext = (k + 1 < order) ? z[k] : 0.0;
                z[k - 1] = b_[k] * x - a_[k] * y + zNext;
            }
            z[order - 1] = b_[order - 1] * x - a_[order - 1] * y;
            dst[i] = static_cast<Sample>(y);
        }
    }
}

void DelayFilter::prepare(double sampleRate, size_t maxFrames, size_t channels) {
    (void)maxFrames;
    delaySamples_ = isMs_ ? static_cast<size_t>(sampleRate * delay_ / 1000.0 + 0.5)
                          : static_cast<size_t>(delay_ + 0.5);
    lines_.assign(channels, std::vector<Sample>(delaySamples_ ? delaySamples_ : 1, 0.0f));
    pos_ = 0;
}

void DelayFilter::reset() {
    for (auto& l : lines_) std::fill(l.begin(), l.end(), 0.0f);
    pos_ = 0;
}

void DelayFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                          size_t channels) {
    if (delaySamples_ == 0) {
        for (size_t c = 0; c < channels; ++c)
            if (in[c] != out[c]) std::memcpy(out[c], in[c], frames * sizeof(Sample));
        return;
    }
    const size_t len = lines_[0].size();
    for (size_t c = 0; c < channels; ++c) {
        std::vector<Sample>& line = lines_[c];
        const Sample* src = in[c];
        Sample* dst = out[c];
        size_t p = pos_;
        for (size_t i = 0; i < frames; ++i) {
            const Sample delayed = line[p];
            line[p] = src[i];
            dst[i] = delayed;
            p = (p + 1) % len;
        }
    }
    pos_ = (pos_ + frames) % len;
}

void CopyFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                         size_t channels) {
    (void)channels;
    for (const CopyAssignment& a : assignments_) {
        if (a.targetChannel < 0 || a.sources.empty()) continue;
        Sample* dst = out[a.targetChannel];

        const CopySummand& first = a.sources[0];
        if (first.sourceChannel < 0) {
            std::fill(dst, dst + frames, first.factor);
        } else if (first.factor == 1.0f) {
            std::memcpy(dst, in[first.sourceChannel], frames * sizeof(Sample));
        } else {
            const Sample* s = in[first.sourceChannel];
            for (size_t i = 0; i < frames; ++i) dst[i] = first.factor * s[i];
        }

        for (size_t j = 1; j < a.sources.size(); ++j) {
            const CopySummand& s = a.sources[j];
            if (s.sourceChannel < 0) {
                for (size_t i = 0; i < frames; ++i) dst[i] += s.factor;
            } else if (s.factor == 1.0f) {
                const Sample* src = in[s.sourceChannel];
                for (size_t i = 0; i < frames; ++i) dst[i] += src[i];
            } else {
                const Sample* src = in[s.sourceChannel];
                for (size_t i = 0; i < frames; ++i) dst[i] += s.factor * src[i];
            }
        }
    }
}

GraphicEqFilter::GraphicEqFilter(std::vector<EqNode> nodes, size_t filterLength)
    : nodes_(std::move(nodes)),
      filterLength_(filterLength == 0 ? 16384 : std::min(filterLength, kMaxImpulseTaps)) {}

void GraphicEqFilter::prepare(double sampleRate, size_t maxFrames, size_t channels) {
    (void)maxFrames;
    GainInterpolator interp(nodes_);
    auto taps = designMinimumPhaseFir(filterLength_, sampleRate, [&](double f) {
        return std::pow(10.0, interp.gainAt(f) / 20.0);
    });

    // One kernel for every channel: the impulse is the same, so the spectra are
    // computed and stored once instead of once per channel.
    auto kernel = std::make_shared<FirKernel>(std::move(taps), kConvolverBlock);
    convolvers_.clear();
    for (size_t i = 0; i < channels; ++i)
        convolvers_.push_back(std::make_unique<FirConvolver>(kernel));

    scratchIn_.assign(kConvolverBlock, 0.0);
    scratchOut_.assign(kConvolverBlock, 0.0);
}

void GraphicEqFilter::reset() {
    for (auto& c : convolvers_) c->reset();
}

void GraphicEqFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                              size_t channels) {
    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(kConvolverBlock, frames - done);
        for (size_t c = 0; c < channels; ++c) {
            for (size_t i = 0; i < n; ++i) scratchIn_[i] = in[c][done + i];
            convolvers_[c]->process(scratchIn_.data(), scratchOut_.data(), n);
            for (size_t i = 0; i < n; ++i) out[c][done + i] = static_cast<Sample>(scratchOut_[i]);
        }
        done += n;
    }
}

void ConvolutionFilter::prepare(double sampleRate, size_t maxFrames, size_t channels) {
    (void)sampleRate;
    (void)maxFrames;
    const size_t distinct = tapsPerChannel_.size();

    // Build one kernel per distinct impulse and share it across the channels
    // that use it, so a mono impulse applied to every channel is not expanded
    // into one copy per channel. No impulses means no convolvers, which matches
    // a filter that was constructed with nothing to apply.
    std::vector<std::shared_ptr<const FirKernel>> kernels(distinct);
    convolvers_.clear();
    if (distinct == 0) return;

    for (size_t i = 0; i < channels; ++i) {
        const size_t k = distinct == 1 ? 0 : i % distinct;
        if (!kernels[k])
            kernels[k] = std::make_shared<FirKernel>(tapsPerChannel_[k], kConvolverBlock);
        convolvers_.push_back(std::make_unique<FirConvolver>(kernels[k]));
    }
    scratchIn_.assign(kConvolverBlock, 0.0);
    scratchOut_.assign(kConvolverBlock, 0.0);
}

void ConvolutionFilter::reset() {
    for (auto& c : convolvers_) c->reset();
}

void ConvolutionFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                                size_t channels) {
    // No impulses to apply: the filter is an identity, and inPlace() is true, so
    // the input buffers already hold the correct output.
    if (convolvers_.empty()) return;

    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(kConvolverBlock, frames - done);
        for (size_t c = 0; c < channels; ++c) {
            for (size_t i = 0; i < n; ++i) scratchIn_[i] = in[c][done + i];
            convolvers_[c]->process(scratchIn_.data(), scratchOut_.data(), n);
            for (size_t i = 0; i < n; ++i) out[c][done + i] = static_cast<Sample>(scratchOut_[i]);
        }
        done += n;
    }
}

CompressorFilter::CompressorFilter(double thresholdDb, double ratio, double attackMs,
                                   double releaseMs, double makeupDb, double kneeDb,
                                   double limitDb)
    : thresholdDb_(thresholdDb),
      ratio_(ratio),
      attackMs_(attackMs),
      releaseMs_(releaseMs),
      makeupDb_(makeupDb),
      kneeDb_(kneeDb),
      limitDb_(limitDb) {}

void CompressorFilter::prepare(double sampleRate, size_t maxFrames, size_t channels) {
    (void)maxFrames;
    const double rate = sampleRate > 0 ? sampleRate : 48000.0;
    threshold_ = static_cast<float>(thresholdDb_);
    makeup_ = static_cast<float>(std::pow(10.0, makeupDb_ / 20.0));
    limit_ = static_cast<float>(std::pow(10.0, limitDb_ / 20.0));
    slope_ = static_cast<float>(ratio_ > 1.0 ? 1.0 - 1.0 / ratio_ : 0.0);
    attackCoeff_ = static_cast<float>(std::exp(-1.0 / (rate * std::max(0.01, attackMs_) / 1000.0)));
    releaseCoeff_ =
        static_cast<float>(std::exp(-1.0 / (rate * std::max(0.01, releaseMs_) / 1000.0)));
    envelope_.assign(channels, 0.0f);
}

void CompressorFilter::reset() { std::fill(envelope_.begin(), envelope_.end(), 0.0f); }

void CompressorFilter::process(const Sample* const* in, Sample* const* out, size_t frames,
                               size_t channels) {
    const float halfKnee = static_cast<float>(kneeDb_ * 0.5);
    for (size_t c = 0; c < channels; ++c) {
        const Sample* src = in[c];
        Sample* dst = out[c];
        float env = envelope_[c];

        for (size_t i = 0; i < frames; ++i) {
            const float x = src[i];
            const float level = std::fabs(x);

            // Peak detector with separate attack/release smoothing.
            const float coeff = level > env ? attackCoeff_ : releaseCoeff_;
            env = coeff * env + (1.0f - coeff) * level;

            // Convert the envelope to dB and compute the gain reduction. The
            // soft knee spreads the transition over +/- halfKnee.
            const float levelDb = 20.0f * std::log10(env + 1e-9f);
            float reductionDb = 0.0f;
            const float over = levelDb - threshold_;
            if (over > halfKnee) {
                reductionDb = slope_ * over;
            } else if (over > -halfKnee && kneeDb_ > 0.0) {
                const float t = over + halfKnee;
                reductionDb = slope_ * (t * t) / (2.0f * static_cast<float>(kneeDb_));
            }

            // exp2(x * -log2(10)/20) is the same as pow(10, -x/20), but a single
            // instruction instead of a library call.
            float gain = std::exp2(reductionDb * kNegLog2TenOver20) * makeup_;

            // Safety limiter keeps the output below the requested ceiling.
            const float peak = std::fabs(x) * gain;
            if (peak > limit_) gain *= limit_ / peak;

            dst[i] = x * gain;
        }
        envelope_[c] = env;
    }
}

}  // namespace apolite