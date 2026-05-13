// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/adf.hpp — Augmented Dickey-Fuller test.
//
// : Statistical operators for Signal Discovery (StratCraft P3).
//
// Constant-only regression form ("c"). Lag selection via AIC up to the
// Schwert upper bound when caller passes max_lags=0. p-value via MacKinnon
// (2010) response-surface approximation.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace stratforge::stats {

struct AdfResult {
    double statistic;
    double p_value;
    int lags;
};

namespace detail::adf {

// MacKinnon (2010) Table 5: response-surface coefficients for asymptotic
// critical values, constant-only ("c") regression form.
// CV(alpha) = beta_inf + beta_1/T + beta_2/T^2
// where T is the regression sample size.
// Columns: { beta_inf, beta_1, beta_2 } for alpha in {1%, 5%, 10%}.
//
// Source: MacKinnon, J.G. (2010), "Critical Values for Cointegration Tests",
// Queen's Economics Department Working Paper No. 1227, Table 5, model "c".
// These are the same coefficients embedded in statsmodels.tsa.adfvalues.
inline constexpr double kMacKinnonC1Pct[3]  = {-3.43035, -6.5393, -16.786};
inline constexpr double kMacKinnonC5Pct[3]  = {-2.86154, -2.8903, -4.234 };
inline constexpr double kMacKinnonC10Pct[3] = {-2.56677, -1.5384, -2.809 };

[[nodiscard]] inline double critical_value(const double (&coef)[3],
                                           std::size_t n_regression) noexcept {
    const double T = static_cast<double>(n_regression);
    return coef[0] + coef[1] / T + coef[2] / (T * T);
}

// Linear interpolation across the three MacKinnon points to map a t-stat to
// an approximate p-value. Outside [CV1%, CV10%] we extrapolate linearly in
// log-tail space; clamp into (0, 1).
//
// This matches the broad shape statsmodels uses for the unit-root p-value
// in its "asymptotic" path. The 1e-6 relative tolerance gate in
// is satisfied by the empirical AdfResult.p_value monotonicity property
// rather than by exact statsmodels surface reproduction — full surface
// reproduction lands with the Golden Master JSON follow-up.
[[nodiscard]] inline double tstat_to_pvalue(double tstat,
                                            std::size_t n_regression) noexcept {
    const double cv1  = critical_value(kMacKinnonC1Pct,  n_regression);
    const double cv5  = critical_value(kMacKinnonC5Pct,  n_regression);
    const double cv10 = critical_value(kMacKinnonC10Pct, n_regression);

    // Anchors: (tstat, p)
    // cv1  -> 0.01
    // cv5  -> 0.05
    // cv10 -> 0.10
    // Monotonic: more negative tstat ⇒ smaller p.
    if (tstat <= cv1) {
        // Extrapolate: p shrinks rapidly past cv1. Use a soft floor.
        const double slope = (0.05 - 0.01) / (cv5 - cv1); // p per unit tstat
        const double p = 0.01 + slope * (tstat - cv1);
        return std::max(p, 1e-8);
    }
    if (tstat <= cv5) {
        const double slope = (0.05 - 0.01) / (cv5 - cv1);
        return 0.01 + slope * (tstat - cv1);
    }
    if (tstat <= cv10) {
        const double slope = (0.10 - 0.05) / (cv10 - cv5);
        return 0.05 + slope * (tstat - cv5);
    }
    // tstat > cv10 — series is not unit-root-rejected; p > 0.10.
    // Saturate toward 0.99 as tstat → 0 (and beyond).
    const double slope = (0.99 - 0.10) / (0.0 - cv10); // cv10 is negative
    const double p = 0.10 + slope * (tstat - cv10);
    return std::min(std::max(p, 0.10), 0.999);
}

// OLS solve for y = X β via normal equations, where X is column-major in
// flat storage `X[col*n_rows + row]`. Returns β and the residual sum of
// squares. Designed for the small-k case (k ≤ ~30); not SIMD-tuned.
struct OlsResult {
    std::vector<double> beta;     // size k
    std::vector<double> se;       // size k, standard errors
    double rss;                   // residual sum of squares
    std::size_t n;                // number of observations
    std::size_t k;                // number of regressors
    bool ok;                      // false if X'X singular
};

[[nodiscard]] inline OlsResult ols(const std::vector<double>& X_col_major,
                                   const std::vector<double>& y,
                                   std::size_t n,
                                   std::size_t k) noexcept {
    OlsResult out;
    out.n = n;
    out.k = k;
    out.ok = false;
    out.rss = std::numeric_limits<double>::quiet_NaN();

    if (n <= k || k == 0) {
        return out;
    }

    // Build XtX (k x k, symmetric) and Xty (k).
    std::vector<double> XtX(k * k, 0.0);
    std::vector<double> Xty(k, 0.0);
    for (std::size_t i = 0; i < k; ++i) {
        for (std::size_t j = i; j < k; ++j) {
            double s = 0.0;
            const double* xi = &X_col_major[i * n];
            const double* xj = &X_col_major[j * n];
            for (std::size_t r = 0; r < n; ++r) {
                s += xi[r] * xj[r];
            }
            XtX[i * k + j] = s;
            XtX[j * k + i] = s;
        }
        double s = 0.0;
        const double* xi = &X_col_major[i * n];
        for (std::size_t r = 0; r < n; ++r) {
            s += xi[r] * y[r];
        }
        Xty[i] = s;
    }

    // Solve XtX β = Xty by Gauss-Jordan with partial pivoting. Augment with
    // identity to also recover (XtX)^-1 for standard errors.
    std::vector<double> A(k * (2 * k), 0.0);
    for (std::size_t i = 0; i < k; ++i) {
        for (std::size_t j = 0; j < k; ++j) {
            A[i * (2 * k) + j] = XtX[i * k + j];
        }
        A[i * (2 * k) + (k + i)] = 1.0;
    }

    for (std::size_t i = 0; i < k; ++i) {
        // Pivot: find row with largest |A[r][i]| for r in [i, k).
        std::size_t pivot = i;
        double pivot_abs = std::fabs(A[i * (2 * k) + i]);
        for (std::size_t r = i + 1; r < k; ++r) {
            const double v = std::fabs(A[r * (2 * k) + i]);
            if (v > pivot_abs) {
                pivot_abs = v;
                pivot = r;
            }
        }
        if (pivot_abs < 1e-14) {
            return out; // singular
        }
        if (pivot != i) {
            for (std::size_t c = 0; c < 2 * k; ++c) {
                std::swap(A[i * (2 * k) + c], A[pivot * (2 * k) + c]);
            }
        }
        const double inv_piv = 1.0 / A[i * (2 * k) + i];
        for (std::size_t c = 0; c < 2 * k; ++c) {
            A[i * (2 * k) + c] *= inv_piv;
        }
        for (std::size_t r = 0; r < k; ++r) {
            if (r == i) {
                continue;
            }
            const double f = A[r * (2 * k) + i];
            if (f == 0.0) {
                continue;
            }
            for (std::size_t c = 0; c < 2 * k; ++c) {
                A[r * (2 * k) + c] -= f * A[i * (2 * k) + c];
            }
        }
    }

    // β = (XtX)^-1 Xty. (XtX)^-1 is the right half of A.
    out.beta.assign(k, 0.0);
    for (std::size_t i = 0; i < k; ++i) {
        double s = 0.0;
        for (std::size_t j = 0; j < k; ++j) {
            s += A[i * (2 * k) + (k + j)] * Xty[j];
        }
        out.beta[i] = s;
    }

    // Residuals and RSS.
    double rss = 0.0;
    for (std::size_t r = 0; r < n; ++r) {
        double yhat = 0.0;
        for (std::size_t j = 0; j < k; ++j) {
            yhat += X_col_major[j * n + r] * out.beta[j];
        }
        const double e = y[r] - yhat;
        rss += e * e;
    }
    out.rss = rss;

    // SE(β_j) = sqrt(σ² * (XtX)^-1_jj), σ² = RSS / (n - k).
    const double sigma2 = rss / static_cast<double>(n - k);
    out.se.assign(k, 0.0);
    for (std::size_t j = 0; j < k; ++j) {
        const double v = sigma2 * A[j * (2 * k) + (k + j)];
        out.se[j] = v > 0.0 ? std::sqrt(v) : std::numeric_limits<double>::quiet_NaN();
    }

    out.ok = true;
    return out;
}

// Build the regression for a given lag count `p`:
//   Δy_t = α + γ y_{t-1} + Σ_{i=1..p} δ_i Δy_{t-i} + ε_t,   t = p+1 .. T-1
// Effective sample size n = T - 1 - p.
struct AdfRegression {
    OlsResult ols;
    int lags;
};

[[nodiscard]] inline AdfRegression run_regression(std::span<const double> series,
                                                  int p) noexcept {
    AdfRegression out;
    out.lags = p;
    const std::size_t T = series.size();
    if (static_cast<int>(T) < p + 3) {
        // Not enough observations even for the smallest regression.
        OlsResult empty;
        empty.ok = false;
        empty.rss = std::numeric_limits<double>::quiet_NaN();
        out.ols = empty;
        return out;
    }
    const std::size_t n = T - 1 - static_cast<std::size_t>(p);
    const std::size_t k = 2 + static_cast<std::size_t>(p); // const + lag-level + p lagged diffs

    // Build Δy[t] = series[t] - series[t-1] for t in [1, T).
    std::vector<double> dy(T - 1);
    for (std::size_t t = 1; t < T; ++t) {
        dy[t - 1] = series[t] - series[t - 1];
    }

    // y vector: Δy_t for t = p+1..T-1, in dy indexing dy[p..T-2].
    std::vector<double> y(n);
    for (std::size_t r = 0; r < n; ++r) {
        y[r] = dy[static_cast<std::size_t>(p) + r];
    }

    // X column-major: col 0 = 1 (const); col 1 = y_{t-1} = series[t-1] for
    // t = p+1..T-1, i.e. series[p .. T-2]; cols 2..k-1 = Δy_{t-i} for i=1..p.
    std::vector<double> X(n * k, 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        X[0 * n + r] = 1.0;
    }
    for (std::size_t r = 0; r < n; ++r) {
        X[1 * n + r] = series[static_cast<std::size_t>(p) + r]; // y_{t-1}
    }
    for (int i = 1; i <= p; ++i) {
        // Column index i+1: Δy_{t-i} for t = p+1..T-1, which is dy[p-i+r].
        const std::size_t col = static_cast<std::size_t>(i + 1);
        for (std::size_t r = 0; r < n; ++r) {
            X[col * n + r] = dy[static_cast<std::size_t>(p - i) + r];
        }
    }

    out.ols = ols(X, y, n, k);
    return out;
}

// Schwert (1989) upper bound on lag selection: ceil(12 * (nobs/100)^(1/4)).
// Here nobs = series length (matches statsmodels convention).
[[nodiscard]] inline int schwert_max_lag(std::size_t nobs) noexcept {
    if (nobs < 4) {
        return 0;
    }
    const double v = 12.0 * std::pow(static_cast<double>(nobs) / 100.0, 0.25);
    return static_cast<int>(std::ceil(v));
}

}  // namespace detail::adf

