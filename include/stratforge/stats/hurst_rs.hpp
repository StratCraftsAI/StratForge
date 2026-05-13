// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/hurst_rs.hpp — Hurst exponent via R/S analysis.
//
// : Statistical operators for Signal Discovery (StratCraft P3).
//
// Standard Mandelbrot R/S: partition the series into non-overlapping chunks
// of geometrically-spaced sizes, compute (range of cumulative deviations)/
// (sample stddev) for each chunk, average across chunks at the same size,
// log-log regress mean R/S vs chunk size. Slope = H.
//
// Distinct from the streaming `HurstExponent` indicator under
// `<stratforge/indicators/hurst.hpp>` which exposes a per-bar API. This
// batch operator is used by Signal Discovery Round 3 hypothesis tests.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace stratforge::stats {

namespace detail::hurst_rs {

// Geometric ladder of chunk sizes: 8, 16, 32, ... bounded by n/2 (need at
// least two chunks at each size).
[[nodiscard]] inline std::vector<std::size_t> chunk_sizes(std::size_t n) noexcept {
    std::vector<std::size_t> out;
    if (n < 16) {
        return out;
    }
    const std::size_t max_chunk = n / 2;
    for (std::size_t s = 8; s <= max_chunk; s *= 2) {
        out.push_back(s);
    }
    return out;
}

// R/S statistic over a single chunk. Returns NaN if the chunk has zero
// variance (constant series).
[[nodiscard]] inline double rs_chunk(std::span<const double> chunk) noexcept {
    const std::size_t m = chunk.size();
    if (m < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Raw-value range guard: detects truly constant chunks before FP roundoff
    // can produce a fake variance signal. (For e.g. all-3.14 the binary mean
    // is slightly biased, accumulating into a small but nonzero cumulative
    // deviation and a small stddev — R/S = O(N) under that regime.)
    double raw_min = chunk[0];
    double raw_max = chunk[0];
    for (double v : chunk) {
        if (v < raw_min) raw_min = v;
        if (v > raw_max) raw_max = v;
    }
    if (raw_max == raw_min) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Mean.
    double sum = 0.0;
    for (double v : chunk) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(m);

    // Cumulative deviation from mean; track range.
    double cum = 0.0;
    double cmin = 0.0;
    double cmax = 0.0;
    double var_sum = 0.0;
    for (std::size_t i = 0; i < m; ++i) {
        const double dev = chunk[i] - mean;
        cum += dev;
        if (cum < cmin) cmin = cum;
        if (cum > cmax) cmax = cum;
        var_sum += dev * dev;
    }
    const double range = cmax - cmin;
    const double stddev = std::sqrt(var_sum / static_cast<double>(m));

    if (!(stddev > 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return range / stddev;
}

}  // namespace detail::hurst_rs

// Hurst exponent via R/S analysis.
//
// Returns NaN if the series is too short (< 16 samples) or has insufficient
// variance to produce at least 2 valid chunk-size points for the log-log
// regression.
//
// Interpretation:
//   H ≈ 0.5  → uncorrelated random walk
//   H > 0.5  → persistent / trending
//   H < 0.5  → anti-persistent / mean-reverting
[[nodiscard]] inline double hurst_rs(std::span<const double> series) noexcept {
    const std::size_t n = series.size();
    const auto sizes = detail::hurst_rs::chunk_sizes(n);
    if (sizes.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::vector<double> log_s;
    std::vector<double> log_rs;
    log_s.reserve(sizes.size());
    log_rs.reserve(sizes.size());

    for (std::size_t s : sizes) {
        const std::size_t n_chunks = n / s;
        if (n_chunks < 2) {
            continue;
        }
        double rs_sum = 0.0;
        std::size_t rs_count = 0;
        for (std::size_t c = 0; c < n_chunks; ++c) {
            std::span<const double> chunk = series.subspan(c * s, s);
            const double rs = detail::hurst_rs::rs_chunk(chunk);
            if (std::isfinite(rs)) {
                rs_sum += rs;
                ++rs_count;
            }
        }
        if (rs_count == 0) {
            continue;
        }
        const double mean_rs = rs_sum / static_cast<double>(rs_count);
        if (!(mean_rs > 0.0)) {
            continue;
        }
        log_s.push_back(std::log(static_cast<double>(s)));
        log_rs.push_back(std::log(mean_rs));
    }

    if (log_s.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // OLS slope of log_rs on log_s.
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (std::size_t i = 0; i < log_s.size(); ++i) {
        sum_x += log_s[i];
        sum_y += log_rs[i];
    }
    const double n_pts = static_cast<double>(log_s.size());
    const double mean_x = sum_x / n_pts;
    const double mean_y = sum_y / n_pts;

    double sxx = 0.0;
    double sxy = 0.0;
    for (std::size_t i = 0; i < log_s.size(); ++i) {
        const double dx = log_s[i] - mean_x;
        sxx += dx * dx;
        sxy += dx * (log_rs[i] - mean_y);
    }

    if (!(sxx > 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sxy / sxx;
}

}  // namespace stratforge::stats
