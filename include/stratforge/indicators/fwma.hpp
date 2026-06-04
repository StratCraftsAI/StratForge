#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Fibonacci Weighted Moving Average.
/// Weights follow the Fibonacci sequence: 1, 1, 2, 3, 5, 8, ...
class FWMA : public Indicator<FWMA> {
public:
    explicit FWMA(const Line<double>& source, std::size_t period = 20uz)
        : source_(source), period_(period == 0 ? 1 : period) {
        fib_weights_.resize(period_);
        if (period_ >= 1) fib_weights_[0] = 1.0;
        if (period_ >= 2) fib_weights_[1] = 1.0;
        for (std::size_t i = 2; i < period_; ++i)
            fib_weights_[i] = fib_weights_[i - 1] + fib_weights_[i - 2];

        weight_sum_ = 0.0;
        for (std::size_t i = 0; i < period_; ++i)
            weight_sum_ += fib_weights_[i];
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
            weighted_sum += source_.data()[start + i] * fib_weights_[i];

        line_.forward(weighted_sum / weight_sum_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::vector<double> fib_weights_;
    double weight_sum_;
};

using FibonacciWeightedMA = FWMA;

} // namespace stratforge
