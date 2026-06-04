#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Volume-Weighted Moving Average.
/// VWMA = sum(close * volume, period) / sum(volume, period)
class VWMA : public Indicator<VWMA> {
public:
    VWMA(const Line<double>& source, const Line<double>& volume, std::size_t period = 20uz)
        : source_(source), volume_(volume), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double pv_sum = 0.0;
        double vol_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const std::size_t pos = idx - i;
            pv_sum += source_.data()[pos] * volume_.data()[pos];
            vol_sum += volume_.data()[pos];
        }

        line_.forward(vol_sum == 0.0 ? source_.data()[idx] : pv_sum / vol_sum);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    const Line<double>& volume_;
    std::size_t period_;
};

using VolumeWeightedMA = VWMA;

} // namespace stratforge
