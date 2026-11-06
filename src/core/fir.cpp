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
    fdlPos_ = 0;

    // Prime the overlap-save pipeline with one zero block so that the first
    // real block produces conv(x, h)[0..block-1].
    processBlock();
    inFill_ = 0;
    outFill_ = 0;
    outRead_ = 0;
}

void FirConvolver::reset() {
    std::fill(inBuf_.begin(), inBuf_.end(), 0.0);
    std::fill(outBuf_.begin(), outBuf_.end(), 0.0);
    std::fill(prev_.begin(), prev_.end(), 0.0);
    for (auto& v : fdl_) std::fill(v.begin(), v.end(), cplx(0.0, 0.0));
    inFill_ = outFill_ = outRead_ = 0;
    fdlPos_ = 0;
    processBlock();
    inFill_ = 0;
    outFill_ = 0;
    outRead_ = 0;
}

void FirConvolver::processBlock() {
    // Build [prev | current] and transform.
    for (size_t i = 0; i < block_; ++i) timeBuf_[i] = prev_[i];
    for (size_t i = 0; i < block_; ++i) timeBuf_[block_ + i] = inBuf_[i];
    fft_.forward(timeBuf_.data(), freqBuf_.data());

    fdlPos_ = (fdlPos_ + partitions_ - 1) % partitions_;
    fdl_[fdlPos_] = freqBuf_;

    std::fill(acc_.begin(), acc_.end(), cplx(0.0, 0.0));
    size_t idx = fdlPos_;
    for (size_t p = 0; p < partitions_; ++p) {
        const auto& x = fdl_[idx];
        const auto& hh = h_[p];
        for (size_t k = 0; k < bins_; ++k) acc_[k] += x[k] * hh[k];
        idx = (idx + 1) % partitions_;
    }

    std::vector<double> y(fftSize_);
    fft_.inverse(acc_.data(), y.data());
    for (size_t i = 0; i < block_; ++i) outBuf_[i] = y[block_ + i];

    for (size_t i = 0; i < block_; ++i) prev_[i] = inBuf_[i];
}

void FirConvolver::process(const double* in, double* out, size_t n) {
    // The pipeline is primed with one zero block, so output is produced at the
    // same rate as input is consumed and output[i] == conv(x, h)[i - block_].
    size_t inPos = 0;
    size_t outPos = 0;
    while (outPos < n) {
        if (outRead_ < outFill_) {
            const size_t take = std::min(outFill_ - outRead_, n - outPos);
            std::copy(outBuf_.begin() + outRead_, outBuf_.begin() + outRead_ + take,
                      out + outPos);
            outRead_ += take;
            outPos += take;
            if (outRead_ == outFill_) outRead_ = outFill_ = 0;
            continue;
        }

        const size_t take = std::min(block_ - inFill_, n - inPos);
        std::copy(in + inPos, in + inPos + take, inBuf_.begin() + inFill_);
        inFill_ += take;
        inPos += take;

        if (inFill_ == block_) {
            processBlock();
            inFill_ = 0;
            outFill_ = block_;
            outRead_ = 0;
        }
    }
}

}  // namespace apolite
