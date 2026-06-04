#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Elder Force Index: EMA(period, (close - close_prev) * volume)
class EFI : public Indicator<EFI> {
public:
    EFI(const Line<double>& close, const Line<double>& volume, std::size_t period = 13uz)
        : close_(close), volume_(volume)
        , period_(period == 0 ? 1 : period)
        , multiplier_(2.0 / (static_cast<double>(period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();

        if (idx == 0) {
            line_.forward(0.0);
            return;
        }

        const double force = (close_.data()[idx] - close_.data()[idx - 1]) * volume_.data()[idx];

        if (!initialized_) [[unlikely]] {
            ema_ = force;
            initialized_ = true;
        } else {
            ema_ = (force - ema_) * multiplier_ + ema_;
        }

        line_.forward(ema_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    std::size_t period_;
    double multiplier_;
    double ema_ = 0.0;
    bool initialized_ = false;
};

using ElderForceIndex = EFI;

} // namespace stratforge
