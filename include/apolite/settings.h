// ApoLite - the settings model shared by the editor and the configuration file.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// The editor never stores state of its own: it renders this model to the
// configuration text the APO reads, and parses that text back. One source of
// truth, so a file edited by hand still opens correctly in the editor.
#pragma once

#include <string>
#include <vector>

namespace apolite {

// Which device class a configuration applies to.
enum class Target {
    Microphone,  // capture stage
    Playback,    // post-mix stage
};

// Fixed centre frequencies and Q values for the named voice bands. The editor
// exposes only the amount, which is what makes it usable without knowing what a
// biquad is; the frequencies match config/voice-mic.txt.
inline constexpr double kDeMudHz = 250.0;
inline constexpr double kDeMudQ = 1.1;
inline constexpr double kPresenceHz = 3000.0;
inline constexpr double kPresenceQ = 1.0;
inline constexpr double kAirHz = 6000.0;
inline constexpr double kAirQ = 1.4;
inline constexpr double kDeEssHz = 9000.0;
inline constexpr double kDeEssQ = 1.2;

// A parametric band the editor adds on request.
struct ExtraBand {
    double freq = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
};

// Everything the editor can express. Values default to the voice preset, so a
// fresh install is already useful.
struct VoiceSettings {
    Target target = Target::Microphone;

    // Output trim. The boosts below need headroom before the compressor acts.
    double preampDb = -3.0;

    bool highPassEnabled = true;
    double highPassHz = 90.0;

    bool deMudEnabled = true;
    double deMudGainDb = -2.5;

    bool presenceEnabled = true;
    double presenceGainDb = 3.0;

    bool airEnabled = true;
    double airGainDb = 2.0;

    bool deEssEnabled = true;
    double deEssGainDb = -2.0;

    bool lowPassEnabled = true;
    double lowPassHz = 18000.0;

    bool compressorEnabled = true;
    double thresholdDb = -22.0;
    double ratio = 3.0;
    double attackMs = 5.0;
    double releaseMs = 120.0;
    double makeupDb = 8.0;
    double kneeDb = 6.0;
    double limitDb = -1.0;

    std::vector<ExtraBand> extraBands;
};

// The defaults described above.
VoiceSettings defaultSettings();

// Render the configuration text the APO reads. ASCII, one command per line.
std::string toConfigText(const VoiceSettings& settings);

struct ParseResult {
    VoiceSettings settings;

    // False when nothing usable was found, so the caller can keep its own state
    // rather than replacing it with defaults.
    bool ok = false;

    // Commands the editor cannot represent (GraphicEQ, Convolution, Copy,
    // Delay, Include, If, shelves, IIR). Saving from the editor would drop
    // them, so the caller must warn before overwriting such a file.
    std::vector<std::string> unsupported;
};

// Read settings back from configuration text. Unknown commands are collected in
// `unsupported` rather than treated as errors.
ParseResult parseConfigText(const std::string& text);

// Combined EQ magnitude in dB at `freq`, for drawing the response curve. The
// compressor is level-dependent, so it is deliberately excluded.
double eqMagnitudeDb(const VoiceSettings& settings, double freq, double sampleRate);

#if defined(_WIN32)
// %ProgramData%\ApoLite\config\config.txt, the file the APO loads.
std::wstring defaultConfigPath();
#endif

}  // namespace apolite
