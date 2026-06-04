#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Inertia: linear regression of RVI over trailing window.
class Inertia : public Indicator<Inertia> {
public:
    explicit Inertia(const Line<double>& source, std::size_t period = 20uz,
                     std::size_t rvi_period = 14uz)
        : source_(source)
        , period_(period == 0 ? 1 : period)
        , rvi_period_(rvi_period == 0 ? 1 : rvi_period)
        , rvi_mult_(2.0 / (static_cast<double>(rvi_period_) + 1.0)) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            reserve_output(source_.size());
            rvi_.data().reserve(source_.size());
        }
        const auto idx = source_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();

        if (idx + 1 < 2) {
            rvi_.forward(nan);
            line_.forward(nan);
            return;
        }

        const auto [mean, variance] = simd::reduce_mean_variance(
            &source_.data()[(idx >= rvi_period_) ? (idx - rvi_period_ + 1) : 0],
            (idx >= rvi_period_) ? rvi_period_ : idx + 1);
        const double stddev = std::sqrt(variance);

        if (idx < 2) {
            prev_stddev_ = stddev;
            rvi_.forward(nan);
            line_.forward(nan);
            return;
        }

        const double change = stddev - prev_stddev_;
        prev_stddev_ = stddev;
        const double gain = change > 0.0 ? change : 0.0;
        const double loss = change < 0.0 ? -change : 0.0;

        if (!rvi_init_) {
            avg_gain_ = gain;
            avg_loss_ = loss;
            rvi_init_ = true;
        } else {
            avg_gain_ = (gain - avg_gain_) * rvi_mult_ + avg_gain_;
            avg_loss_ = (loss - avg_loss_) * rvi_mult_ + avg_loss_;
        }

        const double rvi_val = (avg_loss_ == 0.0) ? 100.0
                             : 100.0 - 100.0 / (1.0 + avg_gain_ / avg_loss_);
        rvi_.forward(rvi_val);

        if (rvi_.size() < period_) {
            line_.forward(nan);
            return;
        }

        const double n = static_cast<double>(period_);
        const double sum_x = n * (n - 1.0) / 2.0;
        const double sum_x2 = n * (n - 1.0) * (2.0 * n - 1.0) / 6.0;
        double sum_y = 0.0;
        double sum_xy = 0.0;
        const std::size_t start = rvi_.size() - period_;
        for (std::size_t i = 0; i < period_; ++i) {
            const double y = rvi_.data()[start + i];
            sum_y += y;
            sum_xy += static_cast<double>(i) * y;
        }
        const double denom = n * sum_x2 - sum_x * sum_x;
        const double slope = (denom == 0.0) ? 0.0 : (n * sum_xy - sum_x * sum_y) / denom;
        const double intercept = (sum_y - slope * sum_x) / n;
        line_.forward(slope * (n - 1.0) + intercept);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return rvi_period_ + period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
    std::size_t rvi_period_;
    double rvi_mult_;
    double avg_gain_ = 0.0;
    double avg_loss_ = 0.0;
    double prev_stddev_ = 0.0;
    bool rvi_init_ = false;
    Line<double> rvi_;
};

using INERTIA = Inertia;

} // namespace stratforge
