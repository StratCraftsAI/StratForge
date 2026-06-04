#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Jurik Moving Average — adaptive smooth filter.
/// Simplified JMA approximation using adaptive EMA with volatility scaling.
class JMA : public Indicator<JMA> {
public:
    explicit JMA(const Line<double>& source, std::size_t period = 7uz,
                 double phase = 0.0, double power = 2.0)
        : source_(source)
        , period_(period == 0 ? 1 : period)
        , phase_(phase)
        , power_(power) {
        const double beta = 0.45 * (static_cast<double>(period_) - 1.0) /
                            (0.45 * (static_cast<double>(period_) - 1.0) + 2.0);
        beta_ = beta;
        const double phase_ratio = (phase_ < -100.0) ? 0.5 :
                                   (phase_ > 100.0)  ? 2.5 :
                                   (phase_ / 100.0 + 1.5);
        phase_ratio_ = phase_ratio;
    }

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        const double val = source_.data()[idx];

        if (idx == 0) {
            e0_ = val;
            e1_ = 0.0;
            e2_ = 0.0;
            jma_ = val;
            line_.forward(val);
            return;
        }

        e0_ = (1.0 - beta_) * val + beta_ * e0_;
        e1_ = (val - e0_) * (1.0 - beta_) + beta_ * e1_;
        e2_ = (e0_ + phase_ratio_ * e1_ - jma_) * std::pow(1.0 - beta_, power_) + std::pow(beta_, power_) * e2_;
        jma_ = jma_ + e2_;
        line_.forward(jma_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    double phase_;
    double power_;
    double beta_;
    double phase_ratio_;
    double e0_ = 0.0;
    double e1_ = 0.0;
    double e2_ = 0.0;
    double jma_ = 0.0;
};

using JurikMA = JMA;

} // namespace stratforge
