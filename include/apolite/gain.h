// ApoLite - log-frequency gain interpolation for GraphicEQ.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <vector>

namespace apolite {

struct EqNode {
    double freq = 0.0;
    double gainDb = 0.0;
};

// Piecewise-linear interpolation of gain over log frequency, matching the
// behaviour of Equalizer APO's GraphicEQ node list. Nodes must be sorted by
// frequency.
class GainInterpolator {
public:
    explicit GainInterpolator(std::vector<EqNode> nodes);

    // Linear-in-dB, linear-in-log(f) interpolation. Frequencies below the first
    // node or above the last node clamp to the nearest node.
    double gainAt(double freq) const;

    const std::vector<EqNode>& nodes() const { return nodes_; }

private:
    std::vector<EqNode> nodes_;
    std::vector<double> logFreq_;
};

}  // namespace apolite
