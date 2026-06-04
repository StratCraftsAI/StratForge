#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Rolling skewness (Fisher's definition) over a trailing window.
class Skew : public Indicator<Skew> {
public:
    explicit Skew(const Line<double>& source, std::size_t period = 30uz)
        : source_(source), period_(period < 3 ? 3 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* p = &source_.data()[idx - period_ + 1];
        const double n = static_cast<double>(period_);
        const double mean = simd::reduce_sum(p, period_) / n;

        double m2 = 0.0;
        double m3 = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double d = p[i] - mean;
            const double d2 = d * d;
            m2 += d2;
            m3 += d2 * d;
        }
        m2 /= n;
        m3 /= n;

        if (m2 == 0.0) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double stddev = std::sqrt(m2);
        line_.forward(m3 / (stddev * stddev * stddev));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using SKEW = Skew;
using RollingSkew = Skew;

} // namespace stratforge
