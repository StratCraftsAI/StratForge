#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Candle Z-Score: z-score of the candle body relative to trailing candle bodies.
/// Body = close - open; z = (body - mean(bodies)) / std(bodies).
class CdlZ : public Indicator<CdlZ> {
public:
    CdlZ(const Line<double>& open,
         const Line<double>& close,
         std::size_t period = 30uz)
        : open_(open), close_(close), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            body_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        const double b = close_.data()[idx] - open_.data()[idx];
        body_.forward(b);

        if (body_.size() < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* p = &body_.data()[body_.size() - period_];
        const auto [mean, variance] = simd::reduce_mean_variance(p, period_);
        const double stddev = std::sqrt(variance);

        if (stddev == 0.0) {
            line_.forward(0.0);
            return;
        }
        line_.forward((b - mean) / stddev);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    std::size_t period_;
    Line<double> body_;
};

using CDL_Z = CdlZ;
using CandleZScore = CdlZ;

} // namespace stratforge
