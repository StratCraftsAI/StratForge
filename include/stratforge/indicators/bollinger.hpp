#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Bollinger Bands with middle, top, and bottom output lines.
class BollingerBands : public Indicator<BollingerBands> {
public:
    explicit BollingerBands(const Line<double>& source, std::size_t period = 20uz, double devfactor = 2.0)
        : source_(source), period_(period == 0 ? 1 : period), devfactor_(devfactor) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            top_.data().reserve(n);
            bottom_.data().reserve(n);
        }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            line_.forward(nan);
            top_.forward(nan);
            bottom_.forward(nan);
            return;
        }

        const auto [mean, variance] = simd::reduce_mean_variance(
            &source_.data()[idx - period_ + 1], period_);
        const double stddev = std::sqrt(variance);
        line_.forward(mean);
        top_.forward(mean + devfactor_ * stddev);
        bottom_.forward(mean - devfactor_ * stddev);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] double devfactor() const noexcept { return devfactor_; }

    [[nodiscard]] const Line<double>& mid() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& top() const noexcept { return top_; }
    [[nodiscard]] const Line<double>& bottom() const noexcept { return bottom_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double devfactor_;
    Line<double> top_;
    Line<double> bottom_;
};

} // namespace stratforge
