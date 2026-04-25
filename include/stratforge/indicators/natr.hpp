#pragma once

#include <stratforge/indicators/atr.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Normalized Average True Range: ATR / Close * 100.
class NATR : public Indicator<NATR> {
public:
    NATR(const Line<double>& high, const Line<double>& low,
         const Line<double>& close, std::size_t period = 14)
        : close_(close), atr_(high, low, close, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        atr_.next();

        const double atr_val = atr_.line().data().back();
        if (std::isnan(atr_val)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double close_val = close_.data()[close_.index()];
        if (close_val == 0.0) {
            line_.forward(0.0);
            return;
        }

        line_.forward((atr_val / close_val) * 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return atr_.minimum_period();
    }

private:
    const Line<double>& close_;
    ATR atr_;
};

using NormalizedATR = NATR;
using NormalizedAverageTrueRange = NATR;

} // namespace stratforge
