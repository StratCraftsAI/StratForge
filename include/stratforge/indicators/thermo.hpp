#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Elder Thermometer: measures bar volatility relative to trailing average.
/// Thermo = max(abs(high - high_prev), abs(low - low_prev))
/// line() = EMA(thermo, period)
class Thermometer : public Indicator<Thermometer> {
public:
    Thermometer(const Line<double>& high,
                const Line<double>& low,
                std::size_t period = 20uz)
        : high_(high), low_(low)
        , period_(period == 0 ? 1 : period)
        , multiplier_(2.0 / (static_cast<double>(period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(high_.size());
            thermo_.data().reserve(high_.size());
        }
        const auto idx = high_.index();

        if (idx == 0) {
            thermo_.forward(0.0);
            line_.forward(0.0);
            return;
        }

        const double th = std::max(
            std::abs(high_.data()[idx] - high_.data()[idx - 1]),
            std::abs(low_.data()[idx] - low_.data()[idx - 1]));
        thermo_.forward(th);

        if (!initialized_) {
            ema_ = th;
            initialized_ = true;
        } else {
            ema_ = (th - ema_) * multiplier_ + ema_;
        }
        line_.forward(ema_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] const Line<double>& thermo() const noexcept { return thermo_; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    std::size_t period_;
    double multiplier_;
    double ema_ = 0.0;
    bool initialized_ = false;
    Line<double> thermo_;
};

using THERMO = Thermometer;
using ElderThermometer = Thermometer;

} // namespace stratforge
