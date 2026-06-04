#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace stratforge {

/// SumD: Sum(positive returns) - Sum(negative returns) over the trailing window.
/// Alpha158 formula: Sum(Max($close-Ref($close,1),0),N) - Sum(Min($close-Ref($close,1),0),N)
class SumD : public Indicator<SumD> {
public:
    explicit SumD(const Line<double>& source, std::size_t period)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* p = source_.data().data();
        double sum_pos = 0.0;
        double sum_neg = 0.0;
        const std::size_t start = idx - period_ + 1;
        for (std::size_t i = start; i <= idx; ++i) {
            const double diff = p[i] - p[i - 1];
            if (diff > 0.0) sum_pos += diff;
            else if (diff < 0.0) sum_neg += diff;
        }
        line_.forward(sum_pos - sum_neg);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

} // namespace stratforge
