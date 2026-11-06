// ApoLite - test suite. Uses only the C++ standard library.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "apolite/biquad.h"
#include "apolite/mathutil.h"
#include "apolite/channel.h"
#include "apolite/config.h"
#include "apolite/engine.h"
#include "apolite/expression.h"
#include "apolite/fft.h"
#include "apolite/fir.h"
#include "apolite/gain.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

void checkNear(double a, double b, double tol, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(a - b) <= tol)) {
        ++g_failures;
        std::printf("FAIL: %s (got %.6f, expected %.6f, tol %.6f)\n", what.c_str(), a, b, tol);
    }
}

// ---------------------------------------------------------------- FFT tests

void testFft() {
    const size_t n = 64;
    apolite::ComplexFft fft(n);
    std::vector<apolite::cplx> data(n);
    for (size_t i = 0; i < n; ++i) data[i] = apolite::cplx(std::sin(0.3 * i), 0.0);

    auto original = data;
    fft.forward(data.data());
    fft.inverse(data.data());
    double maxErr = 0.0;
    for (size_t i = 0; i < n; ++i) maxErr = std::max(maxErr, std::abs(data[i] - original[i]));
    checkNear(maxErr, 0.0, 1e-12, "complex FFT round trip");

    // A pure tone must land in a single bin.
    std::vector<apolite::cplx> tone(n);
    const size_t bin = 5;
    for (size_t i = 0; i < n; ++i)
        tone[i] = apolite::cplx(std::cos(2.0 * apolite::kPi * bin * i / n), 0.0);
    fft.forward(tone.data());
    checkNear(std::abs(tone[bin]), n / 2.0, 1e-9, "tone energy in bin 5");
    for (size_t k = 0; k < n; ++k)
        if (k != bin && k != n - bin) checkNear(std::abs(tone[k]), 0.0, 1e-9, "tone leakage");

    // Real FFT round trip.
    apolite::RealFft rfft(n);
    std::vector<double> real(n);
    for (size_t i = 0; i < n; ++i) real[i] = std::cos(0.11 * i) * 0.7;
    auto realOriginal = real;
    std::vector<apolite::cplx> spec(rfft.bins());
    rfft.forward(real.data(), spec.data());
    std::vector<double> back(n);
    rfft.inverse(spec.data(), back.data());
    maxErr = 0.0;
    for (size_t i = 0; i < n; ++i) maxErr = std::max(maxErr, std::fabs(back[i] - realOriginal[i]));
    checkNear(maxErr, 0.0, 1e-12, "real FFT round trip");
}

// ------------------------------------------------------------- Biquad tests

