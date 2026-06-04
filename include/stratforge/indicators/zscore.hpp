#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Rolling Z-Score: (value - mean) / stddev over trailing window.
class ZScore : public Indicator<ZScore> {
public:
    explicit ZScore(const Line<double>& source, std::size_t period = 30uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto [mean, variance] = simd::reduce_mean_variance(
            &source_.data()[idx - period_ + 1], period_);
        const double stddev = std::sqrt(variance);
        if (stddev == 0.0) {
            line_.forward(0.0);
            return;
        }
        line_.forward((source_.data()[idx] - mean) / stddev);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using ZSCORE = ZScore;

} // namespace stratforge
