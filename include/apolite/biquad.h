// ApoLite - biquad filter design and runtime.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// The coefficient formulas are the public-domain "Audio EQ Cookbook" equations
// by Robert Bristow-Johnson. Parameter interpretation (Q vs BW vs slope, and the
// DCX2496 corner-frequency adjustment) follows the Equalizer APO documentation.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace apolite {

enum class FilterType {
    LowPass,
    HighPass,
    BandPass,
    Notch,
    AllPass,
    Peaking,
    LowShelf,
    HighShelf,
};

// Transposed direct form II biquad, double precision internal state.
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

// How the third Filter parameter is interpreted.
enum class ParamKind {
    Q,          // "Q <v>"
    Bandwidth,  // "BW Oct <v>"
    ShelfSlope  // "LSC/HS <x> dB" slope, or the default slope
};

class Biquad {
public:
    Biquad() = default;

    // Design a biquad. `q` is interpreted according to `kind`. When
    // `isCornerFreq` is set for a shelf filter, `freq` is a corner frequency
    // and is converted to the center frequency using the DCX2496 rule.
    static Biquad design(FilterType type, double gainDb, double freq, double sampleRate, double q,
                         ParamKind kind, bool isCornerFreq = false);

    void setCoeffs(const BiquadCoeffs& c) { c_ = c; }
    const BiquadCoeffs& coeffs() const { return c_; }

    // Reset the internal filter state.
    void reset() { s1_ = s2_ = 0.0; }

    inline double process(double x) {
        const double y = c_.b0 * x + s1_;
        s1_ = c_.b1 * x - c_.a1 * y + s2_;
        s2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

    // Magnitude response in dB at `freq` for a given sample rate.
    double gainAt(double freq, double sampleRate) const;

private:
    BiquadCoeffs c_{};
    double s1_ = 0.0;
    double s2_ = 0.0;
};

// Map an Equalizer APO filter type name (PK, PEQ, LP, HP, ...) to a FilterType.
// Returns false if the name is not recognized.
bool parseFilterTypeName(const std::string& name, FilterType& out);

}  // namespace apolite
