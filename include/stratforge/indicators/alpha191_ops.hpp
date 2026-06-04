#pragma once

// Alpha191 shared operator primitives (Guotai Junan 2017).
// Extends alpha101_ops with China-market-specific operators.
// Cross-sectional approximations match Alpha101 strategy:
//   rank(expr)            -> ts_rank(expr, 252)
//   IndNeutralize(expr,g) -> passthrough

#include <stratforge/indicators/alpha101_ops.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>

namespace stratforge::alpha191 {

namespace a101 = stratforge::alpha101;

inline constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

using a101::delta;
using a101::delay;
using a101::sign;
using a101::signed_power;
using a101::sma;
using a101::stddev;
using a101::sum;
using a101::ts_min;
using a101::ts_max;
using a101::ts_argmax;
using a101::ts_argmin;
using a101::ts_rank;
using a101::correlation;
using a101::decay_linear;
using a101::scale;
using a101::covariance;
using a101::product;
using a101::adv;
using a101::vwap;
using a101::returns;

inline double mean(const Line<double>& x, std::size_t idx, std::size_t period) {
    return a101::sma(x, idx, period);
}

inline double wma(const Line<double>& x, std::size_t idx, std::size_t period) {
    return a101::decay_linear(x, idx, period);
}

inline double highday(const Line<double>& x, std::size_t idx, std::size_t d) {
    if (idx + 1 < d) [[unlikely]] return NaN;
    std::size_t best = 0;
    double best_val = x.data()[idx - d + 1];
    for (std::size_t i = 1; i < d; ++i) {
        const double v = x.data()[idx - d + 1 + i];
        if (v >= best_val) { best_val = v; best = i; }
    }
    return static_cast<double>(d - 1 - best);
}

inline double lowday(const Line<double>& x, std::size_t idx, std::size_t d) {
    if (idx + 1 < d) [[unlikely]] return NaN;
    std::size_t best = 0;
    double best_val = x.data()[idx - d + 1];
    for (std::size_t i = 1; i < d; ++i) {
        const double v = x.data()[idx - d + 1 + i];
        if (v <= best_val) { best_val = v; best = i; }
    }
    return static_cast<double>(d - 1 - best);
}

struct RegResult {
    double alpha;
    double beta;
    double r_squared;
};

inline RegResult reg_compute(const Line<double>& y, const Line<double>& x,
                             std::size_t idx, std::size_t d) {
    if (idx + 1 < d) [[unlikely]] return {NaN, NaN, NaN};
    const std::size_t start = idx - d + 1;
    const double n = static_cast<double>(d);
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0;
    for (std::size_t i = 0; i < d; ++i) {
        const double xi = x.data()[start + i];
        const double yi = y.data()[start + i];
        sx += xi;
        sy += yi;
        sxx += xi * xi;
        sxy += xi * yi;
        syy += yi * yi;
    }
    const double denom = n * sxx - sx * sx;
    if (denom == 0.0) return {NaN, NaN, NaN};
    const double beta = (n * sxy - sx * sy) / denom;
    const double alpha = (sy - beta * sx) / n;
    const double ss_tot = syy - sy * sy / n;
    if (ss_tot == 0.0) return {alpha, beta, NaN};
    double ss_res = 0.0;
    for (std::size_t i = 0; i < d; ++i) {
        const double ei = y.data()[start + i] - alpha - beta * x.data()[start + i];
        ss_res += ei * ei;
    }
    const double r2 = 1.0 - ss_res / ss_tot;
    return {alpha, beta, r2};
}

inline double regbeta(const Line<double>& y, const Line<double>& x,
                      std::size_t idx, std::size_t d) {
    return reg_compute(y, x, idx, d).beta;
}

inline double regresi(const Line<double>& y, const Line<double>& x,
                      std::size_t idx, std::size_t d) {
    const auto r = reg_compute(y, x, idx, d);
    if (std::isnan(r.alpha)) [[unlikely]] return NaN;
    return y.data()[idx] - (r.alpha + r.beta * x.data()[idx]);
}

inline double regrsq(const Line<double>& y, const Line<double>& x,
                     std::size_t idx, std::size_t d) {
    return reg_compute(y, x, idx, d).r_squared;
}

inline double count(const Line<double>& cond, std::size_t idx, std::size_t d) {
    if (idx + 1 < d) [[unlikely]] return NaN;
    double cnt = 0.0;
    for (std::size_t i = 0; i < d; ++i)
        cnt += (cond.data()[idx - d + 1 + i] != 0.0) ? 1.0 : 0.0;
    return cnt;
}

inline double sumif(const Line<double>& x, const Line<double>& cond,
                    std::size_t idx, std::size_t d) {
    if (idx + 1 < d) [[unlikely]] return NaN;
    double s = 0.0;
    for (std::size_t i = 0; i < d; ++i) {
        if (cond.data()[idx - d + 1 + i] != 0.0)
            s += x.data()[idx - d + 1 + i];
    }
    return s;
}

inline double filter(double x, double condition) {
    return (condition != 0.0) ? x : NaN;
}

inline double log(double x) {
    return std::log(x);
}

inline double abs(double x) {
    return std::abs(x);
}

inline double max(double a, double b) {
    return std::max(a, b);
}

inline double min(double a, double b) {
    return std::min(a, b);
}

inline double IF(double cond, double true_val, double false_val) {
    return (cond != 0.0) ? true_val : false_val;
}

} // namespace stratforge::alpha191
