// ApoLite - portable FFT (real + complex, radix-2, iterative).
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <complex>
#include <vector>

namespace apolite {

using cplx = std::complex<double>;

// In-place iterative radix-2 complex FFT. `n` must be a power of two.
class ComplexFft {
public:
    explicit ComplexFft(size_t n);

    size_t size() const { return n_; }

    void forward(cplx* data) const;
    void inverse(cplx* data) const;  // includes 1/n scaling

private:
    size_t n_ = 0;
    unsigned logN_ = 0;
    std::vector<cplx> twiddle_;  // n/2 entries: exp(-2*pi*i*k/n)
    std::vector<size_t> rev_;
};

// Real-input FFT built on the complex transform (n real samples -> n/2+1 bins).
// Output layout is the standard half-spectrum: bin 0 (DC) .. bin n/2 (Nyquist),
// both purely real.
class RealFft {
public:
    explicit RealFft(size_t n);  // n must be a power of two, n >= 4

    size_t size() const { return n_; }
    size_t bins() const { return n_ / 2 + 1; }

    void forward(const double* in, cplx* out) const;
    void inverse(const cplx* in, double* out) const;  // includes 1/n scaling

private:
    size_t n_ = 0;
    ComplexFft half_;
    mutable std::vector<cplx> scratch_;
};

// Next power of two >= v (minimum 1).
size_t nextPow2(size_t v);

}  // namespace apolite