void testBiquad() {
    const double sr = 48000.0;

    // A peaking filter must reach its gain exactly at Fc.
    apolite::Biquad pk =
        apolite::Biquad::design(apolite::FilterType::Peaking, 6.0, 1000.0, sr, 1.0,
                                apolite::ParamKind::Q);
    checkNear(pk.gainAt(1000.0, sr), 6.0, 1e-6, "peaking gain at Fc");

    // Low/high shelf reach the gain at DC and Nyquist.
    apolite::Biquad ls = apolite::Biquad::design(apolite::FilterType::LowShelf, 6.0, 200.0, sr,
                                                 0.9, apolite::ParamKind::ShelfSlope);
    checkNear(ls.gainAt(1.0, sr), 6.0, 0.05, "low shelf gain at DC");
    checkNear(ls.gainAt(20000.0, sr), 0.0, 0.05, "low shelf unity at high f");

    apolite::Biquad hs = apolite::Biquad::design(apolite::FilterType::HighShelf, -6.0, 4000.0, sr,
                                                 0.9, apolite::ParamKind::ShelfSlope);
    checkNear(hs.gainAt(23000.0, sr), -6.0, 0.05, "high shelf gain at Nyquist");

    // A low-pass is -3 dB at its cutoff for Q = 1/sqrt(2).
    apolite::Biquad lp = apolite::Biquad::design(apolite::FilterType::LowPass, 0.0, 1000.0, sr,
                                                 0.7071067811865476, apolite::ParamKind::Q);
    checkNear(lp.gainAt(1000.0, sr), -3.0103, 0.02, "low pass -3 dB at cutoff");

    // The DCX2496 corner-frequency rule shifts the shelf midpoint upward.
    apolite::Biquad lsCorner = apolite::Biquad::design(
        apolite::FilterType::LowShelf, 6.0, 200.0, sr, 0.9, apolite::ParamKind::ShelfSlope, true);
    const double shifted = 200.0 * std::pow(10.0, 6.0 / 80.0 / 0.9);
    apolite::Biquad lsCenter = apolite::Biquad::design(
        apolite::FilterType::LowShelf, 6.0, shifted, sr, 0.9, apolite::ParamKind::ShelfSlope, false);
    checkNear(lsCorner.coeffs().b0, lsCenter.coeffs().b0, 1e-12,
              "corner-frequency rule shifts Fc by 10^(gain/80/S)");
    checkNear(lsCorner.coeffs().a2, lsCenter.coeffs().a2, 1e-12,
              "corner-frequency rule matches the shifted design");

    // Running a constant signal through a peaking filter settles to gain*DC.
    apolite::Biquad bq = apolite::Biquad::design(apolite::FilterType::LowShelf, 6.0, 100.0, sr,
                                                 0.9, apolite::ParamKind::ShelfSlope);
    double y = 0.0;
    for (int i = 0; i < 20000; ++i) y = bq.process(1.0);
    checkNear(y, std::pow(10.0, 6.0 / 20.0), 1e-3, "biquad DC gain from time domain");
}

// ---------------------------------------------------------------- FIR tests

void testFir() {
    const double sr = 48000.0;
    // Flat magnitude must produce an impulse.
    auto flat = apolite::designMinimumPhaseFir(4096, sr, [](double) { return 1.0; });
    checkNear(flat[0], 1.0, 1e-3, "flat FIR is a unit impulse");
    double tail = 0.0;
    for (size_t i = 1; i < flat.size(); ++i) tail = std::max(tail, std::fabs(flat[i]));
    check(tail < 1e-3, "flat FIR has no tail");

    // A 6 dB gain across the band.
    auto gain6 = apolite::designMinimumPhaseFir(4096, sr, [](double) {
        return std::pow(10.0, 6.0 / 20.0);
    });
    checkNear(gain6[0], std::pow(10.0, 6.0 / 20.0), 1e-2, "6 dB FIR impulse height");

    // Convolution must apply the designed gain to a sine.
    apolite::FirConvolver conv(gain6, 256);
    const size_t n = 4096;
    std::vector<double> x(n), y(n);
    for (size_t i = 0; i < n; ++i) x[i] = std::sin(2.0 * apolite::kPi * 1000.0 * i / sr);
    conv.process(x.data(), y.data(), n);
    double peak = 0.0;
    for (size_t i = 2048; i < n; ++i) peak = std::max(peak, std::fabs(y[i]));
    checkNear(peak, std::pow(10.0, 6.0 / 20.0), 0.01, "FIR convolution applies 6 dB");
}

// ------------------------------------------------------- Expression tests

void testExpression() {
    apolite::ExpressionEvaluator ev;
    ev.setVariable("sampleRate", apolite::Value::number(48000));
    ev.setVariable("name", apolite::Value::string("Speakers"));

    checkNear(ev.evaluate("1+2*3").asNumber(), 7.0, 1e-12, "arithmetic precedence");
    checkNear(ev.evaluate("2^10").asNumber(), 1024.0, 1e-12, "power");
    checkNear(ev.evaluate("sampleRate/2").asNumber(), 24000.0, 1e-12, "variable");
    check(ev.evaluate("sampleRate > 44100").truthy(), "comparison");
    check(ev.evaluate("sampleRate > 44100 and 1 == 1").truthy(), "logical and");
    check(!ev.evaluate("sampleRate < 44100 or 0").truthy(), "logical or false");
    check(ev.evaluate("1 ? 5 : 9").truthy(), "ternary condition");
    checkNear(ev.evaluate("1 ? 5 : 9").asNumber(), 5.0, 1e-12, "ternary true branch");
    checkNear(ev.evaluate("0 ? 5 : 9").asNumber(), 9.0, 1e-12, "ternary false branch");
    check(ev.evaluate("name == \"Speakers\"").truthy(), "string equality");
    check(ev.evaluate("name + \" 2\"").asString() == "Speakers 2", "string concatenation");
    check(ev.evaluate("!0").truthy(), "not operator");
    checkNear(ev.evaluate("min(3, 1, 2)").asNumber(), 1.0, 1e-12, "min");
    checkNear(ev.evaluate("floor(3.7)").asNumber(), 3.0, 1e-12, "floor");
    check(ev.evaluate("(2+3)*4 == 20").truthy(), "parentheses");
}

