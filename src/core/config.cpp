// ApoLite - Equalizer APO compatible configuration loader.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "apolite/channel.h"
#include "apolite/impulse.h"

namespace apolite {

namespace {

constexpr int kRecursionLimit = 100;

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string w;
    while (ss >> w) out.push_back(w);
    return out;
}

// Replace ',' with '.' unless a '.' is already present (Equalizer APO's
// handling of locales that use a comma as the decimal mark).
std::string normalizeDecimal(std::string s) {
    if (s.find('.') != std::string::npos) return s;
    for (char& c : s)
        if (c == ',') c = '.';
    return s;
}

bool isNumberToken(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    bool digit = false, dot = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
        else if (c == '.' && !dot) dot = true;
        else if ((c == 'e' || c == 'E') && digit) return true;
        else return false;
    }
    return digit;
}

std::string dirName(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? std::string() : path.substr(0, pos);
}

bool isAbsolutePath(const std::string& path) {
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        return true;
    return !path.empty() && (path[0] == '\\' || path[0] == '/');
}

std::string joinPath(const std::string& dir, const std::string& rel) {
    if (dir.empty()) return rel;
    const char last = dir.back();
    if (last == '/' || last == '\\') return dir + rel;
    return dir + "/" + rel;
}

}  // namespace

ConfigLoader::ConfigLoader(DeviceContext context, LogSink log)
    : ctx_(std::move(context)), log_(std::move(log)) {
    const size_t deviceChannels = ctx_.isCapture ? ctx_.channelCount : ctx_.channelCount;
    const ChannelLayout layout(deviceChannels, ctx_.channelMask);
    allChannels_ = layout.names();
    if (allChannels_.empty()) allChannels_ = {"L", "R"};

    // All channels are selected initially.
    currentChannels_.resize(allChannels_.size());
    for (size_t i = 0; i < allChannels_.size(); ++i) currentChannels_[i] = i;

    evaluator_.setVariable("inputChannelCount", Value::number(static_cast<double>(ctx_.channelCount)));
    evaluator_.setVariable("outputChannelCount", Value::number(static_cast<double>(ctx_.channelCount)));
    evaluator_.setVariable("sampleRate", Value::number(ctx_.sampleRate));
    evaluator_.setVariable("deviceName", Value::string(ctx_.deviceName));
    evaluator_.setVariable("connectionName", Value::string(ctx_.connectionName));
    evaluator_.setVariable("deviceGuid", Value::string(ctx_.deviceGuid));
    evaluator_.setVariable("stage", Value::string(ctx_.stageName));
}

bool ConfigLoader::currentActive() const {
    if (!deviceMatches_ || !stageMatches_) return false;
    if (!ifStack_.empty() && ifStack_.back().falseCount > 0) return false;
    return true;
}

void ConfigLoader::addToCurrent(std::shared_ptr<IFilter> filter) {
    if (!filter || currentChannels_.empty()) return;
    ChainEntry entry;
    entry.filter = std::move(filter);
    entry.inChannels = currentChannels_;
    entry.outChannels = currentChannels_;
    chain_.entries.push_back(std::move(entry));
}

int ConfigLoader::indexInAll(const std::string& word, bool allowNew) {
    if (word.empty()) return -1;
    if (std::isdigit(static_cast<unsigned char>(word[0]))) {
        const long n = std::strtol(word.c_str(), nullptr, 10) - 1;
        if (n < 0 || static_cast<size_t>(n) >= allChannels_.size()) return -1;
        return static_cast<int>(n);
    }
    std::string upper;
    for (char c : word) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    auto it = std::find(allChannels_.begin(), allChannels_.end(), upper);
    if (it != allChannels_.end()) return static_cast<int>(it - allChannels_.begin());

    static const char* aliases[][2] = {
        {"SL", "RL"}, {"SR", "RR"}, {"RL", "SL"}, {"RR", "SR"}, {"SUB", "LFE"}};
    for (const auto& alias : aliases) {
        if (upper == alias[0]) {
            auto a = std::find(allChannels_.begin(), allChannels_.end(), std::string(alias[1]));
            if (a != allChannels_.end()) return static_cast<int>(a - allChannels_.begin());
        }
    }
    if (allowNew) {
        allChannels_.push_back(upper);
        return static_cast<int>(allChannels_.size() - 1);
    }
    return -1;
}

