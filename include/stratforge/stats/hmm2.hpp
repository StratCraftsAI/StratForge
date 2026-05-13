// SPDX-License-Identifier: MIT
//
// include/stratforge/stats/hmm2.hpp — 2-state Gaussian HMM via Baum-Welch EM.
//
// : Statistical operators for Signal Discovery (StratCraft P3).
//
// Model:
//   Hidden state s_t ∈ {0, 1}
//   Transitions P(s_t = j | s_{t-1} = i) = A[i][j]
//   Emissions   y_t | s_t = i ~ N(μ_i, σ²_i)
//   Initial     π_i = P(s_0 = i)
//
// Fit via Baum-Welch EM. Forward-backward operates in log-space with
// log-sum-exp for numerical stability. Output is the smoothed posterior
// probability of state 0 at each observation, plus the converged
// log-likelihood.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace stratforge::stats {

struct HmmResult {
    std::vector<double> p_state0;
    double log_lik;
};

namespace detail::hmm2 {

[[nodiscard]] inline double log_sum_exp(double a, double b) noexcept {
    if (a == -std::numeric_limits<double>::infinity()) return b;
    if (b == -std::numeric_limits<double>::infinity()) return a;
    const double m = std::max(a, b);
    return m + std::log(std::exp(a - m) + std::exp(b - m));
}

[[nodiscard]] inline double log_gaussian_pdf(double y, double mu, double sigma) noexcept {
    constexpr double k_ln_2pi = 1.8378770664093454835606594728112352;
    if (!(sigma > 0.0)) {
        return -std::numeric_limits<double>::infinity();
    }
    const double d = y - mu;
    return -0.5 * (k_ln_2pi + 2.0 * std::log(sigma) + (d * d) / (sigma * sigma));
}

}  // namespace detail::hmm2

