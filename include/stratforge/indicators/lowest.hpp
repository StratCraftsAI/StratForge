#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Lowest value over the trailing period, inclusive of the current bar.
class Lowest : public Indicator<Lowest> {
public:
    explicit Lowest(const Line<double>& source, std::size_t period)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double lowest = source_.data()[idx];
        for (std::size_t i = 1; i < period_; ++i) {
            const double candidate = source_.data()[idx - i];
            if (candidate < lowest) {
                lowest = candidate;
            }
        }

        line_.forward(lowest);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using MinN = Lowest;

} // namespace stratforge
