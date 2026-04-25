#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Positive downward close change over the configured lookback.
class DownDay : public Indicator<DownDay> {
public:
    explicit DownDay(const Line<double>& source, std::size_t period = 1)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(std::max(source_.data()[idx - period_] - source_.data()[idx], 0.0));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

/// Boolean down-day flag over the configured lookback.
class DownDayBool : public Indicator<DownDayBool> {
public:
    explicit DownDayBool(const Line<double>& source, std::size_t period = 1)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(source_.data()[idx - period_] > source_.data()[idx] ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

} // namespace stratforge
