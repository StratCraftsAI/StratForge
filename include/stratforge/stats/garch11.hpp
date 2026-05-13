// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/garch11.hpp — GARCH(1,1) MLE fit.
//
// : Statistical operators for Signal Discovery (StratCraft P3).
//
// Model:
//   r_t        = ε_t
//   ε_t | F_t  = σ_t · z_t,    z_t ~ N(0, 1)
//   σ²_t       = ω + α · ε²_{t-1} + β · σ²_{t-1}
//
// Parameters fit via maximum likelihood; the optimizer is an in-tree
// Nelder-Mead simplex over the reparameterization (log ω, logit α, logit β).
// This enforces ω > 0 and α, β ∈ (0, 1) without constraints in the
// optimizer; stationarity α + β < 1 is enforced by penalty (negative
// log-likelihood returns +∞).

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace stratforge::stats {

struct Garch11Fit {
    double omega;
    double alpha;
    double beta;
    double log_lik;
};

namespace detail::garch11 {

constexpr int kMaxIter = 10000;
constexpr double kTolParam = 1e-10;
constexpr double kPenalty = 1e10;

[[nodiscard]] inline double logistic(double x) noexcept {
    // Numerically-stable logistic.
    if (x >= 0.0) {
        const double e = std::exp(-x);
        return 1.0 / (1.0 + e);
    }
    const double e = std::exp(x);
    return e / (1.0 + e);
}

// Negative log-likelihood at reparameterized point (u0, u1, u2):
//   ω = exp(u0),  α = logistic(u1),  β = logistic(u2)
[[nodiscard]] inline double neg_log_lik(std::span<const double> r,
                                        double u0, double u1, double u2) noexcept {
    const double omega = std::exp(u0);
    const double alpha = logistic(u1);
    const double beta  = logistic(u2);

    if (!(omega > 0.0) || !std::isfinite(omega) ||
        !std::isfinite(alpha) || !std::isfinite(beta)) {
        return kPenalty;
    }
    if (alpha + beta >= 0.9999) {
        return kPenalty; // enforce strict stationarity
    }

    // σ²_0 = unconditional variance = ω / (1 - α - β).
    const double denom = 1.0 - alpha - beta;
    if (!(denom > 1e-12)) {
        return kPenalty;
    }
    double sigma2 = omega / denom;
    if (!(sigma2 > 0.0)) {
        return kPenalty;
    }

    double nll = 0.0;
    constexpr double k_ln_2pi = 1.8378770664093454835606594728112352;
    for (std::size_t t = 0; t < r.size(); ++t) {
        if (!(sigma2 > 0.0)) {
            return kPenalty;
        }
        // log N(r_t; 0, σ²_t) = -0.5 (ln(2π) + ln σ² + r²/σ²)
        nll += 0.5 * (k_ln_2pi + std::log(sigma2) + (r[t] * r[t]) / sigma2);
        sigma2 = omega + alpha * (r[t] * r[t]) + beta * sigma2;
    }
    if (!std::isfinite(nll)) {
        return kPenalty;
    }
    return nll;
}

struct Vec3 {
    double v[3];
    [[nodiscard]] double& operator[](std::size_t i) noexcept { return v[i]; }
    [[nodiscard]] double  operator[](std::size_t i) const noexcept { return v[i]; }
};

[[nodiscard]] inline Vec3 add(const Vec3& a, const Vec3& b) noexcept {
    return {{a[0] + b[0], a[1] + b[1], a[2] + b[2]}};
}
[[nodiscard]] inline Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
    return {{a[0] - b[0], a[1] - b[1], a[2] - b[2]}};
}
[[nodiscard]] inline Vec3 scale(const Vec3& a, double s) noexcept {
    return {{a[0] * s, a[1] * s, a[2] * s}};
}

// Nelder-Mead simplex minimization for 3-d unconstrained problem. Returns
// best vertex's parameters.
[[nodiscard]] inline Vec3 nelder_mead(std::span<const double> r,
                                      Vec3 x0,
                                      double initial_step,
                                      double& out_best_nll) noexcept {
    constexpr double kAlpha = 1.0;  // reflection
    constexpr double kGamma = 2.0;  // expansion
    constexpr double kRho   = 0.5;  // contraction
    constexpr double kSigma = 0.5;  // shrink

    std::array<Vec3, 4> simplex{};
    std::array<double, 4> nll{};

    simplex[0] = x0;
    for (int i = 1; i < 4; ++i) {
        Vec3 p = x0;
        p[static_cast<std::size_t>(i - 1)] += initial_step;
        simplex[static_cast<std::size_t>(i)] = p;
    }
    for (int i = 0; i < 4; ++i) {
        const auto& p = simplex[static_cast<std::size_t>(i)];
        nll[static_cast<std::size_t>(i)] = neg_log_lik(r, p[0], p[1], p[2]);
    }

    auto sort_simplex = [&]() noexcept {
        std::array<int, 4> idx{0, 1, 2, 3};
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            return nll[static_cast<std::size_t>(a)] < nll[static_cast<std::size_t>(b)];
        });
        std::array<Vec3, 4> ssim{};
        std::array<double, 4> snll{};
        for (int i = 0; i < 4; ++i) {
            ssim[static_cast<std::size_t>(i)] = simplex[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])];
            snll[static_cast<std::size_t>(i)] = nll[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])];
        }
        simplex = ssim;
        nll = snll;
    };

    for (int iter = 0; iter < kMaxIter; ++iter) {
        sort_simplex();

        // Convergence: max span across vertices below tol.
        double span_max = 0.0;
        for (std::size_t d = 0; d < 3; ++d) {
            double lo = simplex[0][d];
            double hi = simplex[0][d];
            for (int i = 1; i < 4; ++i) {
                const double v = simplex[static_cast<std::size_t>(i)][d];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            span_max = std::max(span_max, hi - lo);
        }
        if (span_max < kTolParam) {
            break;
        }

        // Centroid of best 3 (excluding worst at index 3).
        Vec3 centroid{};
        for (int i = 0; i < 3; ++i) {
            centroid = add(centroid, simplex[static_cast<std::size_t>(i)]);
        }
        centroid = scale(centroid, 1.0 / 3.0);

        // Reflection.
        const Vec3 worst = simplex[3];
        Vec3 xr = add(centroid, scale(sub(centroid, worst), kAlpha));
        const double fr = neg_log_lik(r, xr[0], xr[1], xr[2]);

        if (fr < nll[2] && fr >= nll[0]) {
            simplex[3] = xr;
            nll[3] = fr;
            continue;
        }
        if (fr < nll[0]) {
            // Expansion.
            Vec3 xe = add(centroid, scale(sub(xr, centroid), kGamma));
            const double fe = neg_log_lik(r, xe[0], xe[1], xe[2]);
            if (fe < fr) {
                simplex[3] = xe;
                nll[3] = fe;
            } else {
                simplex[3] = xr;
                nll[3] = fr;
            }
            continue;
        }

        // Contraction.
        Vec3 xc = add(centroid, scale(sub(worst, centroid), kRho));
        const double fc = neg_log_lik(r, xc[0], xc[1], xc[2]);
        if (fc < nll[3]) {
            simplex[3] = xc;
            nll[3] = fc;
            continue;
        }

        // Shrink toward best vertex.
        for (int i = 1; i < 4; ++i) {
            simplex[static_cast<std::size_t>(i)] = add(
                simplex[0],
                scale(sub(simplex[static_cast<std::size_t>(i)], simplex[0]), kSigma)
            );
            nll[static_cast<std::size_t>(i)] = neg_log_lik(
                r,
                simplex[static_cast<std::size_t>(i)][0],
                simplex[static_cast<std::size_t>(i)][1],
                simplex[static_cast<std::size_t>(i)][2]
            );
        }
    }

    sort_simplex();
    out_best_nll = nll[0];
    return simplex[0];
}

