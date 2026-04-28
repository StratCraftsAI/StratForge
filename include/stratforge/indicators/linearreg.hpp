#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace stratforge {

/// Linear Regression value at the current bar (time series vs price).
/// Fits y = slope * x + intercept to the last `period` bars, outputs the
/// fitted value at the most recent bar.
class LinearReg : public Indicator<LinearReg> {
public:
    explicit LinearReg(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            slope_.data().reserve(n);
            intercept_.data().reserve(n);
        }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            slope_.forward(std::numeric_limits<double>::quiet_NaN());
            intercept_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        // x = 0, 1, ..., period-1 (time indices)
        const double n = static_cast<double>(period_);
        const double sum_x = n * (n - 1.0) / 2.0;
        const double sum_x2 = n * (n - 1.0) * (2.0 * n - 1.0) / 6.0;

        double sum_y = 0.0;
        double sum_xy = 0.0;
        const std::size_t start = idx - period_ + 1;

        for (std::size_t i = 0; i < period_; ++i) {
            const double y = source_.data()[start + i];
            const double x = static_cast<double>(i);
            sum_y += y;
            sum_xy += x * y;
        }

        const double denom = n * sum_x2 - sum_x * sum_x;
        const double s = (denom == 0.0) ? 0.0 : (n * sum_xy - sum_x * sum_y) / denom;
        const double b = (sum_y - s * sum_x) / n;

        slope_.forward(s);
        intercept_.forward(b);
        // Fitted value at x = period - 1 (most recent bar)
        line_.forward(s * (n - 1.0) + b);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] const Line<double>& slope() const noexcept { return slope_; }
    [[nodiscard]] const Line<double>& intercept() const noexcept { return intercept_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    Line<double> slope_;
    Line<double> intercept_;
};

using LINEARREG = LinearReg;
using LinearRegression = LinearReg;

/// Linear Regression Slope.
class LinearRegSlope : public Indicator<LinearRegSlope> {
public:
    explicit LinearRegSlope(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), lr_(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        lr_.next();
        line_.forward(lr_.slope().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return lr_.minimum_period();
    }

private:
    const Line<double>& source_;
    LinearReg lr_;
};

using LINEARREG_SLOPE = LinearRegSlope;

/// Linear Regression Intercept.
class LinearRegIntercept : public Indicator<LinearRegIntercept> {
public:
    explicit LinearRegIntercept(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), lr_(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        lr_.next();
        line_.forward(lr_.intercept().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return lr_.minimum_period();
    }

private:
    const Line<double>& source_;
    LinearReg lr_;
};

using LINEARREG_INTERCEPT = LinearRegIntercept;

/// Linear Regression Angle: atan(slope) in degrees.
class LinearRegAngle : public Indicator<LinearRegAngle> {
public:
    explicit LinearRegAngle(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), lr_(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        lr_.next();
        const double s = lr_.slope().data().back();
        if (std::isnan(s)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }
        line_.forward(std::atan(s) * (180.0 / std::numbers::pi));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return lr_.minimum_period();
    }

private:
    const Line<double>& source_;
    LinearReg lr_;
};

using LINEARREG_ANGLE = LinearRegAngle;

/// Time Series Forecast: linear regression extrapolated one bar forward.
/// TSF = slope * period + intercept  (value at x = period, i.e. one bar ahead)
class TSF : public Indicator<TSF> {
public:
    explicit TSF(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), lr_(source, period), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        lr_.next();
        const double s = lr_.slope().data().back();
        const double b = lr_.intercept().data().back();
        if (std::isnan(s) || std::isnan(b)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }
        // Extrapolate to x = period (one step beyond the window)
        line_.forward(s * static_cast<double>(period_) + b);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return lr_.minimum_period();
    }

private:
    const Line<double>& source_;
    LinearReg lr_;
    std::size_t period_;
};

using TimeSeriesForecast = TSF;

} // namespace stratforge
