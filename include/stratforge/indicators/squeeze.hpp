#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// TTM Squeeze: Bollinger Bands vs Keltner Channels squeeze detector.
/// line() = momentum histogram, squeeze_on() = 1 when BB inside KC.
class Squeeze : public Indicator<Squeeze> {
public:
    explicit Squeeze(const Line<double>& high,
                     const Line<double>& low,
                     const Line<double>& close,
                     std::size_t period = 20uz,
                     double bb_mult = 2.0,
                     double kc_mult = 1.5)
        : high_(high), low_(low), close_(close)
        , period_(period == 0 ? 1 : period)
        , bb_mult_(bb_mult), kc_mult_(kc_mult) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            squeeze_on_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(nan);
            squeeze_on_.forward(nan);
            return;
        }

        const double* cp = &close_.data()[idx - period_ + 1];
        const auto [mean, variance] = simd::reduce_mean_variance(cp, period_);
        const double bb_upper = mean + bb_mult_ * std::sqrt(variance);
        const double bb_lower = mean - bb_mult_ * std::sqrt(variance);

        double tr_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const std::size_t pos = idx - period_ + 1 + i;
            double tr = high_.data()[pos] - low_.data()[pos];
            if (pos > 0) {
                const double prev_c = close_.data()[pos - 1];
                tr = std::max(tr, std::abs(high_.data()[pos] - prev_c));
                tr = std::max(tr, std::abs(low_.data()[pos] - prev_c));
            }
            tr_sum += tr;
        }
        const double atr = tr_sum / static_cast<double>(period_);
        const double kc_upper = mean + kc_mult_ * atr;
        const double kc_lower = mean - kc_mult_ * atr;

        squeeze_on_.forward((bb_lower > kc_lower && bb_upper < kc_upper) ? 1.0 : 0.0);

        const double n = static_cast<double>(period_);
        const double sum_x = n * (n - 1.0) / 2.0;
        const double sum_x2 = n * (n - 1.0) * (2.0 * n - 1.0) / 6.0;
        double sum_y = 0.0;
        double sum_xy = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const std::size_t pos = idx - period_ + 1 + i;
            const double hl_mid = (high_.data()[pos] + low_.data()[pos]) / 2.0;
            const double delta = close_.data()[pos] - (hl_mid + mean) / 2.0;
            sum_y += delta;
            sum_xy += static_cast<double>(i) * delta;
        }
        const double denom = n * sum_x2 - sum_x * sum_x;
        const double slope = (denom == 0.0) ? 0.0 : (n * sum_xy - sum_x * sum_y) / denom;
        const double intercept = (sum_y - slope * sum_x) / n;
        line_.forward(slope * (n - 1.0) + intercept);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] const Line<double>& momentum() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& squeeze_on() const noexcept { return squeeze_on_; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t period_;
    double bb_mult_;
    double kc_mult_;
    Line<double> squeeze_on_;
};

using SQUEEZE = Squeeze;
using TTMSqueeze = Squeeze;

} // namespace stratforge