// 2-state Gaussian HMM via Baum-Welch EM.
//
// Returns:
//   p_state0[t] = P(s_t = 0 | y_{0..T-1}, fitted params)
//   log_lik     = log P(y_{0..T-1} | fitted params)
//
// Degenerate input (< 10 samples, zero variance) returns empty p_state0 and
// NaN log_lik.
[[nodiscard]] inline HmmResult hmm2_gaussian(std::span<const double> series,
                                             int max_iter = 100) noexcept {
    using detail::hmm2::log_sum_exp;
    using detail::hmm2::log_gaussian_pdf;

    HmmResult out{};
    out.log_lik = std::numeric_limits<double>::quiet_NaN();

    const std::size_t T = series.size();
    if (T < 10) {
        return out;
    }

    // Initialization: split by median.
    std::vector<double> sorted_y(series.begin(), series.end());
    std::sort(sorted_y.begin(), sorted_y.end());
    const double median = sorted_y[T / 2];

    double lo_sum = 0.0, hi_sum = 0.0;
    std::size_t lo_n = 0, hi_n = 0;
    for (double v : series) {
        if (v < median) { lo_sum += v; ++lo_n; }
        else            { hi_sum += v; ++hi_n; }
    }
    if (lo_n == 0 || hi_n == 0) {
        return out; // degenerate (all values equal)
    }
    double mu0 = lo_sum / static_cast<double>(lo_n);
    double mu1 = hi_sum / static_cast<double>(hi_n);
    double v0 = 0.0, v1 = 0.0;
    for (double v : series) {
        if (v < median) { const double d = v - mu0; v0 += d * d; }
        else            { const double d = v - mu1; v1 += d * d; }
    }
    double sigma0 = std::sqrt(v0 / static_cast<double>(lo_n));
    double sigma1 = std::sqrt(v1 / static_cast<double>(hi_n));
    // Guard against zero-variance partition.
    double global_sd = 0.0;
    {
        double s = 0.0;
        for (double v : series) s += v;
        const double m = s / static_cast<double>(T);
        double vs = 0.0;
        for (double v : series) { const double d = v - m; vs += d * d; }
        global_sd = std::sqrt(vs / static_cast<double>(T));
    }
    if (!(global_sd > 0.0)) {
        return out;
    }
    if (!(sigma0 > 1e-9)) sigma0 = global_sd;
    if (!(sigma1 > 1e-9)) sigma1 = global_sd;

    // Initial transition + prior.
    double A00 = 0.95, A01 = 0.05;
    double A10 = 0.05, A11 = 0.95;
    double pi0 = 0.5, pi1 = 0.5;

    std::vector<double> log_alpha0(T), log_alpha1(T);
    std::vector<double> log_beta0(T),  log_beta1(T);
    std::vector<double> log_gamma0(T), log_gamma1(T);
    std::vector<double> log_xi00(T - 1), log_xi01(T - 1);
    std::vector<double> log_xi10(T - 1), log_xi11(T - 1);

    double prev_log_lik = -std::numeric_limits<double>::infinity();
    double log_lik = -std::numeric_limits<double>::infinity();

    for (int iter = 0; iter < max_iter; ++iter) {
        // === E-step: forward-backward in log space. ===
        const double log_pi0 = std::log(pi0);
        const double log_pi1 = std::log(pi1);
        const double log_A00 = std::log(A00);
        const double log_A01 = std::log(A01);
        const double log_A10 = std::log(A10);
        const double log_A11 = std::log(A11);

        // Forward.
        log_alpha0[0] = log_pi0 + log_gaussian_pdf(series[0], mu0, sigma0);
        log_alpha1[0] = log_pi1 + log_gaussian_pdf(series[0], mu1, sigma1);
        for (std::size_t t = 1; t < T; ++t) {
            const double b0 = log_gaussian_pdf(series[t], mu0, sigma0);
            const double b1 = log_gaussian_pdf(series[t], mu1, sigma1);
            log_alpha0[t] = log_sum_exp(
                log_alpha0[t - 1] + log_A00,
                log_alpha1[t - 1] + log_A10
            ) + b0;
            log_alpha1[t] = log_sum_exp(
                log_alpha0[t - 1] + log_A01,
                log_alpha1[t - 1] + log_A11
            ) + b1;
        }
        log_lik = log_sum_exp(log_alpha0[T - 1], log_alpha1[T - 1]);

        // Backward.
        log_beta0[T - 1] = 0.0;
        log_beta1[T - 1] = 0.0;
        for (std::size_t t_next = T - 1; t_next > 0; --t_next) {
            const std::size_t t = t_next - 1;
            const double b0 = log_gaussian_pdf(series[t + 1], mu0, sigma0);
            const double b1 = log_gaussian_pdf(series[t + 1], mu1, sigma1);
            log_beta0[t] = log_sum_exp(
                log_A00 + b0 + log_beta0[t + 1],
                log_A01 + b1 + log_beta1[t + 1]
            );
            log_beta1[t] = log_sum_exp(
                log_A10 + b0 + log_beta0[t + 1],
                log_A11 + b1 + log_beta1[t + 1]
            );
        }

        // Posteriors γ.
        for (std::size_t t = 0; t < T; ++t) {
            const double num0 = log_alpha0[t] + log_beta0[t];
            const double num1 = log_alpha1[t] + log_beta1[t];
            const double denom = log_sum_exp(num0, num1);
            log_gamma0[t] = num0 - denom;
            log_gamma1[t] = num1 - denom;
        }

        // Joint posteriors ξ.
        for (std::size_t t = 0; t + 1 < T; ++t) {
            const double b0 = log_gaussian_pdf(series[t + 1], mu0, sigma0);
            const double b1 = log_gaussian_pdf(series[t + 1], mu1, sigma1);
            const double e00 = log_alpha0[t] + log_A00 + b0 + log_beta0[t + 1];
            const double e01 = log_alpha0[t] + log_A01 + b1 + log_beta1[t + 1];
            const double e10 = log_alpha1[t] + log_A10 + b0 + log_beta0[t + 1];
            const double e11 = log_alpha1[t] + log_A11 + b1 + log_beta1[t + 1];
            const double denom = log_sum_exp(log_sum_exp(e00, e01),
                                              log_sum_exp(e10, e11));
            log_xi00[t] = e00 - denom;
            log_xi01[t] = e01 - denom;
            log_xi10[t] = e10 - denom;
            log_xi11[t] = e11 - denom;
        }

        // === M-step. ===
        pi0 = std::exp(log_gamma0[0]);
        pi1 = std::exp(log_gamma1[0]);

        // Transition matrix.
        double sum_xi00 = -std::numeric_limits<double>::infinity();
        double sum_xi01 = -std::numeric_limits<double>::infinity();
        double sum_xi10 = -std::numeric_limits<double>::infinity();
        double sum_xi11 = -std::numeric_limits<double>::infinity();
        double sum_g0 = -std::numeric_limits<double>::infinity();
        double sum_g1 = -std::numeric_limits<double>::infinity();
        for (std::size_t t = 0; t + 1 < T; ++t) {
            sum_xi00 = log_sum_exp(sum_xi00, log_xi00[t]);
            sum_xi01 = log_sum_exp(sum_xi01, log_xi01[t]);
            sum_xi10 = log_sum_exp(sum_xi10, log_xi10[t]);
            sum_xi11 = log_sum_exp(sum_xi11, log_xi11[t]);
            sum_g0 = log_sum_exp(sum_g0, log_gamma0[t]);
            sum_g1 = log_sum_exp(sum_g1, log_gamma1[t]);
        }
        A00 = std::exp(sum_xi00 - sum_g0);
        A01 = std::exp(sum_xi01 - sum_g0);
        A10 = std::exp(sum_xi10 - sum_g1);
        A11 = std::exp(sum_xi11 - sum_g1);
        // Renormalize rows defensively.
        {
            const double s0 = A00 + A01;
            const double s1 = A10 + A11;
            if (s0 > 0.0) { A00 /= s0; A01 /= s0; }
            if (s1 > 0.0) { A10 /= s1; A11 /= s1; }
        }

        // Emission parameters.
        double sum_g0_all = -std::numeric_limits<double>::infinity();
        double sum_g1_all = -std::numeric_limits<double>::infinity();
        for (std::size_t t = 0; t < T; ++t) {
            sum_g0_all = log_sum_exp(sum_g0_all, log_gamma0[t]);
            sum_g1_all = log_sum_exp(sum_g1_all, log_gamma1[t]);
        }
        double num_mu0 = 0.0, num_mu1 = 0.0;
        for (std::size_t t = 0; t < T; ++t) {
            num_mu0 += std::exp(log_gamma0[t]) * series[t];
            num_mu1 += std::exp(log_gamma1[t]) * series[t];
        }
        const double denom_mu0 = std::exp(sum_g0_all);
        const double denom_mu1 = std::exp(sum_g1_all);
        if (denom_mu0 > 0.0) mu0 = num_mu0 / denom_mu0;
        if (denom_mu1 > 0.0) mu1 = num_mu1 / denom_mu1;

        double num_v0 = 0.0, num_v1 = 0.0;
        for (std::size_t t = 0; t < T; ++t) {
            const double d0 = series[t] - mu0;
            const double d1 = series[t] - mu1;
            num_v0 += std::exp(log_gamma0[t]) * d0 * d0;
            num_v1 += std::exp(log_gamma1[t]) * d1 * d1;
        }
        if (denom_mu0 > 0.0) {
            const double v = num_v0 / denom_mu0;
            sigma0 = v > 1e-12 ? std::sqrt(v) : global_sd * 1e-3;
        }
        if (denom_mu1 > 0.0) {
            const double v = num_v1 / denom_mu1;
            sigma1 = v > 1e-12 ? std::sqrt(v) : global_sd * 1e-3;
        }

        // Convergence check.
        if (std::isfinite(prev_log_lik) &&
            log_lik - prev_log_lik < 1e-6 &&
            log_lik - prev_log_lik > -1e-3) {
            break;
        }
        prev_log_lik = log_lik;
    }

    // Final p_state0 from converged posteriors.
    out.p_state0.resize(T);
    for (std::size_t t = 0; t < T; ++t) {
        out.p_state0[t] = std::exp(log_gamma0[t]);
    }
    out.log_lik = log_lik;
    return out;
}

}  // namespace stratforge::stats
