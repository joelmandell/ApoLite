// ApoLite - log-frequency gain interpolation for GraphicEQ.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/gain.h"

#include <algorithm>
#include <cmath>

namespace apolite {

GainInterpolator::GainInterpolator(std::vector<EqNode> nodes) : nodes_(std::move(nodes)) {
    std::stable_sort(nodes_.begin(), nodes_.end(),
                     [](const EqNode& a, const EqNode& b) { return a.freq < b.freq; });
    logFreq_.resize(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i)
        logFreq_[i] = std::log(nodes_[i].freq > 0.0 ? nodes_[i].freq : 1e-6);
}

double GainInterpolator::gainAt(double freq) const {
    if (nodes_.empty()) return 0.0;
    if (nodes_.size() == 1) return nodes_[0].gainDb;

    const double lf = std::log(freq > 0.0 ? freq : 1e-6);
    if (lf <= logFreq_.front()) return nodes_.front().gainDb;
    if (lf >= logFreq_.back()) return nodes_.back().gainDb;

    const auto it = std::upper_bound(logFreq_.begin(), logFreq_.end(), lf);
    const size_t right = static_cast<size_t>(it - logFreq_.begin());
    const size_t left = right - 1;

    const double span = logFreq_[right] - logFreq_[left];
    if (span <= 0.0) return nodes_[left].gainDb;
    const double t = (lf - logFreq_[left]) / span;
    return nodes_[left].gainDb + t * (nodes_[right].gainDb - nodes_[left].gainDb);
}

}  // namespace apolite
