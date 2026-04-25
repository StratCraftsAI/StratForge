#pragma once

#include <stratforge/indicators/hma.hpp>
#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/zerolag.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Dickson moving average.
class DicksonMovingAverage : public Indicator<DicksonMovingAverage> {
public:
    explicit DicksonMovingAverage(const Line<double>& source,
                                  std::size_t period = 20,
                                  int gainlimit = 50,
                                  std::size_t hperiod = 7)
        : source_(source), ec_(source, period, gainlimit), hma_(source, hperiod) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        ec_.next();
        hma_.next();

        const double ec = ec_.line().data().back();
        const double hma = hma_.line().data().back();
        if (std::isnan(ec) || std::isnan(hma)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward((ec + hma) / 2.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return std::max(ec_.minimum_period(), hma_.minimum_period());
    }

private:
    const Line<double>& source_;
    ZeroLagIndicator ec_;
    HMA hma_;
};

using DMA = DicksonMovingAverage;
using DicksonMA = DicksonMovingAverage;

} // namespace stratforge
