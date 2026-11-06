// ApoLite - biquad filter design and runtime.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/biquad.h"

#include <cmath>

namespace apolite {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLn2 = 0.69314718055994530942;

double clampFreq(double freq, double sampleRate) {
    const double nyq = sampleRate * 0.5;
    if (freq <= 0.0) return 1e-6;
    if (freq >= nyq) return nyq * 0.999999;
    return freq;
}
}  // namespace

bool parseFilterTypeName(const std::string& name, FilterType& out) {
    if (name == "PK" || name == "PEQ" || name == "Modal") {
        out = FilterType::Peaking;
    } else if (name == "LP") {
        out = FilterType::LowPass;
    } else if (name == "HP") {
        out = FilterType::HighPass;
    } else if (name == "LPQ") {
        out = FilterType::LowPass;
    } else if (name == "HPQ") {
        out = FilterType::HighPass;
    } else if (name == "BP") {
        out = FilterType::BandPass;
    } else if (name == "LS" || name == "LSC") {
        out = FilterType::LowShelf;
    } else if (name == "HS" || name == "HSC") {
        out = FilterType::HighShelf;
    } else if (name == "NO") {
        out = FilterType::Notch;
    } else if (name == "AP") {
        out = FilterType::AllPass;
    } else {
        return false;
    }
    return true;
}

Biquad Biquad::design(FilterType type, double gainDb, double freq, double sampleRate, double q,
                      ParamKind kind, bool isCornerFreq) {
    // A (amplitude) uses 40 dB/decade for shelf/peaking, 20 dB/decade otherwise.
    const bool usesA40 = type == FilterType::Peaking || type == FilterType::LowShelf ||
                         type == FilterType::HighShelf;
    const double A = std::pow(10.0, gainDb / (usesA40 ? 40.0 : 20.0));

    const bool shelf = type == FilterType::LowShelf || type == FilterType::HighShelf;

    // Convert a corner frequency into the shelf center frequency (DCX2496 rule).
    if (isCornerFreq && shelf) {
        double s = q;
        if (kind != ParamKind::Bandwidth && kind != ParamKind::ShelfSlope) {
            s = 1.0 / ((1.0 / (q * q) - 2.0) / (A + 1.0 / A) + 1.0);
        }
        const double factor = std::pow(10.0, std::abs(gainDb) / 80.0 / s);
        freq = (type == FilterType::LowShelf) ? freq * factor : freq / factor;
    }

    const double w0 = 2.0 * kPi * clampFreq(freq, sampleRate) / sampleRate;
    const double sn = std::sin(w0);
    const double cs = std::cos(w0);

    double alpha = 0.0;
    if (kind == ParamKind::Bandwidth) {
        alpha = sn * std::sinh(kLn2 / 2.0 * q * w0 / sn);
    } else if (kind == ParamKind::ShelfSlope && shelf) {
        alpha = sn / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / q - 1.0) + 2.0);
    } else {
        alpha = sn / (2.0 * (q > 0.0 ? q : 0.7071067811865476));
    }

    const double beta = 2.0 * std::sqrt(A) * alpha;

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type) {
        case FilterType::LowPass:
            b0 = (1.0 - cs) / 2.0;
            b1 = 1.0 - cs;
            b2 = (1.0 - cs) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::HighPass:
            b0 = (1.0 + cs) / 2.0;
            b1 = -(1.0 + cs);
            b2 = (1.0 + cs) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::BandPass:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::Notch:
            b0 = 1.0;
            b1 = -2.0 * cs;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::AllPass:
            b0 = 1.0 - alpha;
            b1 = -2.0 * cs;
            b2 = 1.0 + alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case FilterType::Peaking:
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cs;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha / A;
            break;
        case FilterType::LowShelf:
            b0 = A * ((A + 1.0) - (A - 1.0) * cs + beta);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
            b2 = A * ((A + 1.0) - (A - 1.0) * cs - beta);
            a0 = (A + 1.0) + (A - 1.0) * cs + beta;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs);
            a2 = (A + 1.0) + (A - 1.0) * cs - beta;
            break;
        case FilterType::HighShelf:
            b0 = A * ((A + 1.0) + (A - 1.0) * cs + beta);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
            b2 = A * ((A + 1.0) + (A - 1.0) * cs - beta);
            a0 = (A + 1.0) - (A - 1.0) * cs + beta;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
            a2 = (A + 1.0) - (A - 1.0) * cs - beta;
            break;
    }

    BiquadCoeffs c;
    const double inv = 1.0 / a0;
    c.b0 = b0 * inv;
    c.b1 = b1 * inv;
    c.b2 = b2 * inv;
    c.a1 = a1 * inv;
    c.a2 = a2 * inv;

    Biquad bq;
    bq.setCoeffs(c);
    return bq;
}

double Biquad::gainAt(double freq, double sampleRate) const {
    const double w = 2.0 * kPi * freq / sampleRate;
    const double sn = std::sin(w / 2.0);
    const double phi = sn * sn;

    const double b0 = c_.b0, b1 = c_.b1, b2 = c_.b2;
    const double a0 = 1.0, a1 = c_.a1, a2 = c_.a2;

    const double num = std::pow(b0 + b1 + b2, 2.0) -
                       4.0 * (b0 * b1 + 4.0 * b0 * b2 + b1 * b2) * phi +
                       16.0 * b0 * b2 * phi * phi;
    const double den = std::pow(a0 + a1 + a2, 2.0) -
                       4.0 * (a0 * a1 + 4.0 * a0 * a2 + a1 * a2) * phi +
                       16.0 * a0 * a2 * phi * phi;

    if (den <= 0.0) return 0.0;
    return 10.0 * std::log10(num / den);
}

}  // namespace apolite