// ------------------------------------------------------ Channel layout tests

void testChannelLayout() {
    apolite::ChannelLayout stereo(2, 0);
    check(stereo.names().size() == 2, "stereo channel count");
    check(stereo.indexOf("L") == 0, "L maps to 0");
    check(stereo.indexOf("R") == 1, "R maps to 1");
    check(stereo.indexOf("1") == 0, "numeric 1 maps to 0");
    check(stereo.indexOf("2") == 1, "numeric 2 maps to 1");
    check(stereo.indexOf("C") == -1, "C absent in stereo");

    apolite::ChannelLayout surround(6, 0);
    check(surround.names().size() == 6, "5.1 channel count");
    check(surround.indexOf("C") == 2, "C maps to 2 in 5.1");
    check(surround.indexOf("LFE") == 3, "LFE maps to 3 in 5.1");
    check(surround.indexOf("SUB") == 3, "SUB aliases LFE");
}

// --------------------------------------------------------------- Gain tests

void testGainInterpolator() {
    apolite::GainInterpolator gi({apolite::EqNode{100.0, 0.0}, apolite::EqNode{1000.0, 10.0}});
    checkNear(gi.gainAt(100.0), 0.0, 1e-12, "node value at 100 Hz");
    checkNear(gi.gainAt(1000.0), 10.0, 1e-12, "node value at 1000 Hz");
    // Log-frequency midpoint of 100 and 1000 is 316.23 Hz.
    checkNear(gi.gainAt(316.227766), 5.0, 1e-3, "log midpoint interpolation");
    checkNear(gi.gainAt(10.0), 0.0, 1e-12, "clamp below first node");
    checkNear(gi.gainAt(20000.0), 10.0, 1e-12, "clamp above last node");
}

// ------------------------------------------------------------- Engine tests

std::string writeTemp(const std::string& name, const std::string& text) {
    const std::string path = "/tmp/apolite_test_" + name;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    return path;
}

void testEnginePreamp() {
    const std::string cfg = writeTemp("preamp.txt", "Preamp: -6.02 dB\n");
    apolite::Engine engine;
    engine.configure(48000.0, 2, 2, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "preamp entry count");

    std::vector<float> buf(512 * 2, 0.5f);
    engine.processInterleaved(buf.data(), 512);
    const double expected = 0.5 * std::pow(10.0, -6.02 / 20.0);
    checkNear(buf[0], expected, 1e-4, "preamp scales samples");
    checkNear(buf[511], expected, 1e-4, "preamp scales last sample");
}

void testEngineBiquad() {
    // A +6 dB low shelf must amplify a DC-ish signal by about 6 dB.
    const std::string cfg = writeTemp("shelf.txt", "Filter: ON LSC 6 dB Fc 200 Hz Gain 6 dB\n");
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "shelf entry count");

    std::vector<float> buf(48000, 1.0f);
    engine.processInterleaved(buf.data(), buf.size());
    checkNear(buf[47999], std::pow(10.0, 6.0 / 20.0), 0.02, "shelf DC gain through engine");
}

