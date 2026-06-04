#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Log return: ln(close / close[period])
class LogReturn : public Indicator<LogReturn> {
public:
    explicit LogReturn(const Line<double>& source, std::size_t period = 1uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double prev = source_.data()[idx - period_];
        if (prev <= 0.0) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }
        line_.forward(std::log(source_.data()[idx] / prev));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using LOG_RETURN = LogReturn;

} // namespace stratforge
