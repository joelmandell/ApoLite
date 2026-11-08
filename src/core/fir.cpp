// ApoLite - minimum-phase FIR design and partitioned convolution.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/fir.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "apolite/mathutil.h"

namespace apolite {

std::vector<double> designMinimumPhaseFir(size_t length, double sampleRate,
                                          const std::function<double(double)>& magAt) {
    const size_t n2 = length * 2;  // full FFT length
    RealFft fft(n2);
    const size_t bins = fft.bins();

    const double threshold = std::pow(10.0, -100.0 / 20.0);

    // Real log-magnitude spectrum, symmetric by construction.
    std::vector<cplx> logMag(bins);
    for (size_t k = 0; k < bins; ++k) {
        const double f = static_cast<double>(k) * sampleRate / static_cast<double>(n2);
        double mag = magAt(f);
        if (!(mag > threshold)) mag = threshold;
        logMag[k] = cplx(std::log(mag), 0.0);
    }

    // Real cepstrum.
    std::vector<double> cepstrum(n2);
    fft.inverse(logMag.data(), cepstrum.data());

    // Minimum-phase folding: keep c[0], double the causal part, drop the rest.
    const size_t half = n2 / 2;
    std::vector<double> folded(n2, 0.0);
    folded[0] = cepstrum[0];
    for (size_t n = 1; n < half; ++n) folded[n] = 2.0 * cepstrum[n];
    folded[half] = cepstrum[half];

    std::vector<cplx> logSpec(bins);
    fft.forward(folded.data(), logSpec.data());

    // Exponentiate to obtain the complex minimum-phase spectrum.
    std::vector<cplx> spec(bins);
    for (size_t k = 0; k < bins; ++k) spec[k] = std::exp(logSpec[k]);

    std::vector<double> ir(n2);
    fft.inverse(spec.data(), ir.data());

    // Raised-cosine window over the first half, then truncate.
    std::vector<double> out(length);
    for (size_t i = 0; i < length; ++i) {
        const double factor =
            0.5 * (1.0 + std::cos(kTwoPi * static_cast<double>(i) / static_cast<double>(n2)));
        out[i] = ir[i] * factor;
    }
    return out;
}

FirConvolver::FirConvolver(std::vector<double> taps, size_t blockSize)
    : block_(blockSize ? blockSize : 1),
      fftSize_(block_ * 2),
      taps_(taps.size()),
      fft_(block_ * 2),
      inBuf_(blockSize ? blockSize : 1, 0.0),
      outBuf_(blockSize ? blockSize : 1, 0.0),
      prev_(blockSize ? blockSize : 1, 0.0),
      timeBuf_(block_ * 2, 0.0),
      freqBuf_(bins_, cplx(0.0, 0.0)),
      acc_(bins_, cplx(0.0, 0.0)) {
    bins_ = fft_.bins();
    freqBuf_.assign(bins_, cplx(0.0, 0.0));
    acc_.assign(bins_, cplx(0.0, 0.0));

    partitions_ = (taps_ + block_ - 1) / block_;
    if (partitions_ == 0) partitions_ = 1;

    h_.assign(partitions_, std::vector<cplx>(bins_, cplx(0.0, 0.0)));
    std::vector<double> padded(fftSize_, 0.0);
    for (size_t p = 0; p < partitions_; ++p) {
        std::fill(padded.begin(), padded.end(), 0.0);
        for (size_t i = 0; i < block_; ++i) {
            const size_t idx = p * block_ + i;
            if (idx < taps_) padded[i] = taps[idx];
        }
        fft_.forward(padded.data(), h_[p].data());
    }

    fdl_.assign(partitions_, std::vector<cplx>(bins_, cplx(0.0, 0.0)));
    savedFdlEntry_.assign(bins_, cplx(0.0, 0.0));
    fdlPos_ = 0;

    // Prime the overlap-save pipeline with one zero block, so the first real
    // block produces conv(x, h)[0..block-1] with no added latency.
    transformBlock(true);
    inFill_ = 0;
    blockEmitted_ = 0;
}

void FirConvolver::reset() {
    std::fill(inBuf_.begin(), inBuf_.end(), 0.0);
    std::fill(outBuf_.begin(), outBuf_.end(), 0.0);
    std::fill(prev_.begin(), prev_.end(), 0.0);
    for (auto& v : fdl_) std::fill(v.begin(), v.end(), cplx(0.0, 0.0));
    inFill_ = 0;
    fdlPos_ = 0;
    blockEmitted_ = 0;
    transformBlock(true);
    inFill_ = 0;
    blockEmitted_ = 0;
}

void FirConvolver::transformBlock(bool commit) {
    // The last block of input is prepended, so this is overlap-save over
    // [prev | current]. An incomplete block is zero-padded to full length; the
    // padding sits at indices >= inFill_ and cannot affect earlier outputs, so
    // those outputs are final even though the transform is not committed.
    for (size_t i = 0; i < block_; ++i) timeBuf_[i] = prev_[i];
    for (size_t i = 0; i < block_; ++i) timeBuf_[block_ + i] = inBuf_[i];
    fft_.forward(timeBuf_.data(), freqBuf_.data());

    // The delay line advances by one slot per block. For an uncommitted block
    // the write is undone afterwards, so the next call sees the same state.
    const size_t nextPos = (fdlPos_ + partitions_ - 1) % partitions_;
    if (!commit) savedFdlEntry_ = fdl_[nextPos];
    fdl_[nextPos] = freqBuf_;
    fdlPos_ = nextPos;

    std::fill(acc_.begin(), acc_.end(), cplx(0.0, 0.0));
    // Accumulate as plain real arithmetic over interleaved [re, im] pairs.
    // std::complex's operator* carries NaN and signed-zero handling that stops
    // the compiler vectorizing the loop; this form vectorizes and is the same
    // complex product. The standard guarantees a complex<double> array is
    // layout-compatible with an array of two doubles.
    double* acc = reinterpret_cast<double*>(acc_.data());
    size_t idx = fdlPos_;
    for (size_t p = 0; p < partitions_; ++p) {
        const double* x = reinterpret_cast<const double*>(fdl_[idx].data());
        const double* hh = reinterpret_cast<const double*>(h_[p].data());
        for (size_t k = 0; k < bins_; ++k) {
            const double xr = x[2 * k], xi = x[2 * k + 1];
            const double hr = hh[2 * k], hi = hh[2 * k + 1];
            acc[2 * k] += xr * hr - xi * hi;
            acc[2 * k + 1] += xr * hi + xi * hr;
        }
        idx = (idx + 1) % partitions_;
    }

    fft_.inverse(acc_.data(), timeBuf_.data());
    for (size_t i = 0; i < block_; ++i) outBuf_[i] = timeBuf_[block_ + i];

    if (commit) {
        std::copy(inBuf_.begin(), inBuf_.end(), prev_.begin());
        // The block is consumed, so clear it. A later partial block must be
        // padded with silence rather than stale samples.
        std::fill(inBuf_.begin(), inBuf_.end(), 0.0);
    } else {
        fdl_[fdlPos_] = savedFdlEntry_;
        fdlPos_ = (fdlPos_ + 1) % partitions_;
    }
}

void FirConvolver::process(const double* in, double* out, size_t n) {
    // Output is produced for exactly the samples consumed, so convolution adds
    // no latency, and the call may be any length.
    //
    // The previous version advanced in whole blocks only, so a call whose length
    // was not a multiple of the block size looped forever: GraphicEqFilter
    // chunks its input at 256 frames, so a 480-frame host block hung the audio
    // thread. An incomplete block is now transformed with a zero-padded tail,
    // which yields final output for the samples that are present.
    size_t done = 0;
    while (done < n) {
        const size_t take = std::min(block_ - inFill_, n - done);
        std::copy(in + done, in + done + take, inBuf_.begin() + inFill_);
        inFill_ += take;
        done += take;

        const bool full = inFill_ == block_;
        transformBlock(full);

        // Out[blockEmitted_, inFill_) is final either way, and the count is
        // exactly `take`. `blockEmitted_` then tracks how much of this block has
        // been returned, for the case where the block completes later.
        std::copy(outBuf_.begin() + blockEmitted_, outBuf_.begin() + inFill_, out + done - take);

        if (full) {
            inFill_ = 0;
            blockEmitted_ = 0;
        } else {
            blockEmitted_ = inFill_;
        }
    }
}

}  // namespace apolite
