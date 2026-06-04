#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>
#include <vector>

namespace stratforge {

/// Pascal Weighted Moving Average.
/// Weights are the nth row of Pascal's triangle (binomial coefficients).
class PWMA : public Indicator<PWMA> {
public:
    explicit PWMA(const Line<double>& source, std::size_t period = 10uz)
        : source_(source), period_(period == 0 ? 1 : period) {
        weights_.resize(period_);
        weights_[0] = 1.0;
        const std::size_t n = period_ - 1;
        for (std::size_t k = 1; k < period_; ++k)
            weights_[k] = weights_[k - 1] * static_cast<double>(n - k + 1) / static_cast<double>(k);

        weight_sum_ = 0.0;
        for (std::size_t i = 0; i < period_; ++i)
            weight_sum_ += weights_[i];
    }

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const std::size_t start = idx - period_ + 1;
        double weighted_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i)
            weighted_sum += source_.data()[start + i] * weights_[i];

        line_.forward(weighted_sum / weight_sum_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::vector<double> weights_;
    double weight_sum_;
};

using PascalWeightedMA = PWMA;

} // namespace stratforge
