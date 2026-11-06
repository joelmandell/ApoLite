// ApoLite - channel layout and channel-name resolution.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <string>
#include <vector>

namespace apolite {

// Maps between channel identifiers ("L", "R", "C", "LFE", "SL", ...), numeric
// positions (1-based) and channel indices, following Equalizer APO's rules.
class ChannelLayout {
public:
    ChannelLayout() = default;
    ChannelLayout(size_t channelCount, unsigned channelMask);

    size_t count() const { return names_.size(); }
    const std::vector<std::string>& names() const { return names_; }

    // Resolve an identifier to a 0-based channel index, or -1 if not found.
    // When `allowNew` is true, an unknown name is appended and its new index
    // returned (used by the Copy command's target channel).
    int indexOf(const std::string& word, bool allowNew = false);

    static unsigned defaultChannelMask(size_t channelCount);
    static std::vector<std::string> namesForMask(size_t channelCount, unsigned channelMask);

private:
    std::vector<std::string> names_;
};

}  // namespace apolite
