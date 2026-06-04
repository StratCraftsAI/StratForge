#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Q-Stick: SMA of (close - open) over trailing window.
class QStick : public Indicator<QStick> {
public:
    QStick(const Line<double>& open, const Line<double>& close, std::size_t period = 10uz)
        : open_(open), close_(close), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(close_.size());
            diff_.data().reserve(close_.size());
        }
        const auto idx = close_.index();
        diff_.forward(close_.data()[idx] - open_.data()[idx]);

        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(simd::reduce_sum(&diff_.data()[idx - period_ + 1], period_) /
                      static_cast<double>(period_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& open_;
    const Line<double>& close_;
    std::size_t period_;
    Line<double> diff_;
};

using QSTICK = QStick;

} // namespace stratforge
