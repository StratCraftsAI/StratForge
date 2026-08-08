// SPDX-License-Identifier: MIT
//
//  P4: Sizing policies -- Gate, Sizing, Hybrid.
// Monotonic, bounded, never enlarges beyond proposed_size (D7).

#pragma once

#include <stratforge/corrective/corrective_contracts.hpp>

#include <algorithm>
#include <cmath>

namespace stratforge::corrective {

struct SizingDecision {
    GateVerdict verdict    = GateVerdict::kPass;
    double      final_size = 0.0;
};

class PopSizingPolicy {
public:
    PopSizingPolicy() = default;

    explicit PopSizingPolicy(SizingPolicy mode, float threshold,
                              float exponent) noexcept
        : mode_(mode)
        , threshold_(threshold)
        , exponent_(std::clamp(exponent, 0.1f, 5.0f))
    {}

    [[nodiscard]] SizingDecision apply(float calibrated_probability,
                                        double proposed_size) const noexcept
    {
        switch (mode_) {
            case SizingPolicy::kGate:
                return apply_gate_(calibrated_probability, proposed_size);
            case SizingPolicy::kSizing:
                return apply_sizing_(calibrated_probability, proposed_size);
            case SizingPolicy::kHybrid:
                return apply_hybrid_(calibrated_probability, proposed_size);
        }
        return {GateVerdict::kPass, proposed_size};
    }

    [[nodiscard]] SizingPolicy mode() const noexcept { return mode_; }
    [[nodiscard]] float threshold() const noexcept { return threshold_; }
    [[nodiscard]] float exponent() const noexcept { return exponent_; }

private:
    // Gate: below threshold -> reject (size 0), otherwise pass unchanged
    [[nodiscard]] SizingDecision apply_gate_(
        float prob, double proposed) const noexcept
    {
        if (prob < threshold_) {
            return {GateVerdict::kReject, 0.0};
        }
        return {GateVerdict::kPass, proposed};
    }

    // Sizing: scale by pow(prob, exponent), never enlarge
    [[nodiscard]] SizingDecision apply_sizing_(
        float prob, double proposed) const noexcept
    {
        const double scale = std::pow(
            static_cast<double>(std::clamp(prob, 0.0f, 1.0f)),
            static_cast<double>(exponent_));
        const double sized = proposed * scale;
        return {GateVerdict::kPass, std::min(sized, proposed)};
    }

    // Hybrid: gate first, then apply sizing to survivors
    [[nodiscard]] SizingDecision apply_hybrid_(
        float prob, double proposed) const noexcept
    {
        if (prob < threshold_) {
            return {GateVerdict::kReject, 0.0};
        }
        return apply_sizing_(prob, proposed);
    }

    SizingPolicy mode_      = SizingPolicy::kGate;
    float        threshold_ = 0.5f;
    float        exponent_  = 1.0f;
};

} // namespace stratforge::corrective
