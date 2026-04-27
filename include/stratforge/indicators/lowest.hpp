#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Lowest value over the trailing period, inclusive of the current bar.
class Lowest : public Indicator<Lowest> {
public:
    explicit Lowest(const Line<double>& source, std::size_t period)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(simd::reduce_min(&source_.data()[idx - period_ + 1], period_));
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
