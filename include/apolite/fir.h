// ApoLite - minimum-phase FIR design and partitioned convolution.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "apolite/fft.h"

namespace apolite {

// Design a minimum-phase FIR from a magnitude response sampled on a uniform
// frequency grid. `magAt` is called with a frequency in Hz and must return a
// linear magnitude. Uses the real-cepstrum method: the impulse response is
// windowed with a raised cosine and truncated to `length` taps.
std::vector<double> designMinimumPhaseFir(size_t length, double sampleRate,
                                          const std::function<double(double)>& magAt);

// Default ceiling on the heap the impulse convolvers in one configuration may
// hold, in bytes. This bounds memory rather than tap count, because memory is
// what a long file actually costs and what varies with the channel count; a tap
// count would be an arbitrary number that means different things for a mono and
// a four-channel device. 256 MB is far above any real impulse response (a 1 s
// stereo response is about 3 MB) and only refuses a file that would exhaust the
// host process. A caller may raise it for a deliberate long impulse.
constexpr size_t kImpulseMemoryBudgetBytes = size_t{256} * 1024 * 1024;

// Hard ceiling on one impulse's spectra, in bytes, whatever the configuration
// budget is set to. This is an overflow guard rather than a policy limit: a
// single FirKernel refuses to build past it, so no configuration can make the
// process allocate without bound even if the budget is raised.
constexpr size_t kMaxFirKernelBytes = size_t{2} * 1024 * 1024 * 1024;

// Longest impulse that fits the budget for `channels` channels at `blockSize`,
// with `distinctKernels` different impulses among them. The inverse of
// firMemoryBytes: a caller that knows the channel count uses this to reject a
// file from its header, before any samples are read. Returns 0 when even one tap
// does not fit, which cannot happen with the default budget.
size_t maxImpulseTapsForBudget(size_t channels, size_t budgetBytes = kImpulseMemoryBudgetBytes,
                               size_t blockSize = 256, size_t distinctKernels = 1);

// Heap one kernel's spectra need, in bytes. The per-channel delay lines are not
// included; see firMemoryBytes for the whole cost.
size_t firKernelBytes(size_t taps, size_t blockSize = 256);

// Estimated share of one CPU core that convolving `taps` taps on `channels`
// channels costs, as a percentage of real time at `sampleRate`. Advisory only:
// the cost depends on the machine, so it is reported to the user rather than
// used as a hard limit. A figure near or above 100 means the audio thread cannot
// keep up and the host will drop samples.
double impulseRealtimePercent(size_t taps, size_t channels, double sampleRate);

// Approximate heap the convolver holds, in bytes, for `taps` taps applied to
// `channels` channels at `blockSize`. A mono impulse applied to every channel is
// expanded once and shared, so the spectra are counted once; a stereo file holds
// `distinctKernels` spectra. The per-channel delay lines are counted for every
// channel. Lets a caller log or bound the cost before allocating.
size_t firMemoryBytes(size_t taps, size_t channels, size_t blockSize = 256,
                      size_t distinctKernels = 1);

// The impulse spectra for one impulse, immutable once built. Every channel that
// convolves with the same impulse shares one of these, so the spectra are
// computed and stored once instead of once per channel.
class FirKernel {
public:
    // Throws std::length_error if the spectra would exceed `maxBytes`, which
    // defaults to kMaxFirKernelBytes. The bound is an overflow guard, not a
    // policy limit; the caller decides how long an impulse may be, using
    // firMemoryBytes and its own budget. `maxBytes` is exposed so a caller can
    // hold a kernel to a tighter limit of its own.
    FirKernel(std::vector<double> taps, size_t blockSize = 256,
              size_t maxBytes = kMaxFirKernelBytes);

    size_t blockSize() const { return block_; }
    size_t tapCount() const { return taps_; }
    size_t partitions() const { return partitions_; }
    size_t bins() const { return bins_; }

    // The spectrum of one partition, `bins()` complex values.
    const cplx* spectrum(size_t partition) const { return h_[partition].data(); }

private:
    size_t block_ = 0;
    size_t fftSize_ = 0;
    size_t bins_ = 0;
    size_t taps_ = 0;
    size_t partitions_ = 0;

    RealFft fft_;
    std::vector<std::vector<cplx>> h_;  // per-partition spectra
};

// Uniform-partitioned overlap-save convolver (frequency-domain delay line).
// Latency is one block, so prefer a small block size for live use.
//
// `process` accepts any sample count: a call may ask for fewer samples than one
// block, or for many blocks at once.
class FirConvolver {
public:
    // Builds a private kernel. Throws std::length_error if the impulse is longer
    // than kMaxFirKernelBytes allows.
    FirConvolver(std::vector<double> taps, size_t blockSize = 256);

    // Shares a kernel that was built once for several channels.
    explicit FirConvolver(std::shared_ptr<const FirKernel> kernel);

    // Process `n` samples in place-friendly fashion. `in` and `out` may alias.
    void process(const double* in, double* out, size_t n);

    void reset();

    size_t blockSize() const { return block_; }
    size_t tapCount() const { return taps_; }

private:
    // Transform the current input block into outBuf_. When `commit` is false the
    // block is incomplete: the transform is computed against a zero-padded tail
    // and the pipeline state is left untouched, so the same block can be
    // transformed again as more input arrives. Outputs for the filled part are
    // already final, because the padding cannot influence them.
    void transformBlock(bool commit);

    std::shared_ptr<const FirKernel> kernel_;
    size_t block_ = 0;
    size_t fftSize_ = 0;
    size_t bins_ = 0;
    size_t taps_ = 0;
    size_t partitions_ = 0;

    RealFft fft_;
    std::vector<std::vector<cplx>> fdl_;  // frequency-domain delay line
    size_t fdlPos_ = 0;

    std::vector<double> inBuf_;   // input of the block being filled, length block_
    size_t inFill_ = 0;
    std::vector<double> outBuf_;  // output of the block just computed

    // How many outputs of the current block have been returned. A partial block
    // is transformed on each call so its outputs can be returned immediately;
    // because the transform is not committed, they are recomputed when the block
    // completes, and this says how many to skip.
    size_t blockEmitted_ = 0;

    // Scratch for an uncommitted transform, so it does not allocate. Sized in
    // the constructor because the audio thread must not allocate.
    std::vector<cplx> savedFdlEntry_;

    std::vector<double> prev_;     // last committed block of input
    std::vector<double> timeBuf_;  // 2*block_
    std::vector<cplx> freqBuf_;    // bins_
    std::vector<cplx> acc_;        // bins_
};

}  // namespace apolite
