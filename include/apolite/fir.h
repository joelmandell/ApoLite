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

// Longest impulse the convolver accepts, 2^18 taps, about 5.5 s at 48 kHz and
// far longer than room correction needs. The partitioned convolver holds roughly
// eight times the impulse length, and it holds that once for the shared kernel,
// so this bounds a kernel at about 4 MB instead of letting a long file exhaust
// memory. A longer file is rejected rather than truncated, so the user sees a
// clear error instead of a filter that quietly does not match the file.
constexpr size_t kMaxImpulseTaps = size_t{1} << 18;

// Approximate heap the convolver holds, in bytes, for `taps` taps applied to
// `channels` channels at `blockSize`. The impulse spectra are shared by every
// channel, so they are counted once, which is the point: without sharing the
// per-channel part would include a whole second copy of the spectra. Lets a
// caller log or bound the cost before allocating.
size_t firMemoryBytes(size_t taps, size_t channels, size_t blockSize = 256);

// The impulse spectra for one impulse, immutable once built. Every channel that
// convolves with the same impulse shares one of these, so the spectra are
// computed and stored once instead of once per channel.
class FirKernel {
public:
    // Throws std::length_error if `taps` is longer than kMaxImpulseTaps.
    FirKernel(std::vector<double> taps, size_t blockSize = 256);

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
    // than kMaxImpulseTaps.
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
