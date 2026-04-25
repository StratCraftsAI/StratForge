#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Triangular Moving Average: double-smoothed SMA.
/// For even period N: SMA(ceil(N/2)) of SMA(floor(N/2)+1)
/// For odd period N: SMA((N+1)/2) of SMA((N+1)/2)
/// Simplified: computed directly as weighted window.
class TRIMA : public Indicator<TRIMA> {
public:
    explicit TRIMA(const Line<double>& source, std::size_t period = 20)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        // TRIMA uses triangular weights
        // For period N, weight[i] = min(i+1, N-i) for i=0..N-1
        double weighted_sum = 0.0;
        double weight_total = 0.0;
        const std::size_t start = idx - period_ + 1;

        for (std::size_t i = 0; i < period_; ++i) {
            const double weight = static_cast<double>(
                std::min(i + 1, period_ - i));
            weighted_sum += source_.data()[start + i] * weight;
            weight_total += weight;
        }

        line_.forward(weighted_sum / weight_total);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using TriangularMovingAverage = TRIMA;

} // namespace stratforge
