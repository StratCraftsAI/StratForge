#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Psychological Line: percentage of up-bars over trailing period.
class PSL : public Indicator<PSL> {
public:
    explicit PSL(const Line<double>& source, std::size_t period = 12uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        std::size_t up_count = 0;
        for (std::size_t i = 0; i < period_; ++i) {
            if (source_.data()[idx - i] > source_.data()[idx - i - 1])
                ++up_count;
        }

        line_.forward(100.0 * static_cast<double>(up_count) / static_cast<double>(period_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using PsychologicalLine = PSL;

} // namespace stratforge
