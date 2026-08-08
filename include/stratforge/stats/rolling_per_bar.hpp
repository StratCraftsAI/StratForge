// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/rolling_per_bar.hpp -- Rolling sub-window wrappers
// that populate HypothesisResult::per_bar_scores from batch statistical tests.
//
// : converts window-level hypotheses into genuine per-bar signals
// so the combinator receives real variance instead of scalar broadcasts.
//
// : incremental algorithms for ADF and Hurst eliminate per-bar
// full-refit overhead.  ADF uses Sherman-Morrison rank-1 (X'X)^{-1} update
// with fixed lag count (AIC on first window only) -- O(k^2) per step instead
// of O(17 * n * k + k^3).  Hurst pre-allocates chunk buffers to avoid
// repeated allocation.
//
// Five strategies:
//   HMM:   p_state0 is already per-bar -- move it directly (zero extra cost).
//   ADF:   incremental sliding-window OLS -> -log10(p_value) per bar.
//   Hurst: sliding sub-window R/S -> H exponent per bar (buffer-reuse).
//   GARCH: single fit -> conditional variance series (no re-fitting per bar).

#pragma once

#include <stratforge/stats/adf.hpp>
#include <stratforge/stats/garch11.hpp>
#include <stratforge/stats/hmm2.hpp>
#include <stratforge/stats/hurst_rs.hpp>
#include <stratforge/stats/result.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace stratforge::stats {

// HMM: p_state0 is already a per-bar vector of smoothed posteriors P(state=0).
// Re-run hmm2_gaussian on the same series and move the result into per_bar_scores.
inline void populate_hmm_per_bar(std::span<const double> series,
                                 HypothesisResult& result) noexcept {
    auto hmm = hmm2_gaussian(series);
    if (!hmm.p_state0.empty()) {
        result.per_bar_scores = std::move(hmm.p_state0);
    }
}

// ---------------------------------------------------------------------------
// ADF: incremental rolling sub-window via Sherman-Morrison (X'X)^{-1} update.
//
// Algorithm:
//   1. Run full AIC lag-selection on the FIRST window to pick optimal p.
//   2. Build initial X'X, X'y, and (X'X)^{-1} for that first window.
//   3. For each subsequent window position, rank-1 subtract the leaving row
//      and rank-1 add the entering row to X'X, X'y, and (X'X)^{-1}.
//   4. Compute beta = (X'X)^{-1} X'y, SE, t-stat, p-value in O(k^2).
//
// This eliminates the 17x AIC loop and reduces per-step cost from
// O(17 * (n*k + k^3)) to O(k^2).
// ---------------------------------------------------------------------------

namespace detail::rolling_adf {

// Build the row vector x[col] for observation at position `obs_idx` within
// the ADF regression.  The regression is:
//   Dy[t] = alpha + gamma * y[t-1] + sum_{i=1..p} delta_i * Dy[t-i] + eps
// Column layout: [const=1, y_{t-1}, Dy_{t-1}, ..., Dy_{t-p}]
//
// `obs_idx` is the index into the effective sample (0-based), corresponding
// to the original series index `first + p + 1 + obs_idx` where `first` is
// the start of the window.
inline void build_row(std::span<const double> series_window,
                      std::span<const double> dy,
                      int p,
                      std::size_t obs_idx,
                      std::span<double> row_out) noexcept {
    // t in series_window coordinates = p + 1 + obs_idx
    // but dy is offset by 1: dy[j] = series_window[j+1] - series_window[j]
    // So obs_idx in dy coordinates = p + obs_idx
    row_out[0] = 1.0;
    row_out[1] = series_window[static_cast<std::size_t>(p) + obs_idx]; // y_{t-1}
    for (int i = 1; i <= p; ++i) {
        // Dy_{t-i} = dy[p + obs_idx - i]
        row_out[static_cast<std::size_t>(i + 1)] =
            dy[static_cast<std::size_t>(p) + obs_idx - static_cast<std::size_t>(i)];
    }
}

// Sherman-Morrison: update A^{-1} after A -> A + sigma * v * v^T
// where sigma = +1 (add) or -1 (remove).
// A_inv is k x k stored row-major.
// Returns false if the update would make the matrix singular.
[[nodiscard]] inline bool sherman_morrison_update(
    std::vector<double>& A_inv,
    std::span<const double> v,
    double sigma,
    std::size_t k) noexcept {

    // u = A_inv * v
    std::vector<double> u(k, 0.0);
    for (std::size_t i = 0; i < k; ++i) {
        double s = 0.0;
        for (std::size_t j = 0; j < k; ++j) {
            s += A_inv[i * k + j] * v[j];
        }
        u[i] = s;
    }

    // denom = 1 + sigma * v^T * u
    double vtu = 0.0;
    for (std::size_t j = 0; j < k; ++j) {
        vtu += v[j] * u[j];
    }
    const double denom = 1.0 + sigma * vtu;
    if (std::fabs(denom) < 1e-14) {
        return false;
    }

    // A_inv <- A_inv - sigma * (u * u^T) / denom
    const double factor = sigma / denom;
    for (std::size_t i = 0; i < k; ++i) {
        for (std::size_t j = 0; j < k; ++j) {
            A_inv[i * k + j] -= factor * u[i] * u[j];
        }
    }
    return true;
}

}  // namespace detail::rolling_adf

