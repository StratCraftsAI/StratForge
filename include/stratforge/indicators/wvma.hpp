#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// WVMA: rolling standard deviation of |return| * volume.
/// Alpha158 formula: Std(Abs($close/Ref($close,1)-1)*$volume, N)
class WVMA : public Indicator<WVMA> {
public:
    explicit WVMA(const Line<double>& close, const Line<double>& volume, std::size_t period)
        : close_(close), volume_(volume), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* c = close_.data().data();
        const double* v = volume_.data().data();
        const auto n = static_cast<double>(period_);
        const std::size_t start = idx - period_ + 1;

        double sum = 0.0;
        double sum2 = 0.0;
        for (std::size_t i = start; i <= idx; ++i) {
            const double val = std::abs(c[i] / c[i - 1] - 1.0) * v[i];
            sum += val;
            sum2 += val * val;
        }

        const double mean = sum / n;
        const double var = sum2 / n - mean * mean;
        line_.forward(std::sqrt(std::max(var, 0.0)));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_ + 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    std::size_t period_;
};

} // namespace stratforge
