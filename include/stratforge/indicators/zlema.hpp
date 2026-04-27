#pragma once

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Zero-lag exponential moving average.
class ZeroLagExponentialMovingAverage : public Indicator<ZeroLagExponentialMovingAverage> {
public:
    explicit ZeroLagExponentialMovingAverage(const Line<double>& source, std::size_t period = 30uz)
        : source_(source)
        , period_(period)
        , lag_((period > 0 ? (period - 1) : 0) / 2)
        , adjusted_()
        , ema_(adjusted_, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            adjusted_.data().reserve(n);
        }
        const auto idx = source_.index();

        if (idx < lag_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        adjusted_.forward((2.0 * source_.data()[idx]) - source_.data()[idx - lag_]);
        ema_.next();
        line_.forward(ema_.line().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + lag_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::size_t lag_;
    Line<double> adjusted_;
    EMA ema_;
};

using ZLEMA = ZeroLagExponentialMovingAverage;
using ZeroLagEma = ZeroLagExponentialMovingAverage;

} // namespace stratforge
