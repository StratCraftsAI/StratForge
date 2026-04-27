#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Rate of Change Percentage: ((price - prev) / prev) * 100.
class ROCP : public Indicator<ROCP> {
public:
    explicit ROCP(const Line<double>& source, std::size_t period = 12uz)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double previous = source_.data()[idx - period_];
        if (previous == 0.0) {
            line_.forward(0.0);
            return;
        }

        line_.forward(((source_.data()[idx] - previous) / previous) * 100.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using RateOfChangePercentage = ROCP;

/// Rate of Change Ratio: price / prev.
class ROCR : public Indicator<ROCR> {
public:
    explicit ROCR(const Line<double>& source, std::size_t period = 12uz)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double previous = source_.data()[idx - period_];
        if (previous == 0.0) {
            line_.forward(0.0);
            return;
        }

        line_.forward(source_.data()[idx] / previous);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using RateOfChangeRatio = ROCR;

/// Rate of Change Ratio * 100: (price / prev) * 100.
class ROCR100 : public Indicator<ROCR100> {
public:
    explicit ROCR100(const Line<double>& source, std::size_t period = 12uz)
        : source_(source), rocr_(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        rocr_.next();
        const double value = rocr_.line().data().back();
        line_.forward(std::isnan(value) ? value : (value * 100.0));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return rocr_.minimum_period();
    }

private:
    const Line<double>& source_;
    ROCR rocr_;
};

using RateOfChangeRatio100 = ROCR100;

} // namespace stratforge
