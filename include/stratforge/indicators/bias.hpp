#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// BIAS = (close - SMA(close, period)) / SMA(close, period)
class Bias : public Indicator<Bias> {
public:
    explicit Bias(const Line<double>& source, std::size_t period = 26uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double sma = simd::reduce_sum(&source_.data()[idx - period_ + 1], period_) /
                           static_cast<double>(period_);
        if (sma == 0.0) {
            line_.forward(0.0);
            return;
        }
        line_.forward((source_.data()[idx] - sma) / sma);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using BIAS = Bias;

} // namespace stratforge