void testEngineDelay() {
    const std::string cfg = writeTemp("delay.txt", "Delay: 1 ms\n");
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "delay entry count");

    const size_t n = 1024;
    std::vector<float> buf(n, 0.0f);
    buf[0] = 1.0f;
    engine.processInterleaved(buf.data(), n);
    const size_t delaySamples = 48;  // 1 ms at 48 kHz
    checkNear(buf[delaySamples], 1.0, 1e-6, "delayed impulse position");
    checkNear(buf[0], 0.0, 1e-6, "original position is silent");
}

void testEngineCopy() {
    const std::string cfg = writeTemp("copy.txt", "Copy: L=R\n");
    apolite::Engine engine;
    engine.configure(48000.0, 2, 2, 0, false, false, true, "Speakers", "Speakers", "");
    engine.loadConfiguration(cfg);

    std::vector<float> buf = {0.25f, 0.75f, 0.25f, 0.75f};
    engine.processInterleaved(buf.data(), 2);
    checkNear(buf[0], 0.75, 1e-6, "copy L=R writes right into left");
    checkNear(buf[2], 0.75, 1e-6, "copy L=R writes right into left (frame 2)");
}

void testEngineChannelSelect() {
    // Only the right channel gets the preamp.
    const std::string cfg = writeTemp("channel.txt", "Channel: R\nPreamp: -20 dB\n");
    apolite::Engine engine;
    engine.configure(48000.0, 2, 2, 0, false, false, true, "Speakers", "Speakers", "");
    engine.loadConfiguration(cfg);

    std::vector<float> buf = {1.0f, 1.0f};
    engine.processInterleaved(buf.data(), 1);
    checkNear(buf[0], 1.0, 1e-6, "left channel untouched");
    checkNear(buf[1], std::pow(10.0, -1.0), 1e-6, "right channel attenuated");
}

void testEngineGraphicEq() {
    // A graphic EQ with a flat response must not alter the signal.
    const std::string cfg = writeTemp("graphiceq.txt", "GraphicEQ: 20 0; 20000 0\n");
    // A flat graphic EQ still needs the FIR design to be accurate to ~80 dB.
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "graphic EQ entry count");

    const size_t n = 8192;
    std::vector<float> buf(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        buf[i] = static_cast<float>(std::sin(2.0 * apolite::kPi * 1000.0 * i / 48000.0));
    }

    std::vector<float> reference = buf;
    engine.processInterleaved(buf.data(), n);

    // The FIR is a pure impulse here, so the output matches the input exactly.
    double maxErr = 0.0;
    for (size_t i = 0; i < n; ++i)
        maxErr = std::max(maxErr, std::fabs(static_cast<double>(buf[i]) - reference[i]));
    checkNear(maxErr, 0.0, 1e-6, "flat graphic EQ is transparent");
}

void testEngineIf() {
    const std::string cfg = writeTemp("if.txt",
                                      "If: sampleRate == 48000\n"
                                      "Preamp: -6.02 dB\n"
                                      "Else:\n"
                                      "Preamp: -20 dB\n"
                                      "EndIf\n");
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "If taken branch yields one entry");

    std::vector<float> buf(64, 1.0f);
    engine.processInterleaved(buf.data(), 64);
    checkNear(buf[0], std::pow(10.0, -6.02 / 20.0), 1e-4, "If true branch applied");
}

void testEngineElseBranch() {
    const std::string cfg = writeTemp("else.txt",
                                      "If: sampleRate == 44100\n"
                                      "Preamp: -6.02 dB\n"
                                      "ElseIf: sampleRate == 48000\n"
                                      "Preamp: -3.01 dB\n"
                                      "Else:\n"
                                      "Preamp: -20 dB\n"
                                      "EndIf\n");
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "ElseIf branch yields one entry");

    std::vector<float> buf(64, 1.0f);
    engine.processInterleaved(buf.data(), 64);
    checkNear(buf[0], std::pow(10.0, -3.01 / 20.0), 1e-4, "ElseIf branch applied");
}

void testEngineDevice() {
    // A device filter that does not match must disable the following lines.
    const std::string cfg = writeTemp("device.txt",
                                      "Device: Headphones\n"
                                      "Preamp: -20 dB\n"
                                      "Device: Speakers\n"
                                      "Preamp: -6.02 dB\n");
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "only the matching device block is applied");

    std::vector<float> buf(64, 1.0f);
    engine.processInterleaved(buf.data(), 64);
    checkNear(buf[0], std::pow(10.0, -6.02 / 20.0), 1e-4, "matching device block applied");
}

