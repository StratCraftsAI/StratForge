#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace stratforge {

/// Even Better SineWave (EBSW) — Ehlers' cycle/trend classifier.
/// Outputs oscillator in [-1, 1]: >0 suggests uptrend/cycle, <0 downtrend.
class EBSW : public Indicator<EBSW> {
public:
    explicit EBSW(const Line<double>& source, std::size_t period = 10uz)
        : source_(source), period_(period < 2 ? 2 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double nd = static_cast<double>(period_);
        const double* p = &source_.data()[idx - period_ + 1];

        double sum_sin2 = 0.0;
        double sum_cos2 = 0.0;
        double sum_sincos = 0.0;
        double sum_y_sin = 0.0;
        double sum_y_cos = 0.0;

        for (std::size_t i = 0; i < period_; ++i) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) / nd;
            const double s = std::sin(angle);
            const double c = std::cos(angle);
            sum_sin2 += s * s;
            sum_cos2 += c * c;
            sum_sincos += s * c;
            sum_y_sin += p[i] * s;
            sum_y_cos += p[i] * c;
        }

        const double denom = sum_sin2 * sum_cos2 - sum_sincos * sum_sincos;
        if (std::abs(denom) < 1e-15) {
            line_.forward(0.0);
            return;
        }

        const double a = (sum_y_sin * sum_cos2 - sum_y_cos * sum_sincos) / denom;
        const double b = (sum_y_cos * sum_sin2 - sum_y_sin * sum_sincos) / denom;
        const double power = a * a + b * b;

        double total_power = 0.0;
        const double mean = simd::reduce_sum(p, period_) / nd;
        for (std::size_t i = 0; i < period_; ++i) {
            const double d = p[i] - mean;
            total_power += d * d;
        }
        total_power /= nd;

        if (total_power == 0.0) {
            line_.forward(0.0);
            return;
        }

        double wave = std::sqrt(power / total_power);
        if (wave > 1.0) wave = 1.0;
        if (a < 0.0) wave = -wave;
        line_.forward(wave);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using EvenBetterSineWave = EBSW;

} // namespace stratforge
