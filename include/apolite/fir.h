// ApoLite - minimum-phase FIR design and partitioned convolution.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "apolite/fft.h"

namespace apolite {

// Design a minimum-phase FIR from a magnitude response sampled on a uniform
// frequency grid. `magAt` is called with a frequency in Hz and must return a
// linear magnitude. Uses the real-cepstrum method: the impulse response is
// windowed with a raised cosine and truncated to `length` taps.
std::vector<double> designMinimumPhaseFir(size_t length, double sampleRate,
                                          const std::function<double(double)>& magAt);

// Uniform-partitioned overlap-save convolver (frequency-domain delay line).
// Latency is one block, so prefer a small block size for live use.
class FirConvolver {
public:
    FirConvolver(std::vector<double> taps, size_t blockSize = 256);

    // Process `n` samples in place-friendly fashion. `in` and `out` may alias.
    void process(const double* in, double* out, size_t n);

    void reset();

    size_t blockSize() const { return block_; }
    size_t tapCount() const { return taps_; }

private:
    void processBlock();

    size_t block_ = 0;
    size_t fftSize_ = 0;
    size_t bins_ = 0;
    size_t taps_ = 0;
    size_t partitions_ = 0;

    RealFft fft_;
    std::vector<std::vector<cplx>> h_;    // per-partition spectra
    std::vector<std::vector<cplx>> fdl_;  // frequency-domain delay line
    size_t fdlPos_ = 0;

    std::vector<double> inBuf_;   // pending input, length block_
    std::vector<double> outBuf_;  // pending output, length block_
    size_t inFill_ = 0;
    size_t outFill_ = 0;
    size_t outRead_ = 0;

    std::vector<double> prev_;     // last block of input
    std::vector<double> timeBuf_;  // 2*block_
    std::vector<cplx> freqBuf_;    // bins_
    std::vector<cplx> acc_;        // bins_
};

}  // namespace apolite
