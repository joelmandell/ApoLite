// ApoLite - settings model: render to and parse from the APO configuration text.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "apolite/biquad.h"
#include "apolite/mathutil.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace apolite {

namespace {

// Trimmed, whitespace-collapsed copy of a line's value part.
std::string collapse(const std::string& text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Split "Key value" into its two halves; `key` comes back lower case.
bool splitCommand(const std::string& line, std::string& key, std::string& value) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    key = lower(collapse(line.substr(0, colon)));
    value = collapse(line.substr(colon + 1));
    return !key.empty();
}

double numberAfter(const std::string& value, const std::string& field, double fallback) {
    const std::string needle = lower(field);
    std::istringstream in(lower(value));
    std::string token;
    while (in >> token) {
        if (token == needle) {
            double parsed = 0.0;
            if (in >> parsed) return parsed;
            return fallback;
        }
    }
    return fallback;
}

// "ON PK Fc 3000 Hz Gain 3 dB Q 1" -> type name is the token after ON.
std::string filterTypeOf(const std::string& value) {
    std::istringstream in(value);
    std::string token;
    if (!(in >> token)) return {};
    if (lower(token) == "on") {
        if (!(in >> token)) return {};
    }
    return lower(token);
}

// Frequency of a filter command, 0 when absent.
double filterFreqOf(const std::string& value) {
    std::istringstream in(lower(value));
    std::string token;
    while (in >> token) {
        if (token == "fc") {
            double parsed = 0.0;
            if (in >> parsed) return parsed;
            return 0.0;
        }
    }
    return 0.0;
}

std::string formatDb(double value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f dB", value);
    return buffer;
}

// `%g` switches to scientific notation for values such as 18000, which is valid
// but unreadable in a file people open by hand. Render plainly instead, then
// trim the trailing zeros that `%f` adds.
std::string formatValue(double value, const char* unit) {
    char buffer[48] = {};
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);

    std::string number(buffer);
    if (number.find('.') != std::string::npos) {
        number.erase(number.find_last_not_of('0') + 1);
        if (!number.empty() && number.back() == '.') number.pop_back();
    }
    if (number.empty() || number == "-0") number = "0";

    return *unit ? number + " " + unit : number;
}

}  // namespace

VoiceSettings defaultSettings() { return VoiceSettings{}; }

std::string toConfigText(const VoiceSettings& s) {
    std::ostringstream out;
    out << "# ApoLite - written by the ApoLite editor.\n";
    out << "# Loaded from %ProgramData%\\ApoLite\\config\\config.txt.\n";
    out << "#\n";
    out << "# Edits here are picked up while audio is running. If you change this\n";
    out << "# file by hand, the editor will offer to keep what it does not\n";
    out << "# understand.\n";
    out << "\n";

    if (s.target == Target::Microphone) {
        out << "# Only the microphone stage, so playback is left untouched.\n";
        out << "Stage: capture\n";
    } else {
        out << "# Playback stage.\n";
        out << "Stage: post-mix\n";
    }
    out << "\n";

    out << "# Headroom so the boosts below cannot clip before the compressor acts.\n";
    out << "Preamp: " << formatDb(s.preampDb) << "\n";

    if (s.highPassEnabled) {
        out << "\n# Remove rumble and desk thumps.\n";
        out << "Filter: ON HP Fc " << formatValue(s.highPassHz, "Hz") << "\n";
    }

    if (s.deMudEnabled) {
        out << "\n# Cut the boxy low-mid region that makes voices sound muddy.\n";
        out << "Filter: ON PK Fc " << formatValue(kDeMudHz, "Hz") << " Gain "
            << formatDb(s.deMudGainDb) << " Q " << kDeMudQ << "\n";
    }

    if (s.presenceEnabled) {
        out << "\n# Presence lift: what makes a voice cut through a call.\n";
        out << "Filter: ON PK Fc " << formatValue(kPresenceHz, "Hz") << " Gain "
            << formatDb(s.presenceGainDb) << " Q " << kPresenceQ << "\n";
    }

    if (s.airEnabled) {
        out << "\n# Breath and clarity above the presence band.\n";
        out << "Filter: ON PK Fc " << formatValue(kAirHz, "Hz") << " Gain "
            << formatDb(s.airGainDb) << " Q " << kAirQ << "\n";
    }

    if (s.deEssEnabled) {
        out << "\n# Tame harsh sibilance.\n";
        out << "Filter: ON PK Fc " << formatValue(kDeEssHz, "Hz") << " Gain "
            << formatDb(s.deEssGainDb) << " Q " << kDeEssQ << "\n";
    }

    for (const ExtraBand& band : s.extraBands) {
        out << "\n# Added band.\n";
        out << "Filter: ON PK Fc " << formatValue(band.freq, "Hz") << " Gain "
            << formatDb(band.gainDb) << " Q " << formatValue(band.q, "") << "\n";
    }

    if (s.compressorEnabled) {
        out << "\n# Even out loud and quiet speech, then hold a hard ceiling.\n";
        out << "# This is the part that actually boosts you: quiet talkers are\n";
        out << "# raised, shouts are held back.\n";
        out << "Compressor: Threshold " << formatValue(s.thresholdDb, "") << " Ratio "
            << formatValue(s.ratio, "") << " Attack " << formatValue(s.attackMs, "") << " Release "
            << formatValue(s.releaseMs, "") << " Makeup " << formatValue(s.makeupDb, "")
            << " Knee " << formatValue(s.kneeDb, "") << " Limit " << formatValue(s.limitDb, "")
            << "\n";
    }

    if (s.lowPassEnabled) {
        out << "\n# Final safety net against the float to integer conversion clipping.\n";
        out << "Filter: ON LP Fc " << formatValue(s.lowPassHz, "Hz") << "\n";
    }

    return out.str();
}