inline void rolling_adf_scores(std::span<const double> series,
                                HypothesisResult& result,
                                std::size_t lookback = 252) noexcept {
    const std::size_t n = series.size();
    result.per_bar_scores.assign(n, 0.0);
    if (n < lookback || lookback < 4) return;

    // --- Step 1: AIC lag selection on first window ---
    auto first_window = series.subspan(0, lookback);
    auto first_adf = adf_test(first_window);
    const int p = std::max(first_adf.lags, 0);
    const std::size_t k = static_cast<std::size_t>(p + 2);

    if (lookback < static_cast<std::size_t>(p + 3)) return;
    const std::size_t n_eff = lookback - 1 - static_cast<std::size_t>(p);

    // Pre-compute Dy for the entire series.
    std::vector<double> dy_full(n > 0 ? n - 1 : 0);
    for (std::size_t t = 1; t < n; ++t) {
        dy_full[t - 1] = series[t] - series[t - 1];
    }

    // Scratch buffers (allocated once, reused).
    std::vector<double> row(k, 0.0);
    std::vector<double> row_old(k, 0.0);
    std::vector<double> XtX_inv(k * k, 0.0);
    std::vector<double> Xty(k, 0.0);
    std::vector<double> beta(k, 0.0);
    std::vector<double> XtX_scratch(k * k, 0.0);
    std::vector<double> A_scratch(k * (2 * k), 0.0);
    double sum_yy = 0.0;

    // Bootstrap (X'X)^{-1}, X'y, sum_yy from a window starting at win_start.
    // Returns false if X'X is singular (degenerate window).
    auto bootstrap = [&](std::size_t win_start) -> bool {
        auto window = series.subspan(win_start, lookback);
        auto dy_win = std::span<const double>(dy_full.data() + win_start, lookback - 1);

        std::fill(XtX_scratch.begin(), XtX_scratch.end(), 0.0);
        Xty.assign(k, 0.0);
        sum_yy = 0.0;

        for (std::size_t r = 0; r < n_eff; ++r) {
            detail::rolling_adf::build_row(window, dy_win, p, r, row);
            const double y_r = dy_win[static_cast<std::size_t>(p) + r];
            sum_yy += y_r * y_r;
            for (std::size_t i = 0; i < k; ++i) {
                Xty[i] += row[i] * y_r;
                for (std::size_t j = 0; j < k; ++j) {
                    XtX_scratch[i * k + j] += row[i] * row[j];
                }
            }
        }

        // Gauss-Jordan inversion of X'X.
        std::fill(A_scratch.begin(), A_scratch.end(), 0.0);
        for (std::size_t i = 0; i < k; ++i) {
            for (std::size_t j = 0; j < k; ++j)
                A_scratch[i * (2 * k) + j] = XtX_scratch[i * k + j];
            A_scratch[i * (2 * k) + (k + i)] = 1.0;
        }

        for (std::size_t i = 0; i < k; ++i) {
            std::size_t piv = i;
            double piv_abs = std::fabs(A_scratch[i * (2 * k) + i]);
            for (std::size_t r = i + 1; r < k; ++r) {
                const double v = std::fabs(A_scratch[r * (2 * k) + i]);
                if (v > piv_abs) { piv_abs = v; piv = r; }
            }
            if (piv_abs < 1e-14) return false;
            if (piv != i) {
                for (std::size_t c = 0; c < 2 * k; ++c)
                    std::swap(A_scratch[i * (2 * k) + c], A_scratch[piv * (2 * k) + c]);
            }
            const double inv_piv = 1.0 / A_scratch[i * (2 * k) + i];
            for (std::size_t c = 0; c < 2 * k; ++c)
                A_scratch[i * (2 * k) + c] *= inv_piv;
            for (std::size_t r = 0; r < k; ++r) {
                if (r == i) continue;
                const double f = A_scratch[r * (2 * k) + i];
                if (f == 0.0) continue;
                for (std::size_t c = 0; c < 2 * k; ++c)
                    A_scratch[r * (2 * k) + c] -= f * A_scratch[i * (2 * k) + c];
            }
        }

        for (std::size_t i = 0; i < k; ++i)
            for (std::size_t j = 0; j < k; ++j)
                XtX_inv[i * k + j] = A_scratch[i * (2 * k) + (k + j)];

        return true;
    };

    // Emit -log10(p_value) for the current (XtX_inv, Xty, sum_yy) state.
    auto emit_score = [&](std::size_t bar_idx) {
        for (std::size_t i = 0; i < k; ++i) {
            double s = 0.0;
            for (std::size_t j = 0; j < k; ++j)
                s += XtX_inv[i * k + j] * Xty[j];
            beta[i] = s;
        }

        const double gamma = beta[1];
        double beta_xty = 0.0;
        for (std::size_t j = 0; j < k; ++j)
            beta_xty += beta[j] * Xty[j];

        const double rss = sum_yy - beta_xty;
        if (!(rss > 0.0)) return;

        const double sigma2 = rss / static_cast<double>(n_eff - k);
        const double var_gamma = sigma2 * XtX_inv[1 * k + 1];
        if (!(var_gamma > 0.0)) return;

        const double se = std::sqrt(var_gamma);
        const double tstat = gamma / se;
        const double pval = detail::adf::tstat_to_pvalue(tstat, n_eff);

        if (std::isfinite(pval) && pval > 0.0) {
            result.per_bar_scores[bar_idx] =
                -std::log10(std::max(pval, 1e-8));
        }
    };

    // --- Step 2: Initial bootstrap ---
    if (!bootstrap(0)) {
        // Singular first window — fall back to batch for everything.
        for (std::size_t i = lookback; i <= n; ++i) {
            auto window = series.subspan(i - lookback, lookback);
            auto adf = adf_test(window);
            if (std::isfinite(adf.p_value) && adf.p_value > 0.0) {
                result.per_bar_scores[i - 1] =
                    -std::log10(std::max(adf.p_value, 1e-8));
            }
        }
        return;
    }

    emit_score(lookback - 1);

    // --- Step 3: Slide window, periodic re-bootstrap to control FP drift ---
    constexpr std::size_t kRebootstrapInterval = 512;
    std::size_t steps_since_bootstrap = 0;

    for (std::size_t win_start = 1; win_start + lookback <= n; ++win_start) {
        ++steps_since_bootstrap;

        // Periodic re-bootstrap resets accumulated FP error in (X'X)^{-1}.
        if (steps_since_bootstrap >= kRebootstrapInterval) {
            bootstrap(win_start);
            steps_since_bootstrap = 0;
            emit_score(win_start + lookback - 1);
            continue;
        }

        auto window = series.subspan(win_start, lookback);
        auto dy_win = std::span<const double>(dy_full.data() + win_start, lookback - 1);

        auto prev_window = series.subspan(win_start - 1, lookback);
        auto prev_dy = std::span<const double>(
            dy_full.data() + win_start - 1, lookback - 1);

        // Leaving row: obs_idx=0 of previous window.
        detail::rolling_adf::build_row(prev_window, prev_dy, p, 0, row_old);
        const double y_old = prev_dy[static_cast<std::size_t>(p)];

        // Entering row: obs_idx=n_eff-1 of current window.
        detail::rolling_adf::build_row(window, dy_win, p, n_eff - 1, row);
        const double y_new = dy_win[static_cast<std::size_t>(p) + n_eff - 1];

        // Incremental X'y and y'y updates.
        for (std::size_t j = 0; j < k; ++j)
            Xty[j] += row[j] * y_new - row_old[j] * y_old;
        sum_yy += y_new * y_new - y_old * y_old;

        // Sherman-Morrison: remove old row, add new row.
        bool ok = detail::rolling_adf::sherman_morrison_update(
            XtX_inv, row_old, -1.0, k);
        if (ok)
            ok = detail::rolling_adf::sherman_morrison_update(
                XtX_inv, row, +1.0, k);

        if (!ok) {
            // Singular update — re-bootstrap and continue.
            bootstrap(win_start);
            steps_since_bootstrap = 0;
        }

        emit_score(win_start + lookback - 1);
    }
}

