#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/stddev.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

class OLS_Slope_InterceptN : public Indicator<OLS_Slope_InterceptN> {
public:
    OLS_Slope_InterceptN(const Line<double>& data0, const Line<double>& data1, std::size_t period = 10)
        : data0_(data0), data1_(data1), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = data0_.size();
            reserve_output(n);
            intercept_.data().reserve(n);
        }
        const std::size_t idx = data0_.index();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(nan);
            intercept_.forward(nan);
            return;
        }

        double sum_x = 0.0;
        double sum_y = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            sum_x += data1_.data()[idx - i];
            sum_y += data0_.data()[idx - i];
        }

        const double mean_x = sum_x / static_cast<double>(period_);
        const double mean_y = sum_y / static_cast<double>(period_);

        double sxx = 0.0;
        double sxy = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double x = data1_.data()[idx - i];
            const double y = data0_.data()[idx - i];
            const double dx = x - mean_x;
            sxx += dx * dx;
            sxy += dx * (y - mean_y);
        }

        const double slope = sxx == 0.0 ? 0.0 : (sxy / sxx);
        const double intercept = mean_y - (slope * mean_x);
        line_.forward(slope);
        intercept_.forward(intercept);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] const Line<double>& slope() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& intercept() const noexcept { return intercept_; }

private:
    const Line<double>& data0_;
    const Line<double>& data1_;
    std::size_t period_;
    Line<double> intercept_;
};

class OLS_TransformationN : public Indicator<OLS_TransformationN> {
public:
    OLS_TransformationN(const Line<double>& data0, const Line<double>& data1, std::size_t period = 10)
        : data0_(data0)
        , data1_(data1)
        , period_(period == 0 ? 1 : period)
        , regression_(data0, data1, period_)
        , spread_mean_sma_(spread_, period_)
        , spread_stddev_(spread_, period_) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = data0_.size();
            reserve_output(n);
            spread_.data().reserve(n);
            spread_mean_.data().reserve(n);
            spread_std_.data().reserve(n);
            zscore_.data().reserve(n);
        }
        const double nan = std::numeric_limits<double>::quiet_NaN();
        regression_.next();

        const double slope = regression_.slope().data().back();
        const double intercept = regression_.intercept().data().back();
        if (std::isnan(slope) || std::isnan(intercept)) {
            line_.forward(nan);
            spread_mean_.forward(nan);
            spread_std_.forward(nan);
            zscore_.forward(nan);
            spread_.forward(nan);
            return;
        }

        const std::size_t idx = data0_.index();
        const double spread_value = data0_.data()[idx] - ((slope * data1_.data()[idx]) + intercept);
        spread_.forward(spread_value);
        line_.forward(spread_value);

        spread_mean_sma_.next();
        spread_stddev_.next();

        const double mean = spread_mean_sma_.line().data().back();
        const double std = spread_stddev_.line().data().back();
        spread_mean_.forward(mean);
        spread_std_.forward(std);

        if (std::isnan(mean) || std::isnan(std) || std == 0.0) {
            zscore_.forward(nan);
            return;
        }

        zscore_.forward((spread_value - mean) / std);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return (period_ * 2) - 1;
    }

    [[nodiscard]] const Line<double>& spread() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& spread_mean() const noexcept { return spread_mean_; }
    [[nodiscard]] const Line<double>& spread_std() const noexcept { return spread_std_; }
    [[nodiscard]] const Line<double>& zscore() const noexcept { return zscore_; }

private:
    const Line<double>& data0_;
    const Line<double>& data1_;
    std::size_t period_;
    OLS_Slope_InterceptN regression_;
    Line<double> spread_;
    SMA spread_mean_sma_;
    StdDev spread_stddev_;
    Line<double> spread_mean_;
    Line<double> spread_std_;
    Line<double> zscore_;
};

class OLS_BetaN : public Indicator<OLS_BetaN> {
public:
    OLS_BetaN(const Line<double>& data0, const Line<double>& data1, std::size_t period = 10)
        : data0_(data0), regression_(data0, data1, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(data0_.size()); }
        regression_.next();
        line_.forward(regression_.slope().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return regression_.minimum_period();
    }

private:
    const Line<double>& data0_;
    OLS_Slope_InterceptN regression_;
};

} // namespace stratforge
