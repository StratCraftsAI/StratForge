#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Chande Kroll Stop: trailing stop based on ATR.
/// Outputs stop_long (line_) and stop_short.
class ChandeKrollStop : public Indicator<ChandeKrollStop> {
public:
    ChandeKrollStop(const Line<double>& high,
                    const Line<double>& low,
                    const Line<double>& close,
                    std::size_t period = 10uz,
                    std::size_t q = 9uz,
                    double mult = 1.0)
        : high_(high), low_(low), close_(close)
        , period_(period == 0 ? 1 : period)
        , q_(q == 0 ? 1 : q)
        , mult_(mult) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = close_.size();
            reserve_output(n);
            stop_short_.data().reserve(n);
        }
        const auto idx = close_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const std::size_t warmup = period_ + q_;

        if (idx < warmup) {
            line_.forward(nan);
            stop_short_.forward(nan);
            return;
        }

        double highest_stop = -std::numeric_limits<double>::infinity();
        double lowest_stop = std::numeric_limits<double>::infinity();

        for (std::size_t j = 0; j < q_; ++j) {
            const std::size_t base = idx - j;

            double atr_sum = 0.0;
            for (std::size_t i = 0; i < period_; ++i) {
                const std::size_t pos = base - period_ + 1 + i;
                double tr = high_.data()[pos] - low_.data()[pos];
                if (pos > 0) {
                    const double pc = close_.data()[pos - 1];
                    tr = std::max(tr, std::abs(high_.data()[pos] - pc));
                    tr = std::max(tr, std::abs(low_.data()[pos] - pc));
                }
                atr_sum += tr;
            }
            const double atr = atr_sum / static_cast<double>(period_);

            double hh = high_.data()[base - period_ + 1];
            double ll = low_.data()[base - period_ + 1];
            for (std::size_t i = 1; i < period_; ++i) {
                hh = std::max(hh, high_.data()[base - period_ + 1 + i]);
                ll = std::min(ll, low_.data()[base - period_ + 1 + i]);
            }

            highest_stop = std::max(highest_stop, hh - mult_ * atr);
            lowest_stop = std::min(lowest_stop, ll + mult_ * atr);
        }

        line_.forward(highest_stop);
        stop_short_.forward(lowest_stop);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + q_ + 1;
    }

    [[nodiscard]] const Line<double>& stop_long() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& stop_short() const noexcept { return stop_short_; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t period_;
    std::size_t q_;
    double mult_;
    Line<double> stop_short_;
};

using CKSP = ChandeKrollStop;

} // namespace stratforge
