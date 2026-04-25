#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Weighted Moving Average with newest bars carrying the largest weight.
class WMA : public Indicator<WMA> {
public:
    explicit WMA(const Line<double>& source, std::size_t period)
        : source_(source)
        , period_(period)
        , denominator_(static_cast<double>(period * (period + 1)) / 2.0) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double weighted_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double weight = static_cast<double>(period_ - i);
            weighted_sum += source_.data()[idx - i] * weight;
        }

        line_.forward(weighted_sum / denominator_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
    double denominator_;
};

} // namespace stratforge
