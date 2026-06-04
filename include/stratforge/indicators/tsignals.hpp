#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Trend Signals: EMA-based trend direction and signal generator.
/// line() = trend direction (+1/-1), signal line = entry triggers.
class TSignals : public Indicator<TSignals> {
public:
    explicit TSignals(const Line<double>& source, std::size_t period = 13uz)
        : source_(source)
        , period_(period == 0 ? 1 : period)
        , mult_(2.0 / (static_cast<double>(period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(source_.size());
            signal_.data().reserve(source_.size());
        }
        const auto idx = source_.index();

        if (idx == 0) {
            ema_ = source_.data()[idx];
            prev_trend_ = 0.0;
            line_.forward(0.0);
            signal_.forward(0.0);
            return;
        }

        ema_ = (source_.data()[idx] - ema_) * mult_ + ema_;
        const double trend = source_.data()[idx] > ema_ ? 1.0 : -1.0;

        double sig = 0.0;
        if (trend != prev_trend_) {
            sig = trend;
        }

        line_.forward(trend);
        signal_.forward(sig);
        prev_trend_ = trend;
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] const Line<double>& trend() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& signal() const noexcept { return signal_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double mult_;
    double ema_ = 0.0;
    double prev_trend_ = 0.0;
    Line<double> signal_;
};

using TSIGNALS = TSignals;
using TrendSignals = TSignals;

} // namespace stratforge
