#pragma once

#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/sma.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Mark Johnson Pretty Good Oscillator.
class PrettyGoodOscillator : public Indicator<PrettyGoodOscillator> {
public:
    explicit PrettyGoodOscillator(const Line<double>& high,
                                  const Line<double>& low,
                                  const Line<double>& close,
                                  std::size_t period = 14uz)
        : close_(close), average_(close, period), atr_(high, low, close, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        average_.next();
        atr_.next();

        const double average = average_.line().data().back();
        const double atr = atr_.line().data().back();
        if (std::isnan(average) || std::isnan(atr)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward((close_.data()[close_.index()] - average) / atr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return std::max(average_.minimum_period(), atr_.minimum_period());
    }

private:
    const Line<double>& close_;
    SMA average_;
    ATR atr_;
};

using PGO = PrettyGoodOscillator;
using PrettyGoodOsc = PrettyGoodOscillator;

} // namespace stratforge
