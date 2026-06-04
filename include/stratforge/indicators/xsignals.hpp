#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Cross Signals: detects crossovers between two lines.
/// Outputs +1 (bullish cross), -1 (bearish cross), 0 (no cross).
class XSignals : public Indicator<XSignals> {
public:
    XSignals(const Line<double>& fast, const Line<double>& slow, std::size_t period = 13uz)
        : fast_(fast), slow_(slow)
        , period_(period == 0 ? 1 : period)
        , fast_mult_(2.0 / (static_cast<double>(period_) + 1.0))
        , slow_mult_(2.0 / (static_cast<double>(period_ * 2) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(fast_.size()); }
        const auto idx = fast_.index();

        if (idx == 0) {
            prev_above_ = fast_.data()[idx] > slow_.data()[idx];
            line_.forward(0.0);
            return;
        }

        const bool above = fast_.data()[idx] > slow_.data()[idx];

        if (above && !prev_above_)
            line_.forward(1.0);
        else if (!above && prev_above_)
            line_.forward(-1.0);
        else
            line_.forward(0.0);

        prev_above_ = above;
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& fast_;
    const Line<double>& slow_;
    std::size_t period_;
    double fast_mult_;
    double slow_mult_;
    bool prev_above_ = false;
};

using XSIGNALS = XSignals;
using CrossSignals = XSignals;

} // namespace stratforge