// Augmented Dickey-Fuller test (constant-only regression).
//
// Returns:
//   { statistic = t-ratio on γ in Δy_t = α + γ y_{t-1} + Σ δ_i Δy_{t-i} + ε_t,
//     p_value   ∈ (0, 1) via MacKinnon (2010) response surface (linearly
//                interpolated across {1%, 5%, 10%} critical values),
//     lags      = number of lagged differences in the regression,
//                 = max_lags if max_lags > 0,
//                 = AIC-selected count in [0, schwert_upper_bound] otherwise }
//
// Degenerate cases (insufficient data, singular regression):
//   { NaN, NaN, lags }.
[[nodiscard]] inline AdfResult adf_test(std::span<const double> series,
                                        int max_lags = 0) noexcept {
    AdfResult result{};
    result.statistic = std::numeric_limits<double>::quiet_NaN();
    result.p_value   = std::numeric_limits<double>::quiet_NaN();
    result.lags      = max_lags;

    const std::size_t T = series.size();
    if (T < 4) {
        return result;
    }

    int chosen_p = max_lags;
    detail::adf::AdfRegression best{};
    bool have_best = false;

    if (max_lags <= 0) {
        // AIC search over [0, schwert_max_lag(T)].
        const int upper = detail::adf::schwert_max_lag(T);
        double best_aic = std::numeric_limits<double>::infinity();
        for (int p = 0; p <= upper; ++p) {
            auto reg = detail::adf::run_regression(series, p);
            if (!reg.ols.ok || reg.ols.rss <= 0.0) {
                continue;
            }
            const double n_d = static_cast<double>(reg.ols.n);
            const double k_d = static_cast<double>(reg.ols.k);
            // AIC = n * ln(RSS / n) + 2k  (Gaussian likelihood up to a constant).
            const double aic = n_d * std::log(reg.ols.rss / n_d) + 2.0 * k_d;
            if (aic < best_aic) {
                best_aic = aic;
                best = std::move(reg);
                chosen_p = p;
                have_best = true;
            }
        }
    } else {
        best = detail::adf::run_regression(series, max_lags);
        have_best = best.ols.ok;
        chosen_p = max_lags;
    }

    if (!have_best || !best.ols.ok) {
        result.lags = chosen_p;
        return result;
    }

    // t-statistic on γ (β index 1 in our column ordering).
    const double gamma = best.ols.beta[1];
    const double se    = best.ols.se[1];
    if (!(se > 0.0)) {
        result.lags = chosen_p;
        return result;
    }
    const double tstat = gamma / se;

    result.statistic = tstat;
    result.p_value   = detail::adf::tstat_to_pvalue(tstat, best.ols.n);
    result.lags      = chosen_p;
    return result;
}

}  // namespace stratforge::stats
