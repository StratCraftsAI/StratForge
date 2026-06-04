#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Correlation Trend Indicator: Spearman correlation of price vs time index.
/// +1 = perfect uptrend, -1 = perfect downtrend, 0 = no trend.
class CTI : public Indicator<CTI> {
public:
    explicit CTI(const Line<double>& source, std::size_t period = 12uz)
        : source_(source), period_(period < 2 ? 2 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const std::size_t start = idx - period_ + 1;
        const double n = static_cast<double>(period_);

        rank_.resize(period_);
        for (std::size_t i = 0; i < period_; ++i) rank_[i] = i;
        std::sort(rank_.begin(), rank_.end(), [&](std::size_t a, std::size_t b) {
            return source_.data()[start + a] < source_.data()[start + b];
        });

        price_ranks_.resize(period_);
        for (std::size_t i = 0; i < period_; ++i)
            price_ranks_[rank_[i]] = static_cast<double>(i + 1);

        double sum_d2 = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double d = price_ranks_[i] - static_cast<double>(i + 1);
            sum_d2 += d * d;
        }

        line_.forward(1.0 - (6.0 * sum_d2) / (n * (n * n - 1.0)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::vector<std::size_t> rank_;
    std::vector<double> price_ranks_;
};

using CorrelationTrendIndicator = CTI;

} // namespace stratforge
