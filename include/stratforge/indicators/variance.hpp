#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Population variance over the trailing period.
class Variance : public Indicator<Variance> {
public:
    explicit Variance(const Line<double>& source, std::size_t period = 5uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const auto [mean, variance] = simd::reduce_mean_variance(
            &source_.data()[idx - period_ + 1], period_);
        line_.forward(variance);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using VAR = Variance;

} // namespace stratforge