void testEngineIir() {
    // A first-order IIR with b = {1}, a = {1, -0.5} is a one-pole low pass.
    const std::string cfg = writeTemp("iir.txt", "Filter: ON IIR Order 1 Coefficients 0.5 0.5 1 -0.5\n");
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "IIR entry count");

    std::vector<float> buf(4000, 1.0f);
    engine.processInterleaved(buf.data(), buf.size());
    // DC gain = (b0 + b1) / (1 + a1) = 1 / 0.5 = 2.
    checkNear(buf[3999], 2.0, 1e-3, "IIR DC gain");
}

void testEngineCompressor() {
    // A limiter with a low ceiling must cap a loud signal near the ceiling.
    const std::string cfg =
        writeTemp("comp.txt", "Compressor: Threshold -20 Ratio 4 Attack 5 Release 120 "
                              "Makeup 6 Knee 6 Limit -3\n");
    apolite::Engine engine;
    engine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    check(engine.loadConfiguration(cfg) == 1, "compressor entry count");

    std::vector<float> loud(4800, 0.9f);
    engine.processInterleaved(loud.data(), loud.size());
    const float ceiling = std::pow(10.0f, -3.0f / 20.0f);
    check(loud.back() <= ceiling + 1e-4f, "compressor limits peak to ceiling");
    check(loud.back() > 0.0f, "compressor does not silence the signal");

    // Quiet speech below the threshold is raised by the makeup gain.
    apolite::Engine quietEngine;
    quietEngine.configure(48000.0, 1, 1, 0, false, false, true, "Speakers", "Speakers", "");
    quietEngine.loadConfiguration(cfg);
    std::vector<float> quiet(4800, 0.002f);  // ~ -54 dBFS, below threshold
    quietEngine.processInterleaved(quiet.data(), quiet.size());
    check(quiet.back() > 0.002f * 1.5f, "compressor raises quiet speech via makeup gain");
}

void testEngineStage() {
    // A capture-only config must be skipped entirely on a playback stage.
    const std::string cfg =
        writeTemp("stage.txt", "Stage: capture\nPreamp: -20 dB\n");
    const std::string full = writeTemp("stage_full.txt", "Preamp: -20 dB\n");

    apolite::Engine capture;
    capture.configure(48000.0, 1, 1, 0, /*isCapture=*/true, false, true, "Mic", "Mic", "");
    check(capture.loadConfiguration(cfg) == 1, "capture stage applies to capture");
    std::vector<float> captureBuf(256, 1.0f);
    capture.processInterleaved(captureBuf.data(), 256);
    check(captureBuf.back() < 0.5f, "capture stage attenuates on capture");

    apolite::Engine playback;
    playback.configure(48000.0, 1, 1, 0, /*isCapture=*/false, false, true, "Speakers", "Speakers", "");
    check(playback.loadConfiguration(cfg) == 0, "capture stage skipped on playback");
    std::vector<float> playbackBuf(256, 1.0f);
    playback.processInterleaved(playbackBuf.data(), 256);
    checkNear(playbackBuf.back(), 1.0, 1e-6, "capture stage leaves playback untouched");

    apolite::Engine noStage;
    noStage.configure(48000.0, 1, 1, 0, /*isCapture=*/false, false, true, "Speakers", "Speakers", "");
    check(noStage.loadConfiguration(full) == 1, "stage-less config applies everywhere");
}

}  // namespace

int main() {
    testFft();
    testBiquad();
    testFir();
    testExpression();
    testChannelLayout();
    testGainInterpolator();
    testEnginePreamp();
    testEngineBiquad();
    testEngineDelay();
    testEngineCopy();
    testEngineChannelSelect();
    testEngineGraphicEq();
    testEngineIf();
    testEngineElseBranch();
    testEngineDevice();
    testEngineIir();
    testEngineCompressor();
    testEngineStage();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
