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
#include "apolite/fir.h"

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

// What a configuration produced when it was loaded, without publishing it.
struct ConfigCheck {
    size_t filters = 0;
    // The same messages the APO logs, so an editor or a tool can show the user
    // why a filter was dropped without reading the APO's log file.
    std::vector<std::string> messages;

    // True when at least one message reports a dropped filter.
    bool hasDroppedFilter() const;
};

// Load a configuration into a throwaway chain and report what happened. Used to
// warn about a problem before the user relies on the file; it publishes nothing
// and has no side effects on any running engine.
ConfigCheck checkConfigurationFile(const std::string& path, const DeviceContext& context);

// The same check for configuration text that has not been written to disk yet,
// so an editor can validate what it is about to save.
ConfigCheck checkConfigurationText(const std::string& text, const std::string& basePath,
                                   const DeviceContext& context);

class ConfigLoader {
public:
    ConfigLoader(DeviceContext context, LogSink log);

    // Ceiling on the heap the impulse convolvers in this configuration may hold,
    // in bytes. A Convolution whose impulse does not fit is refused, and the
    // refusal names the file. Defaults to kImpulseMemoryBudgetBytes. Raise it to
    // allow a deliberately long impulse; it is a budget, not a tap limit, so a
    // mono device and a four-channel one are held to the same memory.
    void setImpulseMemoryBudget(size_t bytes) { impulseBudgetBytes_ = bytes; }
    size_t impulseMemoryBudget() const { return impulseBudgetBytes_; }

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
    size_t impulseBudgetBytes_ = kImpulseMemoryBudgetBytes;

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
