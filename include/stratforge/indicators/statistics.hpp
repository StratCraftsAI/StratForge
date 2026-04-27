#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Pearson correlation over a trailing window.
class Correlation : public Indicator<Correlation> {
public:
    Correlation(const Line<double>& data0, const Line<double>& data1, std::size_t period = 20uz)
        : data0_(data0), data1_(data1), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(data0_.size()); }
        const auto idx = data0_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double* px = &data0_.data()[idx - period_ + 1];
        const double* py = &data1_.data()[idx - period_ + 1];

        const double mean_x = simd::reduce_sum(px, period_) / static_cast<double>(period_);
        const double mean_y = simd::reduce_sum(py, period_) / static_cast<double>(period_);

        double covariance = 0.0;
        double var_x = 0.0;
        double var_y = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double dx = px[i] - mean_x;
            const double dy = py[i] - mean_y;
            covariance += dx * dy;
            var_x += dx * dx;
            var_y += dy * dy;
        }

        const double denominator = std::sqrt(var_x * var_y);
        if (denominator == 0.0) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(covariance / denominator);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& data0_;
    const Line<double>& data1_;
    std::size_t period_;
};

using Correl = Correlation;

/// Coefficient of determination from the trailing Pearson correlation.
class RSquared : public Indicator<RSquared> {
public:
    RSquared(const Line<double>& data0, const Line<double>& data1, std::size_t period = 20uz)
        : data0_(data0), correlation_(data0, data1, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(data0_.size()); }
        correlation_.next();
        const double corr = correlation_.line().data().back();
        if (std::isnan(corr)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward(corr * corr);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return correlation_.minimum_period();
    }

private:
    const Line<double>& data0_;
    Correlation correlation_;
};

} // namespace stratforge
