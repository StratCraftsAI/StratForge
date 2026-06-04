#pragma once

// Alpha101 shared operator primitives (Kakushadze 2015).
//
// All functions operate on Line<double> data at a given bar index.
// Cross-sectional approximations for single-symbol mode:
//   rank(expr)            → ts_rank(expr, 252)  (rolling percentile rank)
//   IndNeutralize(expr,g) → passthrough (returns expr unchanged)
//   scale(expr)           → expr / sum(abs(expr), len) (self-scaling)
//   cap                   → close * volume (dollar volume proxy)
//   vwap                  → (high + low + close) / 3 (typical price)

#include <stratforge/core/line.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>

namespace stratforge::alpha101 {

inline constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// delta(x, d) = x[t] - x[t-d]
inline double delta(const Line<double>& x, std::size_t idx, std::size_t d) {
    if (idx < d) return NaN;
    return x.data()[idx] - x.data()[idx - d];
}

// delay(x, d) = x[t-d]
inline double delay(const Line<double>& x, std::size_t idx, std::size_t d) {
    if (idx < d) return NaN;
    return x.data()[idx - d];
}

// sign(x)
inline double sign(double x) {
    if (x > 0.0) return 1.0;
    if (x < 0.0) return -1.0;
    return 0.0;
}

// SignedPower(x, e) = sign(x) * abs(x)^e
inline double signed_power(double x, double e) {
    return sign(x) * std::pow(std::abs(x), e);
}

// SMA over trailing window: sum(x[idx-period+1..idx]) / period
inline double sma(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    return simd::reduce_sum(&x.data()[idx - period + 1], period) /
           static_cast<double>(period);
}

// stddev over trailing window (population stddev)
inline double stddev(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const double mean = sma(x, idx, period);
    double var = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double d = x.data()[idx - period + 1 + i] - mean;
        var += d * d;
    }
    return std::sqrt(var / static_cast<double>(period));
}

// sum over trailing window
inline double sum(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    return simd::reduce_sum(&x.data()[idx - period + 1], period);
}

// ts_min: minimum over trailing window
inline double ts_min(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    double mn = x.data()[idx - period + 1];
    for (std::size_t i = 1; i < period; ++i)
        mn = std::min(mn, x.data()[idx - period + 1 + i]);
    return mn;
}

// ts_max: maximum over trailing window
inline double ts_max(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    double mx = x.data()[idx - period + 1];
    for (std::size_t i = 1; i < period; ++i)
        mx = std::max(mx, x.data()[idx - period + 1 + i]);
    return mx;
}

// ts_argmax: position of max in trailing window (0-based from window start)
inline double ts_argmax(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    std::size_t best = 0;
    double best_val = x.data()[idx - period + 1];
    for (std::size_t i = 1; i < period; ++i) {
        const double v = x.data()[idx - period + 1 + i];
        if (v >= best_val) { best_val = v; best = i; }
    }
    return static_cast<double>(best);
}

// ts_argmin: position of min in trailing window (0-based from window start)
inline double ts_argmin(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    std::size_t best = 0;
    double best_val = x.data()[idx - period + 1];
    for (std::size_t i = 1; i < period; ++i) {
        const double v = x.data()[idx - period + 1 + i];
        if (v <= best_val) { best_val = v; best = i; }
    }
    return static_cast<double>(best);
}

// ts_rank: rolling percentile rank — fraction of values in trailing window < current
inline double ts_rank(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const double current = x.data()[idx];
    double count = 0.0;
    for (std::size_t i = 0; i < period; ++i)
        count += (x.data()[idx - period + 1 + i] < current) ? 1.0 : 0.0;
    return count / static_cast<double>(period);
}

// correlation: Pearson correlation over trailing window
inline double correlation(const Line<double>& x, const Line<double>& y,
                          std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const std::size_t start = idx - period + 1;
    const double mx = simd::reduce_sum(&x.data()[start], period) / static_cast<double>(period);
    const double my = simd::reduce_sum(&y.data()[start], period) / static_cast<double>(period);
    double cov = 0.0, vx = 0.0, vy = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double dx = x.data()[start + i] - mx;
        const double dy = y.data()[start + i] - my;
        cov += dx * dy;
        vx += dx * dx;
        vy += dy * dy;
    }
    const double denom = std::sqrt(vx * vy);
    if (denom == 0.0) return NaN;
    return cov / denom;
}

// decay_linear: weighted average with linearly decaying weights [period, period-1, ..., 1]
inline double decay_linear(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        const double w = static_cast<double>(i + 1);
        num += x.data()[idx - period + 1 + i] * w;
        den += w;
    }
    return num / den;
}

// scale: self-scaling within single symbol.
// scale(x) at index idx = x[idx] / sum(abs(x[0..idx]))
// Simplified: just returns x[idx] normalized, with fallback to 0.
inline double scale(double value, double abs_sum) {
    if (abs_sum == 0.0) return 0.0;
    return value / abs_sum;
}

// covariance: population covariance over trailing window
inline double covariance(const Line<double>& x, const Line<double>& y,
                         std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    const std::size_t start = idx - period + 1;
    const double mx = simd::reduce_sum(&x.data()[start], period) / static_cast<double>(period);
    const double my = simd::reduce_sum(&y.data()[start], period) / static_cast<double>(period);
    double cov = 0.0;
    for (std::size_t i = 0; i < period; ++i) {
        cov += (x.data()[start + i] - mx) * (y.data()[start + i] - my);
    }
    return cov / static_cast<double>(period);
}

// product: rolling product over trailing window
inline double product(const Line<double>& x, std::size_t idx, std::size_t period) {
    if (idx + 1 < period) return NaN;
    double p = 1.0;
    for (std::size_t i = 0; i < period; ++i)
        p *= x.data()[idx - period + 1 + i];
    return p;
}

// Helpers for intermediate Line building within an indicator.
// These are used when an alpha needs to build scratch lines during warmup.

// adv{N}: SMA of volume over N bars
inline double adv(const Line<double>& volume, std::size_t idx, std::size_t n) {
    return sma(volume, idx, n);
}

// vwap approximation: typical price = (high + low + close) / 3
inline double vwap(const Line<double>& high, const Line<double>& low,
                   const Line<double>& close, std::size_t idx) {
    return (high.data()[idx] + low.data()[idx] + close.data()[idx]) / 3.0;
}

// returns = (close[t] - close[t-1]) / close[t-1]
inline double returns(const Line<double>& close, std::size_t idx) {
    if (idx == 0) return NaN;
    const double prev = close.data()[idx - 1];
    if (prev == 0.0) return NaN;
    return (close.data()[idx] - prev) / prev;
}

} // namespace stratforge::alpha101
