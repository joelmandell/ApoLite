// ApoLite - Equalizer APO compatible configuration loader.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apolite/expression.h"
#include "apolite/filter.h"

namespace apolite {

// A filter bound to the channels it reads from and writes to. Indices refer to
// positions in FilterChain::channelNames. For most filters the two lists are
// identical; Copy reads from every channel and writes to its targets.
struct ChainEntry {
    std::shared_ptr<IFilter> filter;
    std::vector<size_t> inChannels;
    std::vector<size_t> outChannels;
};

// A fully resolved processing chain.
struct FilterChain {
    std::vector<std::string> channelNames;
    std::vector<ChainEntry> entries;

    bool empty() const { return entries.empty(); }
};

// Static description of the device and stage the chain is being built for.
struct DeviceContext {
    double sampleRate = 48000.0;
    size_t channelCount = 2;
    unsigned channelMask = 0;
    bool isCapture = false;
    bool isPreMix = false;
    bool postMixInstalled = true;

    std::string deviceName;
    std::string connectionName;
    std::string deviceGuid;

    // " Device_name Connection_name GUID ", as Equalizer APO builds it.
    std::string deviceString;
    std::string stageName;  // "pre-mix", "post-mix" or "capture"
};

// Receives diagnostic messages (unknown commands, parse errors, ...).
using LogSink = std::function<void(const std::string&)>;

class ConfigLoader {
public:
    ConfigLoader(DeviceContext context, LogSink log);

    // Load a configuration file. Relative Include paths resolve against the
    // including file's directory.
    FilterChain loadFile(const std::string& path);

    // Load configuration from an in-memory string; `basePath` is used to
    // resolve relative Include paths.
    FilterChain loadText(const std::string& text, const std::string& basePath);

private:
    void parseInto(const std::string& text, const std::string& path, int depth);
    void handleLine(const std::string& key, const std::string& value,
                    const std::string& path);

    void addPreamp(const std::string& value);
    void addFilter(const std::string& value);
    void addDelay(const std::string& value);
    void addCopy(const std::string& value);
    void addGraphicEq(const std::string& value);
    void addConvolution(const std::string& value, const std::string& path);
    void addCompressor(const std::string& value);
    void handleChannel(const std::string& value);
    void handleDevice(const std::string& value);
    void handleStage(const std::string& value);
    void handleIf(const std::string& command, const std::string& value);
    void handleEval(const std::string& value);

    // Append a filter entry that processes the currently selected channels.
    void addToCurrent(std::shared_ptr<IFilter> filter);

    std::string substituteInline(const std::string& value);
    Value evalExpression(const std::string& expr) const;
    bool currentActive() const;
    int indexInAll(const std::string& word, bool allowNew);
    static bool matchDevice(const std::string& deviceString, const std::string& pattern);

    DeviceContext ctx_;
    LogSink log_;

    std::vector<std::string> allChannels_;
    std::vector<size_t> currentChannels_;
    FilterChain chain_;

    ExpressionEvaluator evaluator_;

    // Control-flow state.
    bool deviceMatches_ = true;
    bool stageMatches_ = true;
    struct IfFrame {
        int trueCount = 0;
        int falseCount = 0;
        bool executeElse = false;
    };
    std::vector<IfFrame> ifStack_;

    // Saved on Include so channel/device selection does not leak outwards.
    struct FileState {
        std::vector<size_t> currentChannels;
        bool deviceMatches;
        bool stageMatches;
        std::vector<IfFrame> ifStack;
    };
};

}  // namespace apolite
