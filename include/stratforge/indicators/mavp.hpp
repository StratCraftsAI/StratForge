#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Moving Average with Variable Period.
/// SMA-based moving average where the period is read per-bar from an input Line<double>.
/// Equivalent to TA-Lib MAVP.
class MAVP : public Indicator<MAVP> {
public:
    explicit MAVP(const Line<double>& source, const Line<double>& period_line,
                  std::size_t max_period = 50)
        : source_(source), period_line_(period_line),
          max_period_(max_period < 2 ? 2 : max_period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();

        // Read period from period_line, clamp to [2, max_period_]
        const double raw_period = period_line_.data()[idx];
        const auto period = static_cast<std::size_t>(
            std::clamp(raw_period, 2.0, static_cast<double>(max_period_)));

        if (idx + 1 < period) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double sum = 0.0;
        const auto* data = &source_.data()[idx - period + 1];
        for (std::size_t i = 0; i < period; ++i) {
            sum += data[i];
        }
        line_.forward(sum / static_cast<double>(period));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2; // minimum possible period value
    }

    [[nodiscard]] std::size_t max_period() const noexcept { return max_period_; }

private:
    const Line<double>& source_;
    const Line<double>& period_line_;
    std::size_t max_period_;
};

} // namespace stratforge
