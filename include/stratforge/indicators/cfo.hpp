#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Chande Forecast Oscillator: 100 * (close - LinReg) / close
/// Measures deviation from the linear regression forecast.
class CFO : public Indicator<CFO> {
public:
    explicit CFO(const Line<double>& source, std::size_t period = 9uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double n = static_cast<double>(period_);
        const double sum_x = n * (n - 1.0) / 2.0;
        const double sum_x2 = n * (n - 1.0) * (2.0 * n - 1.0) / 6.0;

        double sum_y = 0.0;
        double sum_xy = 0.0;
        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = 0; i < period_; ++i) {
            const double y = source_.data()[start + i];
            sum_y += y;
            sum_xy += static_cast<double>(i) * y;
        }

        const double denom = n * sum_x2 - sum_x * sum_x;
        const double slope = (denom == 0.0) ? 0.0 : (n * sum_xy - sum_x * sum_y) / denom;
        const double intercept = (sum_y - slope * sum_x) / n;
        const double linreg = slope * (n - 1.0) + intercept;

        const double close = source_.data()[idx];
        if (close == 0.0) {
            line_.forward(0.0);
            return;
        }
        line_.forward(100.0 * (close - linreg) / close);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using ChandeForecastOscillator = CFO;

} // namespace stratforge