// ---------------------------------------------------------------------------
// Hurst: sliding sub-window R/S with pre-allocated scratch buffers.
// Algorithm unchanged from batch hurst_rs; optimization is buffer reuse.
// ---------------------------------------------------------------------------
inline void rolling_hurst_scores(std::span<const double> series,
                                  HypothesisResult& result,
                                  std::size_t lookback = 252) noexcept {
    const std::size_t n = series.size();
    result.per_bar_scores.assign(n, 0.5);
    if (n < lookback || lookback < 16) return;

    // Pre-compute chunk sizes (constant for fixed lookback).
    const auto chunk_sizes = detail::hurst_rs::chunk_sizes(lookback);
    if (chunk_sizes.size() < 2) return;

    // Pre-allocate log-log regression buffers.
    std::vector<double> log_s;
    std::vector<double> log_rs;
    log_s.reserve(chunk_sizes.size());
    log_rs.reserve(chunk_sizes.size());

    for (std::size_t i = lookback; i <= n; ++i) {
        auto window = series.subspan(i - lookback, lookback);

        log_s.clear();
        log_rs.clear();

        for (std::size_t s : chunk_sizes) {
            const std::size_t n_chunks = lookback / s;
            if (n_chunks < 2) continue;

            double rs_sum = 0.0;
            std::size_t rs_count = 0;
            for (std::size_t c = 0; c < n_chunks; ++c) {
                auto chunk = window.subspan(c * s, s);
                const double rs = detail::hurst_rs::rs_chunk(chunk);
                if (std::isfinite(rs)) {
                    rs_sum += rs;
                    ++rs_count;
                }
            }
            if (rs_count == 0) continue;

            const double mean_rs = rs_sum / static_cast<double>(rs_count);
            if (!(mean_rs > 0.0)) continue;

            log_s.push_back(std::log(static_cast<double>(s)));
            log_rs.push_back(std::log(mean_rs));
        }

        if (log_s.size() < 2) continue;

        // OLS slope: H = Sxy / Sxx
        double sum_x = 0.0, sum_y = 0.0;
        for (std::size_t j = 0; j < log_s.size(); ++j) {
            sum_x += log_s[j];
            sum_y += log_rs[j];
        }
        const double n_pts = static_cast<double>(log_s.size());
        const double mean_x = sum_x / n_pts;
        const double mean_y = sum_y / n_pts;

        double sxx = 0.0, sxy = 0.0;
        for (std::size_t j = 0; j < log_s.size(); ++j) {
            const double dx = log_s[j] - mean_x;
            sxx += dx * dx;
            sxy += dx * (log_rs[j] - mean_y);
        }
        if (!(sxx > 0.0)) continue;

        const double h = sxy / sxx;
        if (std::isfinite(h)) {
            result.per_bar_scores[i - 1] = h;
        }
    }
}