FilterChain ConfigLoader::loadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (log_) log_("Cannot open configuration file: " + path);
        FilterChain empty;
        empty.channelNames = allChannels_;
        return empty;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadText(ss.str(), path);
}

FilterChain ConfigLoader::loadText(const std::string& text, const std::string& basePath) {
    chain_ = FilterChain();
    chain_.channelNames = allChannels_;

    parseInto(text, basePath, 0);

    chain_.channelNames = allChannels_;
    return chain_;
}

void ConfigLoader::parseInto(const std::string& text, const std::string& path, int depth) {
    if (depth > kRecursionLimit) {
        if (log_) log_("Include recursion limit reached");
        return;
    }

    // Save state so Includes cannot leak channel/device/If state outwards.
    const std::vector<size_t> savedChannels = currentChannels_;
    const bool savedDevice = deviceMatches_;
    const bool savedStage = stageMatches_;
    const std::vector<IfFrame> savedIf = ifStack_;
    ifStack_.clear();

    std::istringstream stream(text);
    std::string rawLine;
    while (std::getline(stream, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();
        const size_t colon = rawLine.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(rawLine.substr(0, colon));
        std::string value = rawLine.substr(colon + 1);
        if (key.empty()) continue;

        // Include is handled here so that it can recurse with the right base path.
        if (key == "Include") {
            if (currentActive()) {
                const std::string target = trim(value);
                const std::string resolved =
                    isAbsolutePath(target) ? target : joinPath(dirName(path), target);
                std::ifstream inc(resolved, std::ios::binary);
                if (!inc) {
                    if (log_) log_("Cannot include file: " + resolved);
                } else {
                    std::ostringstream ss;
                    ss << inc.rdbuf();
                    parseInto(ss.str(), resolved, depth + 1);
                }
            }
            continue;
        }

        handleLine(key, value, path);
    }

    if (!ifStack_.empty() && log_) log_("If was not closed by EndIf in " + path);

    currentChannels_ = savedChannels;
    deviceMatches_ = savedDevice;
    stageMatches_ = savedStage;
    ifStack_ = savedIf;
}

void ConfigLoader::handleLine(const std::string& key, const std::string& value,
                              const std::string& path) {
    // If/ElseIf/Else/EndIf are evaluated regardless of device/stage filtering
    // so that the control-flow structure stays balanced.
    if (key == "If" || key == "ElseIf" || key == "Else" || key == "EndIf") {
        handleIf(key, value);
        return;
    }

    if (key == "Device") {
        handleDevice(value);
        return;
    }

    // Device does not match: skip everything until the next Device line.
    if (!deviceMatches_) return;
    if (!ifStack_.empty() && ifStack_.back().falseCount > 0) return;

    if (key == "Stage") {
        handleStage(value);
        return;
    }
    if (!stageMatches_) return;

    if (key == "Channel") {
        handleChannel(value);
        return;
    }

    std::string v = substituteInline(value);

    if (key == "Preamp") {
        addPreamp(v);
    } else if (key.rfind("Filter", 0) == 0) {
        addFilter(v);
    } else if (key == "Delay") {
        addDelay(v);
    } else if (key == "Copy") {
        addCopy(v);
    } else if (key == "GraphicEQ") {
        addGraphicEq(v);
    } else if (key == "Convolution") {
        addConvolution(v, path);
    } else if (key == "Compressor") {
        addCompressor(v);
    } else if (key == "Eval") {
        handleEval(v);
    }
    // Unknown commands are silently ignored, as Equalizer APO does.
}

std::string ConfigLoader::substituteInline(const std::string& value) {
    std::string out;
    std::string expr;
    bool inExpr = false;
    bool lastBackslash = false;

    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '`') {
            if (!inExpr) {
                if (lastBackslash) out += c;
                else inExpr = true;
            } else {
                if (lastBackslash) {
                    expr += c;
                } else {
                    inExpr = false;
                    try {
                        out += evalExpression(expr).asString();
                    } catch (const std::exception& e) {
                        if (log_) log_("Inline expression error: " + std::string(e.what()));
                    }
                    expr.clear();
                }
            }
            lastBackslash = false;
        } else if (c == '\\') {
            const bool escapesTick = (i + 1 < value.size() && value[i + 1] == '`');
            if (!escapesTick) (inExpr ? expr : out) += c;
            lastBackslash = true;
        } else {
            (inExpr ? expr : out) += c;
            lastBackslash = false;
        }
    }
    return out;
}

