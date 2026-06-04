#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace stratforge {

/// Ehlers' Super Smoother Filter (2-pole Butterworth).
class SSF : public Indicator<SSF> {
public:
    explicit SSF(const Line<double>& source, std::size_t period = 10uz)
        : source_(source), period_(period < 2 ? 2 : period) {
        const double x = std::numbers::pi * std::numbers::sqrt2 / static_cast<double>(period_);
        const double a = std::exp(-x);
        coeff_b_ = 2.0 * a * std::cos(x);
        coeff_c_ = -(a * a);
        coeff_a_ = 1.0 - coeff_b_ - coeff_c_;
    }

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();

        if (idx == 0) {
            line_.forward(source_.data()[idx]);
            return;
        }
        if (idx == 1) {
            line_.forward(source_.data()[idx]);
            return;
        }

        const double prev1 = line_.data()[idx - 1];
        const double prev2 = line_.data()[idx - 2];
        const double val = coeff_a_ * (source_.data()[idx] + source_.data()[idx - 1]) / 2.0 +
                           coeff_b_ * prev1 + coeff_c_ * prev2;
        line_.forward(val);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double coeff_a_;
    double coeff_b_;
    double coeff_c_;
};

using SuperSmootherFilter = SSF;

} // namespace stratforge
