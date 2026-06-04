#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Increasing: 1.0 if source has been strictly increasing for `period` bars, else 0.0.
class Increasing : public Indicator<Increasing> {
public:
    explicit Increasing(const Line<double>& source, std::size_t period = 5uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        bool all_increasing = true;
        for (std::size_t i = 0; i < period_; ++i) {
            if (source_.data()[idx - i] <= source_.data()[idx - i - 1]) {
                all_increasing = false;
                break;
            }
        }
        line_.forward(all_increasing ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using INCREASING = Increasing;

} // namespace stratforge
