#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Efficiency Ratio: abs(net change) / sum(abs(bar-to-bar changes)) over period.
/// Also known as Kaufman's ER (used internally by KAMA).
class EfficiencyRatio : public Indicator<EfficiencyRatio> {
public:
    explicit EfficiencyRatio(const Line<double>& source, std::size_t period = 10uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double net_change = std::abs(source_.data()[idx] - source_.data()[idx - period_]);
        double volatility = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            volatility += std::abs(source_.data()[idx - i] - source_.data()[idx - i - 1]);
        }

        line_.forward(volatility == 0.0 ? 0.0 : net_change / volatility);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using ER = EfficiencyRatio;

} // namespace stratforge