Value ConfigLoader::evalExpression(const std::string& expr) const {
    return evaluator_.evaluate(expr);
}

void ConfigLoader::handleEval(const std::string& value) {
    const std::string expr = trim(value);
    // Assignment: "name = expression" or "name = expr".
    const size_t eq = expr.find('=');
    const bool prevIsOperator =
        eq > 0 && (expr[eq - 1] == '=' || expr[eq - 1] == '!' || expr[eq - 1] == '<' ||
                   expr[eq - 1] == '>');
    const bool nextIsEquals = eq + 1 < expr.size() && expr[eq + 1] == '=';
    if (eq != std::string::npos && !prevIsOperator && !nextIsEquals) {
        const std::string name = trim(expr.substr(0, eq));
        const std::string rhs = trim(expr.substr(eq + 1));
        if (!name.empty() && std::isalpha(static_cast<unsigned char>(name[0]))) {
            try {
                evaluator_.setVariable(name, evalExpression(rhs));
            } catch (const std::exception& e) {
                if (log_) log_("Eval error: " + std::string(e.what()));
            }
            return;
        }
    }
    try {
        evalExpression(expr);
    } catch (const std::exception& e) {
        if (log_) log_("Eval error: " + std::string(e.what()));
    }
}

bool ConfigLoader::matchDevice(const std::string& deviceString, const std::string& pattern) {
    // Split on ';' into pattern groups; each group is a list of words that must
    // all be present. The special pattern "all" always matches.
    std::string value = trim(pattern) + ";";
    std::vector<std::vector<std::string>> groups;
    std::vector<std::string> current;
    std::string word;
    for (char c : value) {
        if (c == ' ' || c == ';') {
            if (!word.empty()) {
                current.push_back(word);
                word.clear();
            }
            if (c == ';' && !current.empty()) {
                groups.push_back(current);
                current.clear();
            }
        } else {
            word += c;
        }
    }

    // Remove GUID substrings from the device string for word matching.
    std::string deviceNoGuid;
    for (size_t i = 0; i < deviceString.size();) {
        if (deviceString[i] == '{') {
            const size_t end = deviceString.find('}', i);
            if (end != std::string::npos && end - i == 37) {
                i = end + 1;
                continue;
            }
        }
        deviceNoGuid += deviceString[i++];
    }

    for (const auto& group : groups) {
        if (group.size() == 1 && toLower(group[0]) == "all") return true;

        bool matches = true;
        for (const auto& w : group) {
            const std::string lw = toLower(w);
            const std::string& haystack = lw.find('{') != std::string::npos ? deviceString : deviceNoGuid;
            if (toLower(haystack).find(lw) == std::string::npos) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

void ConfigLoader::handleDevice(const std::string& value) {
    deviceMatches_ = matchDevice(ctx_.deviceString, value);
    if (log_)
        log_((deviceMatches_ ? "Device matches: " : "Device does not match: ") + trim(value));
}

void ConfigLoader::handleStage(const std::string& value) {
    stageMatches_ = false;
    for (const auto& part : splitWords(toLower(trim(value)))) {
        if (part == "pre-mix" && !ctx_.isCapture && ctx_.isPreMix) stageMatches_ = true;
        else if (part == "post-mix" && !ctx_.isCapture && !ctx_.isPreMix) stageMatches_ = true;
        else if (part == "capture" && ctx_.isCapture) stageMatches_ = true;
    }
}

void ConfigLoader::handleChannel(const std::string& value) {
    const std::vector<std::string> words = splitWords(value);
    std::vector<bool> selected(allChannels_.size(), false);
    bool all = false;
    for (const std::string& w : words) {
        std::string upper;
        for (char c : w) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper == "ALL") {
            all = true;
            continue;
        }
        const int idx = indexInAll(w, false);
        if (idx >= 0 && static_cast<size_t>(idx) < allChannels_.size()) selected[idx] = true;
        else if (log_)
            log_("Invalid channel position: " + w);
    }
    if (all) {
        currentChannels_.resize(allChannels_.size());
        for (size_t i = 0; i < allChannels_.size(); ++i) currentChannels_[i] = i;
        return;
    }
    currentChannels_.clear();
    for (size_t i = 0; i < allChannels_.size(); ++i)
        if (selected[i]) currentChannels_.push_back(i);
}

void ConfigLoader::handleIf(const std::string& command, const std::string& value) {
    if (command == "If") {
        IfFrame frame;
        if (ifStack_.empty() || ifStack_.back().falseCount == 0) {
            try {
                const bool truthy = evalExpression(trim(value)).truthy();
                if (truthy) frame.trueCount = 1;
                else {
                    frame.falseCount = 1;
                    frame.executeElse = true;
                }
            } catch (const std::exception& e) {
                if (log_) log_("If error: " + std::string(e.what()));
                frame.falseCount = 1;
            }
        } else {
            frame.falseCount = 1;
        }
        ifStack_.push_back(frame);
    } else if (command == "ElseIf") {
        if (ifStack_.empty()) {
            if (log_) log_("ElseIf without If");
            return;
        }
        IfFrame& frame = ifStack_.back();
        if (frame.falseCount == 0) {
            if (frame.trueCount == 0) {
                if (log_) log_("ElseIf without If");
            } else {
                frame.falseCount = 1;
                frame.trueCount = 0;
            }
        } else if (frame.falseCount == 1 && frame.executeElse) {
            try {
                if (evalExpression(trim(value)).truthy()) {
                    frame.falseCount = 0;
                    frame.trueCount = 1;
                    frame.executeElse = false;
                }
            } catch (const std::exception& e) {
                if (log_) log_("ElseIf error: " + std::string(e.what()));
            }
        }
    } else if (command == "Else") {
        if (ifStack_.empty()) {
            if (log_) log_("Else without If");
            return;
        }
        IfFrame& frame = ifStack_.back();
        if (frame.falseCount == 0) {
            if (frame.trueCount == 0) {
                if (log_) log_("Else without If");
            } else {
                frame.falseCount = 1;
                frame.trueCount = 0;
            }
        } else if (frame.falseCount == 1 && frame.executeElse) {
            frame.falseCount = 0;
            frame.trueCount = 1;
            frame.executeElse = false;
        }
    } else if (command == "EndIf") {
        if (ifStack_.empty()) {
            if (log_) log_("EndIf without If");
            return;
        }
        IfFrame& frame = ifStack_.back();
        if (frame.falseCount == 0) {
            if (frame.trueCount == 0) {
                if (log_) log_("EndIf without If");
            } else {
                frame.trueCount = 0;
            }
        } else {
            frame.falseCount = 0;
        }
        if (frame.falseCount == 0) frame.executeElse = false;
        ifStack_.pop_back();
    }

    // Suppress processing while inside a non-taken branch.
    if (!ifStack_.empty() && ifStack_.back().falseCount > 0) {
        // handled by currentActive()
    }
}

void ConfigLoader::addPreamp(const std::string& value) {
    const std::string v = normalizeDecimal(value);
    double db = 0.0;
    if (std::sscanf(v.c_str(), " %lf dB", &db) == 1) addToCurrent(std::make_shared<PreampFilter>(db));
}

void ConfigLoader::addFilter(const std::string& value) {
    const std::string v = normalizeDecimal(value);
    std::vector<std::string> tokens = splitWords(v);
    if (tokens.empty()) return;

    // Expect: ON <Type> ... ; the ON token is required by Equalizer APO.
    size_t i = 0;
    if (tokens[i] == "ON" || tokens[i] == "on") ++i;
    if (i >= tokens.size()) return;

    const std::string typeName = tokens[i++];
    if (typeName == "None") return;

    if (typeName == "IIR") {
        // Filter: ON IIR Order <m> Coefficients <b0..bm> <a0..am>
        unsigned order = 0;
        for (; i < tokens.size(); ++i) {
            if (tokens[i] == "Order" && i + 1 < tokens.size()) {
                order = static_cast<unsigned>(std::strtoul(tokens[i + 1].c_str(), nullptr, 10));
                i++;
            } else if (tokens[i] == "Coefficients") {
                ++i;
                break;
            }
        }
        std::vector<double> coeffs;
        for (; i < tokens.size(); ++i) coeffs.push_back(std::strtod(tokens[i].c_str(), nullptr));
        if (order < 1 || coeffs.size() != (order + 1) * 2) {
            if (log_) log_("Invalid IIR coefficient count");
            return;
        }
        addToCurrent(std::make_shared<IirFilter>(std::move(coeffs)));
        return;
    }

    FilterType type;
    if (!parseFilterTypeName(typeName, type)) {
        if (log_) log_("Invalid filter type: " + typeName);
        return;
    }

    const bool shelf = type == FilterType::LowShelf || type == FilterType::HighShelf;

    double freq = 0.0, gain = 0.0, param = 0.0;
    bool haveFreq = false, haveGain = false, haveParam = false;
    bool isBandwidthOrS = false;

    for (; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        const std::string lt = toLower(t);
        if (lt == "fc" && i + 1 < tokens.size()) {
            freq = std::strtod(tokens[++i].c_str(), nullptr);
            haveFreq = true;
            if (i + 1 < tokens.size() && toLower(tokens[i + 1]) == "hz") ++i;
        } else if (lt == "gain" && i + 1 < tokens.size()) {
            gain = std::strtod(tokens[++i].c_str(), nullptr);
            haveGain = true;
            if (i + 1 < tokens.size() && toLower(tokens[i + 1]) == "db") ++i;
        } else if (lt == "q" && i + 1 < tokens.size()) {
            param = std::strtod(tokens[++i].c_str(), nullptr);
            haveParam = true;
            isBandwidthOrS = false;
        } else if (lt == "bw" && i + 2 < tokens.size()) {
            if (toLower(tokens[i + 1]) == "oct") {
                param = std::strtod(tokens[i + 2].c_str(), nullptr);
                i += 2;
            } else {
                param = std::strtod(tokens[i + 1].c_str(), nullptr);
                i += 1;
            }
            haveParam = true;
            isBandwidthOrS = true;
        } else if (isNumberToken(t) && i + 1 < tokens.size() && toLower(tokens[i + 1]) == "db" &&
                   (shelf && i == 0)) {
            // Leading shelf slope: "LSC 6 dB Fc ...".
            param = std::strtod(t.c_str(), nullptr);
            haveParam = true;
            isBandwidthOrS = true;
        }
    }

    if (!haveFreq) {
        if (log_) log_("Filter without Fc: " + value);
        return;
    }

    const bool needsGain = type == FilterType::Peaking || shelf;
    if (!haveGain && needsGain) {
        if (log_) log_("Filter without Gain: " + value);
        return;
    }

    bool isCornerFreq = false;
    if (!haveParam || param == 0.0) {
        if (type == FilterType::Peaking || type == FilterType::AllPass) {
            if (log_) log_("Peaking/all-pass filter without Q or BW: " + value);
            return;
        }
        if (type == FilterType::LowPass || type == FilterType::HighPass ||
            type == FilterType::BandPass) {
            param = 0.7071067811865476;  // sqrt(1/2)
            isBandwidthOrS = false;
        } else if (shelf) {
            param = 0.9;  // default slope found experimentally by Equalizer APO
            isBandwidthOrS = true;
        } else if (type == FilterType::Notch) {
            param = 30.0;
            isBandwidthOrS = false;
        }
    } else if (shelf) {
        if (isBandwidthOrS) param /= 12.0;  // maximum S is 1 for a 12 dB slope
        if (typeName.back() != 'C') isCornerFreq = true;
    }

    const ParamKind kind = isBandwidthOrS ? ParamKind::ShelfSlope : ParamKind::Q;
    addToCurrent(
        std::make_shared<BiquadFilter>(type, gain, freq, param, kind, isCornerFreq));
}

void ConfigLoader::addDelay(const std::string& value) {
    const std::string v = normalizeDecimal(value);
    std::istringstream ss(v);
    double delay = -1.0;
    std::string unit;
    ss >> delay >> unit;
    if (delay < 0.0) return;
    const std::string lu = toLower(unit);
    if (lu == "ms") addToCurrent(std::make_shared<DelayFilter>(delay, true));
    else if (lu == "samples") addToCurrent(std::make_shared<DelayFilter>(delay, false));
}

void ConfigLoader::addCopy(const std::string& value) {
    std::vector<CopyAssignment> assignments;
    for (const std::string& token : splitWords(value)) {
        const size_t eq = token.find('=');
        if (eq == std::string::npos) continue;
        const std::string target = token.substr(0, eq);
        const std::string source = token.substr(eq + 1);

        CopyAssignment a;
        a.targetChannel = indexInAll(target, true);

        // Split the source on '+' while keeping dB factor suffixes intact.
        std::vector<std::string> summands;
        std::string cur;
        for (char c : source) {
            if (c == '+') {
                summands.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        summands.push_back(cur);

        for (const std::string& raw : summands) {
            if (raw.empty()) continue;
            std::vector<std::string> factors;
            std::string cur2;
            for (char c : raw) {
                if (c == '*') {
                    factors.push_back(cur2);
                    cur2.clear();
                } else {
                    cur2 += c;
                }
            }
            factors.push_back(cur2);

            std::string factor, channel;
            if (factors.size() == 2) {
                factor = factors[0];
                channel = factors[1];
            } else if (factors.size() == 1) {
                const std::string& f = factors[0];
                if (f == "0" || f.find('.') != std::string::npos) factor = f;
                else channel = f;
            }

            CopySummand summand;
            if (factor.empty()) {
                summand.factor = 1.0f;
            } else {
                double fv = std::strtod(factor.c_str(), nullptr);
                const std::string lf = toLower(factor);
                if (lf.size() > 2 && lf.substr(lf.size() - 2) == "db")
                    fv = std::pow(10.0, fv / 20.0);
                summand.factor = static_cast<float>(fv);
            }
            summand.sourceChannel = channel.empty() ? -1 : indexInAll(channel, false);
            a.sources.push_back(summand);
        }
        if (a.targetChannel >= 0 && !a.sources.empty()) assignments.push_back(std::move(a));
    }

    if (assignments.empty()) return;

    auto filter = std::make_shared<CopyFilter>(std::move(assignments));
    ChainEntry entry;
    entry.filter = filter;
    entry.inChannels.resize(allChannels_.size());
    for (size_t i = 0; i < allChannels_.size(); ++i) entry.inChannels[i] = i;
    // Output channels are all channels so the mixer can write to any target.
    entry.outChannels = entry.inChannels;
    chain_.entries.push_back(std::move(entry));
}

void ConfigLoader::addGraphicEq(const std::string& value) {
    const std::string v = normalizeDecimal(value);
    std::vector<EqNode> nodes;

    // Parse pairs of numbers separated by ';' (or whitespace).
    std::vector<double> nums;
    std::string cur;
    for (char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' ||
            c == 'e' || c == 'E') {
            cur += c;
        } else {
            if (!cur.empty()) {
                nums.push_back(std::strtod(cur.c_str(), nullptr));
                cur.clear();
            }
        }
    }
    if (!cur.empty()) nums.push_back(std::strtod(cur.c_str(), nullptr));

    for (size_t i = 0; i + 1 < nums.size(); i += 2) nodes.push_back(EqNode{nums[i], nums[i + 1]});
    if (nodes.empty()) return;
    addToCurrent(std::make_shared<GraphicEqFilter>(std::move(nodes)));
}

void ConfigLoader::addCompressor(const std::string& value) {
    // Compressor: Threshold <dB> Ratio <r> Attack <ms> Release <ms> Makeup <dB>
    //             Knee <dB> Limit <dB>
    // Missing parameters fall back to values tuned for speech.
    const std::string v = normalizeDecimal(value);
    const std::vector<std::string> tokens = splitWords(v);

    double threshold = -24.0, ratio = 3.0, attack = 5.0, release = 120.0;
    double makeup = 0.0, knee = 6.0, limit = -1.0;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string lt = toLower(tokens[i]);
        const bool hasNext = i + 1 < tokens.size();
        const double num = hasNext ? std::strtod(tokens[i + 1].c_str(), nullptr) : 0.0;
        if (!hasNext) break;
        if (lt == "threshold") { threshold = num; ++i; }
        else if (lt == "ratio") { ratio = num; ++i; }
        else if (lt == "attack") { attack = num; ++i; }
        else if (lt == "release") { release = num; ++i; }
        else if (lt == "makeup") { makeup = num; ++i; }
        else if (lt == "knee") { knee = num; ++i; }
        else if (lt == "limit") { limit = num; ++i; }
    }

    addToCurrent(std::make_shared<CompressorFilter>(threshold, ratio, attack, release, makeup,
                                                    knee, limit));
}

void ConfigLoader::addConvolution(const std::string& value, const std::string& path) {
    const std::string target = trim(value);
    if (target.empty()) return;
    const std::string resolved =
        isAbsolutePath(target) ? target : joinPath(dirName(path), target);

    std::vector<std::vector<double>> taps = loadWavImpulse(resolved);
    if (taps.empty()) {
        if (log_) log_("Cannot load impulse response: " + resolved);
        return;
    }
    addToCurrent(std::make_shared<ConvolutionFilter>(std::move(taps)));
}

}  // namespace apolite
