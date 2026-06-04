#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Relative Volatility Index: RSI applied to standard deviation instead of price.
class RVI : public Indicator<RVI> {
public:
    explicit RVI(const Line<double>& source, std::size_t period = 14uz,
                 std::size_t stddev_period = 10uz)
        : source_(source)
        , period_(period == 0 ? 1 : period)
        , stddev_period_(stddev_period == 0 ? 1 : stddev_period)
        , multiplier_(2.0 / (static_cast<double>(period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();

        if (idx + 1 < stddev_period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto [mean, variance] = simd::reduce_mean_variance(
            &source_.data()[idx - stddev_period_ + 1], stddev_period_);
        const double stddev = std::sqrt(variance);

        if (idx < stddev_period_) {
            prev_stddev_ = stddev;
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double change = stddev - prev_stddev_;
        prev_stddev_ = stddev;
        const double gain = change > 0.0 ? change : 0.0;
        const double loss = change < 0.0 ? -change : 0.0;

        if (!initialized_) {
            avg_gain_ = gain;
            avg_loss_ = loss;
            initialized_ = true;
        } else {
            avg_gain_ = (gain - avg_gain_) * multiplier_ + avg_gain_;
            avg_loss_ = (loss - avg_loss_) * multiplier_ + avg_loss_;
        }

        if (avg_loss_ == 0.0) {
            line_.forward(100.0);
            return;
        }
        line_.forward(100.0 - 100.0 / (1.0 + avg_gain_ / avg_loss_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return stddev_period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::size_t stddev_period_;
    double multiplier_;
    double avg_gain_ = 0.0;
    double avg_loss_ = 0.0;
    double prev_stddev_ = 0.0;
    bool initialized_ = false;
};

using RelativeVolatilityIndex = RVI;

} // namespace stratforge