[[nodiscard]] inline double logit(double p) noexcept {
    return std::log(p / (1.0 - p));
}

}  // namespace detail::garch11

// GARCH(1,1) MLE fit. Returns {omega, alpha, beta, log_lik}; on degenerate
// input (size < 10, zero variance) returns NaN log_lik.
[[nodiscard]] inline Garch11Fit garch11_fit(std::span<const double> returns) noexcept {
    Garch11Fit out{};
    out.omega   = std::numeric_limits<double>::quiet_NaN();
    out.alpha   = std::numeric_limits<double>::quiet_NaN();
    out.beta    = std::numeric_limits<double>::quiet_NaN();
    out.log_lik = std::numeric_limits<double>::quiet_NaN();

    const std::size_t n = returns.size();
    if (n < 10) {
        return out;
    }

    // Sample variance.
    double sum = 0.0;
    for (double v : returns) sum += v;
    const double mean = sum / static_cast<double>(n);
    double var_sum = 0.0;
    for (double v : returns) {
        const double d = v - mean;
        var_sum += d * d;
    }
    const double var = var_sum / static_cast<double>(n);
    if (!(var > 0.0)) {
        return out;
    }

    // Initial point: ω₀ = 0.05·var, α₀ = 0.05, β₀ = 0.90.
    detail::garch11::Vec3 x0{};
    x0[0] = std::log(0.05 * var);
    x0[1] = detail::garch11::logit(0.05);
    x0[2] = detail::garch11::logit(0.90);

    double best_nll = std::numeric_limits<double>::quiet_NaN();
    const auto best = detail::garch11::nelder_mead(returns, x0, 0.5, best_nll);

    if (!std::isfinite(best_nll) || best_nll >= detail::garch11::kPenalty) {
        return out;
    }

    out.omega   = std::exp(best[0]);
    out.alpha   = detail::garch11::logistic(best[1]);
    out.beta    = detail::garch11::logistic(best[2]);
    out.log_lik = -best_nll;
    return out;
}

}  // namespace stratforge::stats
