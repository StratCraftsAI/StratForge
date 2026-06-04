#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// ThinkOrSwim StDevAll: linear regression over trailing window with
/// upper/lower bands at ±1, ±2 standard deviations of residuals.
class TosStdevAll : public Indicator<TosStdevAll> {
public:
    explicit TosStdevAll(const Line<double>& source, std::size_t period = 30uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            upper1_.data().reserve(n);
            lower1_.data().reserve(n);
            upper2_.data().reserve(n);
            lower2_.data().reserve(n);
        }
        const auto idx = source_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(nan);
            upper1_.forward(nan);
            lower1_.forward(nan);
            upper2_.forward(nan);
            lower2_.forward(nan);
            return;
        }

        const double nd = static_cast<double>(period_);
        const double sum_x = nd * (nd - 1.0) / 2.0;
        const double sum_x2 = nd * (nd - 1.0) * (2.0 * nd - 1.0) / 6.0;

        double sum_y = 0.0;
        double sum_xy = 0.0;
        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = 0; i < period_; ++i) {
            const double y = source_.data()[start + i];
            sum_y += y;
            sum_xy += static_cast<double>(i) * y;
        }

        const double denom = nd * sum_x2 - sum_x * sum_x;
        const double slope = (denom == 0.0) ? 0.0 : (nd * sum_xy - sum_x * sum_y) / denom;
        const double intercept = (sum_y - slope * sum_x) / nd;
        const double linreg = slope * (nd - 1.0) + intercept;

        double sse = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double residual = source_.data()[start + i] - (slope * static_cast<double>(i) + intercept);
            sse += residual * residual;
        }
        const double stddev = std::sqrt(sse / nd);

        line_.forward(linreg);
        upper1_.forward(linreg + stddev);
        lower1_.forward(linreg - stddev);
        upper2_.forward(linreg + 2.0 * stddev);
        lower2_.forward(linreg - 2.0 * stddev);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] const Line<double>& linreg() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& upper1() const noexcept { return upper1_; }
    [[nodiscard]] const Line<double>& lower1() const noexcept { return lower1_; }
    [[nodiscard]] const Line<double>& upper2() const noexcept { return upper2_; }
    [[nodiscard]] const Line<double>& lower2() const noexcept { return lower2_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    Line<double> upper1_;
    Line<double> lower1_;
    Line<double> upper2_;
    Line<double> lower2_;
};

using TOS_STDEVALL = TosStdevAll;

} // namespace stratforge
