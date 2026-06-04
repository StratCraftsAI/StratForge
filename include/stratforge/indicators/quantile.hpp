#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace stratforge {

/// Rolling Quantile over a trailing window (default q=0.5 = median).
class Quantile : public Indicator<Quantile> {
public:
    explicit Quantile(const Line<double>& source, std::size_t period = 30uz, double q = 0.5)
        : source_(source), period_(period == 0 ? 1 : period), q_(q), buf_(period_) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = 0; i < period_; ++i)
            buf_[i] = source_.data()[start + i];

        std::sort(buf_.begin(), buf_.end());

        const double pos = q_ * static_cast<double>(period_ - 1);
        const auto lower = static_cast<std::size_t>(pos);
        const auto upper = lower + 1 < period_ ? lower + 1 : lower;
        const double frac = pos - static_cast<double>(lower);
        line_.forward(buf_[lower] * (1.0 - frac) + buf_[upper] * frac);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }
    [[nodiscard]] double q() const noexcept { return q_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double q_;
    std::vector<double> buf_;
};

using QUANTILE = Quantile;
using RollingQuantile = Quantile;

} // namespace stratforge
