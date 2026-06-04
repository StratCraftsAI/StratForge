#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Choppiness Index: 100 * LOG10(sum(ATR,period) / (highest_high - lowest_low)) / LOG10(period)
/// Values near 100 = choppy, near 0 = trending.
class Choppiness : public Indicator<Choppiness> {
public:
    Choppiness(const Line<double>& high,
               const Line<double>& low,
               const Line<double>& close,
               std::size_t period = 14uz)
        : high_(high), low_(low), close_(close)
        , period_(period == 0 ? 1 : period)
        , log_period_(std::log10(static_cast<double>(period_))) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double atr_sum = 0.0;
        double highest = high_.data()[idx - period_ + 1];
        double lowest = low_.data()[idx - period_ + 1];

        for (std::size_t i = 0; i < period_; ++i) {
            const std::size_t pos = idx - period_ + 1 + i;
            highest = std::max(highest, high_.data()[pos]);
            lowest = std::min(lowest, low_.data()[pos]);

            double tr = high_.data()[pos] - low_.data()[pos];
            if (pos > 0) {
                const double prev_c = close_.data()[pos - 1];
                tr = std::max(tr, std::abs(high_.data()[pos] - prev_c));
                tr = std::max(tr, std::abs(low_.data()[pos] - prev_c));
            }
            atr_sum += tr;
        }

        const double range = highest - lowest;
        if (range == 0.0 || log_period_ == 0.0) {
            line_.forward(0.0);
            return;
        }

        line_.forward(100.0 * std::log10(atr_sum / range) / log_period_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t period_;
    double log_period_;
};

using CHOP = Choppiness;
using ChoppinessIndex = Choppiness;

} // namespace stratforge
