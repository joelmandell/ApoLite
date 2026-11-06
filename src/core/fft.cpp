// ApoLite - portable FFT implementation.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/fft.h"

#include <cmath>

#include "apolite/mathutil.h"

namespace apolite {

size_t nextPow2(size_t v) {
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

ComplexFft::ComplexFft(size_t n) : n_(n) {
    unsigned bits = 0;
    while ((size_t{1} << bits) < n) ++bits;
    logN_ = bits;

    twiddle_.resize(n / 2);
    for (size_t k = 0; k < n / 2; ++k) {
        const double ang = -kTwoPi * static_cast<double>(k) / static_cast<double>(n);
        twiddle_[k] = cplx(std::cos(ang), std::sin(ang));
    }

    rev_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        size_t x = i, r = 0;
        for (unsigned b = 0; b < logN_; ++b) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        rev_[i] = r;
    }
}

void ComplexFft::forward(cplx* data) const {
    const size_t n = n_;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = rev_[i];
        if (j > i) std::swap(data[i], data[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len >> 1;
        const size_t step = n / len;
        for (size_t i = 0; i < n; i += len) {
            for (size_t k = 0; k < half; ++k) {
                const cplx w = twiddle_[k * step];
                const cplx u = data[i + k];
                const cplx v = data[i + k + half] * w;
                data[i + k] = u + v;
                data[i + k + half] = u - v;
            }
        }
    }
}

void ComplexFft::inverse(cplx* data) const {
    const size_t n = n_;
    for (size_t i = 0; i < n; ++i) data[i] = std::conj(data[i]);
    forward(data);
    const double s = 1.0 / static_cast<double>(n);
    for (size_t i = 0; i < n; ++i) data[i] = std::conj(data[i]) * s;
}

RealFft::RealFft(size_t n) : n_(n), half_(n / 2) {
    scratch_.resize(n / 2);
}

void RealFft::forward(const double* in, cplx* out) const {
    const size_t m = n_ / 2;
    cplx* z = scratch_.data();
    for (size_t i = 0; i < m; ++i) z[i] = cplx(in[2 * i], in[2 * i + 1]);
    half_.forward(z);

    const cplx dc = z[0];
    out[0] = cplx(dc.real() + dc.imag(), 0.0);
    out[m] = cplx(dc.real() - dc.imag(), 0.0);

    for (size_t k = 1; k < m; ++k) {
        const cplx a = z[k];
        const cplx b = std::conj(z[m - k]);
        const double ang = -kTwoPi * static_cast<double>(k) / static_cast<double>(n_);
        const cplx w(std::cos(ang), std::sin(ang));
        const cplx even = (a + b) * 0.5;
        const cplx odd = (a - b) * cplx(0.0, -0.5);
        out[k] = even + w * odd;
    }
}

void RealFft::inverse(const cplx* in, double* out) const {
    const size_t m = n_ / 2;
    cplx* z = scratch_.data();

    // E[0] = (X[0] + X[m]) / 2, O[0] = (X[0] - X[m]) / 2.
    const double x0 = in[0].real();
    const double xm = in[m].real();
    z[0] = cplx(0.5 * (x0 + xm), 0.5 * (x0 - xm));
    for (size_t k = 1; k < m; ++k) {
        const cplx a = in[k];
        const cplx b = std::conj(in[m - k]);
        const double ang = -kTwoPi * static_cast<double>(k) / static_cast<double>(n_);
        const cplx w(std::cos(ang), std::sin(ang));
        // X[k] = E[k] + W^k O[k]  =>  Z[k] = E[k] + i O[k]
        const cplx even = (a + b) * 0.5;
        const cplx odd = (a - b) * cplx(0.0, 0.5) * std::conj(w);
        z[k] = even + odd;
    }
    half_.inverse(z);
    for (size_t i = 0; i < m; ++i) {
        out[2 * i] = z[i].real();
        out[2 * i + 1] = z[i].imag();
    }
}

}  // namespace apolite