ParseResult parseConfigText(const std::string& text) {
    ParseResult result;
    VoiceSettings& s = result.settings;

    // A hand-edited file that omits a band keeps the default for it; an
    // explicit band overrides it.
    bool sawAnyCommand = false;

    std::istringstream lines(text);
    std::string raw;
    while (std::getline(lines, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const std::string line = collapse(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        std::string key;
        std::string value;
        if (!splitCommand(line, key, value)) continue;

        sawAnyCommand = true;
        if (key == "stage") {
            const std::string v = lower(value);
            s.target = (v.find("capture") != std::string::npos) ? Target::Microphone
                                                                : Target::Playback;
        } else if (key == "preamp") {
            s.preampDb = std::strtod(value.c_str(), nullptr);
        } else if (key == "compressor") {
            s.thresholdDb = numberAfter(value, "threshold", s.thresholdDb);
            s.ratio = numberAfter(value, "ratio", s.ratio);
            s.attackMs = numberAfter(value, "attack", s.attackMs);
            s.releaseMs = numberAfter(value, "release", s.releaseMs);
            s.makeupDb = numberAfter(value, "makeup", s.makeupDb);
            s.kneeDb = numberAfter(value, "knee", s.kneeDb);
            s.limitDb = numberAfter(value, "limit", s.limitDb);
        } else if (key == "filter") {
            const std::string type = filterTypeOf(value);
            if (type == "hp") {
                s.highPassEnabled = true;
                s.highPassHz = filterFreqOf(value);
            } else if (type == "lp") {
                s.lowPassEnabled = true;
                s.lowPassHz = filterFreqOf(value);
            } else if (type == "pk" || type == "peq") {
                const double freq = filterFreqOf(value);
                const double gain = numberAfter(value, "gain", 0.0);
                const double q = numberAfter(value, "q", 1.0);
                const double tol = 0.5;  // the editor writes exact frequencies
                if (std::fabs(freq - kDeMudHz) < tol) {
                    s.deMudEnabled = true;
                    s.deMudGainDb = gain;
                } else if (std::fabs(freq - kPresenceHz) < tol) {
                    s.presenceEnabled = true;
                    s.presenceGainDb = gain;
                } else if (std::fabs(freq - kAirHz) < tol) {
                    s.airEnabled = true;
                    s.airGainDb = gain;
                } else if (std::fabs(freq - kDeEssHz) < tol) {
                    s.deEssEnabled = true;
                    s.deEssGainDb = gain;
                } else {
                    s.extraBands.push_back(ExtraBand{freq, gain, q});
                }
            } else {
                result.unsupported.push_back(line);
            }
        } else {
            // GraphicEQ, Convolution, Copy, Delay, Include, Channel, Device,
            // If/ElseIf/Else/EndIf, Eval and anything else.
            result.unsupported.push_back(line);
        }
    }

    // A file with no Stage applies everywhere; keep the capture default, which
    // is the right guess for a microphone-focused tool.
    result.ok = sawAnyCommand;
    return result;
}

double eqMagnitudeDb(const VoiceSettings& s, double freq, double sampleRate) {
    double total = s.preampDb;

    auto add = [&](FilterType type, double gainDb, double centerHz, double q, ParamKind kind,
                   bool corner) {
        const Biquad biquad = Biquad::design(type, gainDb, centerHz, sampleRate, q, kind, corner);
        total += biquad.gainAt(freq, sampleRate);
    };

    if (s.highPassEnabled) add(FilterType::HighPass, 0.0, s.highPassHz, 0.7071067811865476,
                               ParamKind::Q, false);
    if (s.deMudEnabled) add(FilterType::Peaking, s.deMudGainDb, kDeMudHz, kDeMudQ, ParamKind::Q, false);
    if (s.presenceEnabled)
        add(FilterType::Peaking, s.presenceGainDb, kPresenceHz, kPresenceQ, ParamKind::Q, false);
    if (s.airEnabled) add(FilterType::Peaking, s.airGainDb, kAirHz, kAirQ, ParamKind::Q, false);
    if (s.deEssEnabled)
        add(FilterType::Peaking, s.deEssGainDb, kDeEssHz, kDeEssQ, ParamKind::Q, false);
    for (const ExtraBand& band : s.extraBands) {
        add(FilterType::Peaking, band.gainDb, band.freq, band.q, ParamKind::Q, false);
    }
    if (s.lowPassEnabled)
        add(FilterType::LowPass, 0.0, s.lowPassHz, 0.7071067811865476, ParamKind::Q, false);

    return total;
}

#if defined(_WIN32)
std::wstring defaultConfigPath() {
    wchar_t programData[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH);
    const std::wstring base = (len > 0 && len < MAX_PATH) ? std::wstring(programData, len)
                                                         : std::wstring(L"C:\\ProgramData");
    return base + L"\\ApoLite\\config\\config.txt";
}
#endif

}  // namespace apolite
