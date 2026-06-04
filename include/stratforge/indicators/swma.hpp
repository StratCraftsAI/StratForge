#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Symmetric Weighted Moving Average.
/// Weights form a symmetric triangle: 1, 2, ..., mid, ..., 2, 1
class SWMA : public Indicator<SWMA> {
public:
    explicit SWMA(const Line<double>& source, std::size_t period = 10uz)
        : source_(source), period_(period == 0 ? 1 : period) {
        weights_.resize(period_);
        const double mid = static_cast<double>(period_ - 1) / 2.0;
        weight_sum_ = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            weights_[i] = mid - std::abs(static_cast<double>(i) - mid) + 1.0;
            weight_sum_ += weights_[i];
        }
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

using SymmetricWeightedMA = SWMA;

} // namespace stratforge
