// ApoLite - filter interface and concrete filters.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "apolite/biquad.h"
#include "apolite/fir.h"
#include "apolite/gain.h"

namespace apolite {

// Sample buffers are 32-bit float, matching the Windows audio engine format.
using Sample = float;

// A single processing stage. Implementations must be real-time safe once
// prepare() has returned: process() must not allocate, lock, or throw.
class IFilter {
public:
    virtual ~IFilter() = default;

    // Called when the sample rate, block size or channel count changes.
    virtual void prepare(double sampleRate, size_t maxFrames, size_t channels) = 0;

    // Clear internal state (delay lines, filter memories).
    virtual void reset() {}

    // In-place filters may write directly into the input buffers.
    virtual bool inPlace() const { return true; }

    // `in` and `out` each hold `channels` pointers.
    virtual void process(const Sample* const* in, Sample* const* out, size_t frames,
                         size_t channels) = 0;
};

// Applies a constant gain (used for Preamp).
class PreampFilter : public IFilter {
public:
    explicit PreampFilter(double gainDb) : gainDb_(gainDb) {}
    void prepare(double sampleRate, size_t maxFrames, size_t channels) override;
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    double gainDb_;
    float gain_ = 1.0f;
};

// A single biquad applied independently to each channel.
class BiquadFilter : public IFilter {
public:
    BiquadFilter(FilterType type, double gainDb, double freq, double q, ParamKind kind,
                 bool isCornerFreq = false);

    void prepare(double sampleRate, size_t maxFrames, size_t channels) override;
    void reset() override;
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    FilterType type_;
    double gainDb_;
    double freq_;
    double q_;
    ParamKind kind_;
    bool isCornerFreq_;
    std::vector<Biquad> biquads_;
};

// Generic IIR of arbitrary order (transposed direct form II), per channel.
class IirFilter : public IFilter {
public:
    // Coefficients: b0..bm followed by a0..am, length 2*(order+1).
    explicit IirFilter(std::vector<double> coefficients);

    void prepare(double sampleRate, size_t maxFrames, size_t channels) override;
    void reset() override;
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    std::vector<double> b_;
    std::vector<double> a_;  // normalized so a[0] == 1
    std::vector<std::vector<double>> state_;
};

// Integer delay line, per channel.
class DelayFilter : public IFilter {
public:
    DelayFilter(double delay, bool isMs) : delay_(delay), isMs_(isMs) {}

    void prepare(double sampleRate, size_t maxFrames, size_t channels) override;
    void reset() override;
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    double delay_;
    bool isMs_;
    size_t delaySamples_ = 0;
    std::vector<std::vector<Sample>> lines_;
    size_t pos_ = 0;
};

// A simple feed-forward dynamics processor with a soft knee. It doubles as a
// compressor and a brick-wall limiter depending on ratio and threshold, which
// is what voice boosting for conferencing needs: raise quiet speech without
// clipping loud passages.
class CompressorFilter : public IFilter {
public:
    // thresholdDb: level where gain reduction begins.
    // ratio: compression ratio (large values approach limiting).
    // attackMs/releaseMs: envelope times.
    // makeupDb: static gain applied after compression.
    // kneeDb: soft-knee width centred on the threshold.
    CompressorFilter(double thresholdDb, double ratio, double attackMs, double releaseMs,
                     double makeupDb, double kneeDb, double limitDb);

    void prepare(double sampleRate, size_t maxFrames, size_t channels) override;
    void reset() override;
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    double thresholdDb_;
    double ratio_;
    double attackMs_;
    double releaseMs_;
    double makeupDb_;
    double kneeDb_;
    double limitDb_;

    float threshold_ = 0.0f;
    float makeup_ = 1.0f;
    float limit_ = 1.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    float slope_ = 1.0f;  // 1 - 1/ratio
    std::vector<float> envelope_;
};

// Channel mixer / router: replaces a target channel with a weighted sum of
// sources, mirroring Equalizer APO's Copy command.
struct CopySummand {
    int sourceChannel = -1;  // -1 means a constant value
    float factor = 1.0f;
};

struct CopyAssignment {
    int targetChannel = -1;
    std::vector<CopySummand> sources;
};

class CopyFilter : public IFilter {
public:
    explicit CopyFilter(std::vector<CopyAssignment> assignments)
        : assignments_(std::move(assignments)) {}

    bool inPlace() const override { return false; }
    void prepare(double, size_t, size_t) override {}
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    std::vector<CopyAssignment> assignments_;
};

// GraphicEQ: a minimum-phase FIR built from interpolated node gains.
class GraphicEqFilter : public IFilter {
public:
    GraphicEqFilter(std::vector<EqNode> nodes, size_t filterLength = 16384);

    void prepare(double sampleRate, size_t maxFrames, size_t channels) override;
    void reset() override;
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    std::vector<EqNode> nodes_;
    size_t filterLength_;
    std::vector<std::unique_ptr<FirConvolver>> convolvers_;
    std::vector<double> scratchIn_;
    std::vector<double> scratchOut_;
};

// Convolution with an impulse response. `tapsPerChannel` may hold a single
// entry, in which case it is applied to every channel, or one entry per
// channel (multi-channel impulse responses are assigned round-robin).
class ConvolutionFilter : public IFilter {
public:
    explicit ConvolutionFilter(std::vector<std::vector<double>> tapsPerChannel)
        : tapsPerChannel_(std::move(tapsPerChannel)) {}

    void prepare(double sampleRate, size_t maxFrames, size_t channels) override;
    void reset() override;
    void process(const Sample* const* in, Sample* const* out, size_t frames,
                 size_t channels) override;

private:
    std::vector<std::vector<double>> tapsPerChannel_;
    std::vector<std::unique_ptr<FirConvolver>> convolvers_;
    std::vector<double> scratchIn_;
    std::vector<double> scratchOut_;
};

}  // namespace apolite
