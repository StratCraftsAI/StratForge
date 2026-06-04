#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Mass Index: sum of EMA(high-low) / EMA(EMA(high-low)) over trailing window.
/// Detects range expansion reversals ("reversal bulge").
class MassIndex : public Indicator<MassIndex> {
public:
    MassIndex(const Line<double>& high,
              const Line<double>& low,
              std::size_t ema_period = 9uz,
              std::size_t sum_period = 25uz)
        : high_(high), low_(low)
        , ema_period_(ema_period == 0 ? 1 : ema_period)
        , sum_period_(sum_period == 0 ? 1 : sum_period)
        , mult_(2.0 / (static_cast<double>(ema_period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            ratio_.data().reserve(high_.size());
        }
        const auto idx = high_.index();

        const double range = high_.data()[idx] - low_.data()[idx];

        if (!ema1_init_) {
            ema1_ = range;
            ema2_ = range;
            ema1_init_ = true;
        } else {
            ema1_ = (range - ema1_) * mult_ + ema1_;
            ema2_ = (ema1_ - ema2_) * mult_ + ema2_;
        }

        const double r = (ema2_ == 0.0) ? 1.0 : ema1_ / ema2_;
        ratio_.forward(r);

        if (ratio_.size() < sum_period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double sum = 0.0;
        for (std::size_t i = 0; i < sum_period_; ++i)
            sum += ratio_.data()[ratio_.size() - 1 - i];

        line_.forward(sum);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return sum_period_;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    std::size_t ema_period_;
    std::size_t sum_period_;
    double mult_;
    double ema1_ = 0.0;
    double ema2_ = 0.0;
    bool ema1_init_ = false;
    Line<double> ratio_;
};

using MASSI = MassIndex;

} // namespace stratforge
