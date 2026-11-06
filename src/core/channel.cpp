// ApoLite - channel layout and channel-name resolution.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/channel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace apolite {

namespace {
// Speaker bit constants from ksmedia.h, repeated here to keep this file
// independent of the Windows SDK.
constexpr unsigned kFrontLeft = 0x1;
constexpr unsigned kFrontRight = 0x2;
constexpr unsigned kFrontCenter = 0x4;
constexpr unsigned kLowFrequency = 0x8;
constexpr unsigned kBackLeft = 0x10;
constexpr unsigned kBackRight = 0x20;
constexpr unsigned kFrontCenterBack = 0x100;  // SPEAKER_BACK_CENTER
constexpr unsigned kSideLeft = 0x200;
constexpr unsigned kSideRight = 0x400;

struct PosName {
    unsigned bit;
    const char* name;
};

const PosName kPosNames[] = {
    {kFrontLeft, "L"},   {kFrontRight, "R"},   {kFrontCenter, "C"},
    {kLowFrequency, "LFE"}, {kBackLeft, "RL"}, {kBackRight, "RR"},
    {kFrontCenterBack, "RC"}, {kSideLeft, "SL"}, {kSideRight, "SR"},
};
}  // namespace

ChannelLayout::ChannelLayout(size_t channelCount, unsigned channelMask) {
    if (channelMask == 0) channelMask = defaultChannelMask(channelCount);
    names_ = namesForMask(channelCount, channelMask);
}

unsigned ChannelLayout::defaultChannelMask(size_t channelCount) {
    switch (channelCount) {
        case 1: return kFrontCenter;  // KSAUDIO_SPEAKER_MONO
        case 2: return kFrontLeft | kFrontRight;
        case 4: return kFrontLeft | kFrontRight | kBackLeft | kBackRight;
        case 6:
            return kFrontLeft | kFrontRight | kFrontCenter | kLowFrequency | kSideLeft | kSideRight;
        case 8:
            return kFrontLeft | kFrontRight | kFrontCenter | kLowFrequency | kBackLeft | kBackRight |
                   kSideLeft | kSideRight;
        default: return 0;
    }
}

std::vector<std::string> ChannelLayout::namesForMask(size_t channelCount, unsigned channelMask) {
    std::vector<std::string> names;
    size_t c = 1;
    for (unsigned i = 0; i < 31; ++i) {
        const unsigned bit = 1u << i;
        if (!(channelMask & bit)) continue;
        const PosName* found = nullptr;
        for (const auto& pn : kPosNames) {
            if (pn.bit == bit) {
                found = &pn;
                break;
            }
        }
        names.push_back(found ? found->name : std::to_string(c));
        ++c;
    }
    for (; c <= channelCount; ++c) names.push_back(std::to_string(c));
    return names;
}

int ChannelLayout::indexOf(const std::string& word, bool allowNew) {
    if (word.empty()) return -1;

    if (std::isdigit(static_cast<unsigned char>(word[0]))) {
        const long n = std::strtol(word.c_str(), nullptr, 10) - 1;
        if (n < 0 || static_cast<size_t>(n) >= names_.size()) return -1;
        return static_cast<int>(n);
    }

    std::string upper;
    upper.reserve(word.size());
    for (char c : word) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    auto find = [&](const std::string& n) {
        return std::find(names_.begin(), names_.end(), n) - names_.begin();
    };

    int idx = -1;
    const auto it = std::find(names_.begin(), names_.end(), upper);
    if (it != names_.end()) {
        idx = static_cast<int>(it - names_.begin());
    } else {
        // Accept "wrong" but unambiguous positions, as Equalizer APO does.
        static const char* aliases[][2] = {
            {"SL", "RL"}, {"SR", "RR"}, {"RL", "SL"}, {"RR", "SR"}, {"SUB", "LFE"}};
        for (const auto& alias : aliases) {
            if (upper == alias[0]) {
                const auto a = std::find(names_.begin(), names_.end(), std::string(alias[1]));
                if (a != names_.end()) idx = static_cast<int>(a - names_.begin());
                break;
            }
        }
    }

    if (idx == -1 && allowNew) {
        names_.push_back(upper);
        idx = static_cast<int>(names_.size() - 1);
    }
    (void)find;
    return idx;
}

}  // namespace apolite