// GARCH: single fit on entire series, then forward-pass conditional variance.
// Output: sqrt(sigma2_t) per bar (conditional volatility).
// This is O(N) after the single O(N * max_iter) fit -- no rolling re-fit.
inline void rolling_garch_scores(std::span<const double> returns,
                                  HypothesisResult& result) noexcept {
    const std::size_t n = returns.size();
    result.per_bar_scores.assign(n, 0.0);
    if (n < 10) return;

    auto fit = garch11_fit(returns);
    if (!std::isfinite(fit.log_lik)) return;

    const double denom = 1.0 - fit.alpha - fit.beta;
    if (!(denom > 1e-12)) return;
    double sigma2 = fit.omega / denom;

    for (std::size_t t = 0; t < n; ++t) {
        if (sigma2 > 0.0) {
            result.per_bar_scores[t] = std::sqrt(sigma2);
        }
        sigma2 = fit.omega + fit.alpha * (returns[t] * returns[t]) + fit.beta * sigma2;
    }
}

// Dispatcher: inspect test_name and call the appropriate rolling function.
// Series should be closes for ADF/Hurst/HMM, returns for GARCH.
// Returns true if per_bar_scores was populated, false if test_name unrecognised.
inline bool populate_rolling_per_bar(std::string_view test_name,
                                     std::span<const double> closes,
                                     std::span<const double> returns,
                                     std::size_t n_bars,
                                     HypothesisResult& result) noexcept {
    if (test_name.find("hmm") != std::string_view::npos) {
        populate_hmm_per_bar(closes, result);
        return true;
    }
    if (test_name.find("adf") != std::string_view::npos) {
        rolling_adf_scores(closes, result);
        return true;
    }
    if (test_name.find("hurst") != std::string_view::npos) {
        rolling_hurst_scores(closes, result);
        return true;
    }
    if (test_name.find("garch") != std::string_view::npos) {
        rolling_garch_scores(returns, result);
        return true;
    }
    return false;
}

}  // namespace stratforge::stats
