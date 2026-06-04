#pragma once

// JKP shared operator primitives (Jensen, Kelly & Pedersen 2023).
//
// Academic factor library — extends alpha101_ops with operators needed for
// classic anomaly factors (Jegadeesh-Titman momentum, Fama-French risk,
// Bali lottery, Harvey-Siddique coskewness, etc.).
//
// Single-symbol approximations:
//   market_return → SMA(return, 20) as a smoothed single-asset proxy
//   cross-sectional rank → ts_rank (same as Alpha101/191)
//   Factors requiring calendar data (seasonality) use bar-index proxies

#include <stratforge/indicators/alpha101_ops.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>

namespace stratforge::jkp {

namespace a101 = stratforge::alpha101;

inline constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

using a101::sma;
using a101::stddev;
using a101::sum;
using a101::ts_max;
using a101::ts_min;
using a101::ts_rank;
using a101::returns;
using a101::delta;
using a101::delay;

// return(close, N) = (close[t] - close[t-N]) / close[t-N]
inline double ret(const Line<double>& close, std::size_t idx, std::size_t period) {
    if (idx < period) return NaN;
    const double prev = close.data()[idx - period];
    if (prev == 0.0) return NaN;
    return (close.data()[idx] - prev) / prev;
}

// Rolling skewness (population, Fisher-adjusted)
inline double skewness(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period || period < 3) return NaN;
    const std::size_t start = idx - period + 1;
    const double n = static_cast<double>(period);
    double s = 0.0;
    for (std::size_t i = 0; i < period; ++i)
        s += x.data()[start + i];
    const double mean = s / n;
    double m2 = 0.0, m3 = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double d = x.data()[start + i] - mean;
        m2 += d * d;
        m3 += d * d * d;
    }
    m2 /= n;
    m3 /= n;
    if (m2 == 0.0) return NaN;
    return m3 / (m2 * std::sqrt(m2));
}

// Rolling OLS beta of y vs x over trailing window (population covariance / variance)
inline double beta(const Line<double>& y, const Line<double>& x,
                   std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const std::size_t start = idx - period + 1;
    const double n = static_cast<double>(period);
    double sx = 0.0, sy = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        sx += x.data()[start + i];
        sy += y.data()[start + i];
    }
    const double mx = sx / n;
    const double my = sy / n;
    double cov = 0.0, var_x = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double dx = x.data()[start + i] - mx;
        cov += dx * (y.data()[start + i] - my);
        var_x += dx * dx;
    }
    if (var_x == 0.0) return NaN;
    return cov / var_x;
}

// Rolling OLS alpha (intercept) of y vs x
inline double alpha_ols(const Line<double>& y, const Line<double>& x,
                        std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const std::size_t start = idx - period + 1;
    const double n = static_cast<double>(period);
    double sx = 0.0, sy = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        sx += x.data()[start + i];
        sy += y.data()[start + i];
    }
    const double mx = sx / n;
    const double my = sy / n;
    const double b = beta(y, x, idx, period);
    if (std::isnan(b)) return NaN;
    return my - b * mx;
}

// Std of OLS residuals (idiosyncratic volatility)
inline double residual_std(const Line<double>& y, const Line<double>& x,
                           std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const double b = beta(y, x, idx, period);
    const double a = alpha_ols(y, x, idx, period);
    if (std::isnan(b)) return NaN;
    const std::size_t start = idx - period + 1;
    double ss = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double e = y.data()[start + i] - a - b * x.data()[start + i];
        ss += e * e;
    }
    return std::sqrt(ss / static_cast<double>(period));
}

// R-squared from OLS of y vs x
inline double r_squared(const Line<double>& y, const Line<double>& x,
                        std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const std::size_t start = idx - period + 1;
    const double n = static_cast<double>(period);
    double sy = 0.0;
    for (std::size_t i = 0; i < period; ++i)
        sy += y.data()[start + i];
    const double my = sy / n;
    double ss_tot = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double d = y.data()[start + i] - my;
        ss_tot += d * d;
    }
    if (ss_tot == 0.0) return NaN;
    const double b = beta(y, x, idx, period);
    const double a = alpha_ols(y, x, idx, period);
    if (std::isnan(b)) return NaN;
    double ss_res = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double e = y.data()[start + i] - a - b * x.data()[start + i];
        ss_res += e * e;
    }
    return 1.0 - ss_res / ss_tot;
}

// Conditional beta: only include bars where condition line is nonzero
inline double beta_cond(const Line<double>& y, const Line<double>& x,
                        const Line<double>& cond,
                        std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const std::size_t start = idx - period + 1;
    double sx = 0.0, sy = 0.0, cnt = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        if (cond.data()[start + i] != 0.0) {
            sx += x.data()[start + i];
            sy += y.data()[start + i];
            cnt += 1.0;
        }
    }
    if (cnt < 3.0) return NaN;
    const double mx = sx / cnt;
    const double my = sy / cnt;
    double cov = 0.0, var_x = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        if (cond.data()[start + i] != 0.0) {
            const double dx = x.data()[start + i] - mx;
            cov += dx * (y.data()[start + i] - my);
            var_x += dx * dx;
        }
    }
    if (var_x == 0.0) return NaN;
    return cov / var_x;
}

// Coskewness: E[(r - mu_r) * (m - mu_m)^2] / (sigma_r * sigma_m^2)
inline double coskewness(const Line<double>& r, const Line<double>& m,
                         std::size_t idx, std::size_t period) {
    if (idx + 1 < period || period < 3) return NaN;
    const std::size_t start = idx - period + 1;
    const double n = static_cast<double>(period);
    double sr = 0.0, sm = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        sr += r.data()[start + i];
        sm += m.data()[start + i];
    }
    const double mr = sr / n;
    const double mm = sm / n;
    double var_r = 0.0, var_m = 0.0, csk = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double dr = r.data()[start + i] - mr;
        const double dm = m.data()[start + i] - mm;
        var_r += dr * dr;
        var_m += dm * dm;
        csk += dr * dm * dm;
    }
    var_r /= n;
    var_m /= n;
    csk /= n;
    const double denom = std::sqrt(var_r) * var_m;
    if (denom == 0.0) return NaN;
    return csk / denom;
}

} // namespace stratforge::jkp
