#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Acceleration Bands: SMA-based bands scaled by (high - low) / (high + low).
class AccelerationBands : public Indicator<AccelerationBands> {
public:
    AccelerationBands(const Line<double>& high,
                      const Line<double>& low,
                      const Line<double>& close,
                      std::size_t period = 20uz,
                      double mult = 4.0)
        : high_(high), low_(low), close_(close)
        , period_(period == 0 ? 1 : period), mult_(mult) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            upper_.data().reserve(n);
            lower_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(nan);
            upper_.forward(nan);
            lower_.forward(nan);
            return;
        }

        double sum_mid = 0.0;
        double sum_upper = 0.0;
        double sum_lower = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const std::size_t pos = idx - i;
            const double h = high_.data()[pos];
            const double l = low_.data()[pos];
            const double c = close_.data()[pos];
            const double hl_sum = h + l;
            const double band_factor = (hl_sum == 0.0) ? 0.0 : mult_ * (h - l) / hl_sum;
            sum_mid += c;
            sum_upper += h * (1.0 + band_factor);
            sum_lower += l * (1.0 - band_factor);
        }

        const double n = static_cast<double>(period_);
        line_.forward(sum_mid / n);
        upper_.forward(sum_upper / n);
        lower_.forward(sum_lower / n);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] const Line<double>& mid() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& upper() const noexcept { return upper_; }
    [[nodiscard]] const Line<double>& lower() const noexcept { return lower_; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t period_;
    double mult_;
    Line<double> upper_;
    Line<double> lower_;
};

using ACCBANDS = AccelerationBands;

} // namespace stratforge
