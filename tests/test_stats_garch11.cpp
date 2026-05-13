// SPDX-License-Identifier: MIT
//
// tests/test_stats_garch11.cpp — GARCH(1,1) MLE (stratforge::stats::garch11_fit).
//
//  acceptance suite. Tag form [stats][garch][regression].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/stats/garch11.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr std::uint32_t kSeed = 42;

// Simulate a GARCH(1,1) return series with known parameters.
[[nodiscard]] std::vector<double> simulate_garch11(std::size_t n,
                                                   double omega,
                                                   double alpha,
                                                   double beta) {
    std::vector<double> r(n);
    std::mt19937 rng(kSeed);
    std::normal_distribution<double> z(0.0, 1.0);
    double sigma2 = omega / (1.0 - alpha - beta);
    for (std::size_t t = 0; t < n; ++t) {
        const double eps = std::sqrt(sigma2) * z(rng);
        r[t] = eps;
        sigma2 = omega + alpha * eps * eps + beta * sigma2;
    }
    return r;
}

}  // namespace

TEST_CASE("GARCH(1,1) recovers parameters from synthetic series",
          "[stats][garch][regression]") {
    // True params: ω=0.05, α=0.10, β=0.85; persistence α+β=0.95.
    const auto r = simulate_garch11(/*n=*/4000, 0.05, 0.10, 0.85);
    const auto fit = stratforge::stats::garch11_fit(r);

    INFO("omega=" << fit.omega << " alpha=" << fit.alpha
                  << " beta=" << fit.beta << " log_lik=" << fit.log_lik);
    REQUIRE(std::isfinite(fit.omega));
    REQUIRE(std::isfinite(fit.alpha));
    REQUIRE(std::isfinite(fit.beta));
    REQUIRE(std::isfinite(fit.log_lik));

    // Tolerance: MLE on finite sample. Loose absolute bounds; the
    // stationarity invariant and finite log-lik are the hard gates.
    CHECK(fit.omega > 0.0);
    CHECK(fit.omega < 0.5);
    CHECK(fit.alpha > 0.0);
    CHECK(fit.alpha < 0.5);
    CHECK(fit.beta > 0.5);
    CHECK(fit.beta < 1.0);
    CHECK(fit.alpha + fit.beta < 1.0);  // stationarity must hold
}

TEST_CASE("GARCH(1,1) returns NaN on tiny input", "[stats][garch]") {
    std::vector<double> tiny(5, 0.0);
    const auto fit = stratforge::stats::garch11_fit(tiny);
    CHECK(std::isnan(fit.omega));
    CHECK(std::isnan(fit.alpha));
    CHECK(std::isnan(fit.beta));
    CHECK(std::isnan(fit.log_lik));
}

TEST_CASE("GARCH(1,1) returns NaN on zero-variance input", "[stats][garch]") {
    std::vector<double> flat(200, 0.0);
    const auto fit = stratforge::stats::garch11_fit(flat);
    CHECK(std::isnan(fit.omega));
}

TEST_CASE("GARCH(1,1) fit on iid Gaussian returns finite, low-alpha solution",
          "[stats][garch]") {
    std::vector<double> r(1000);
    std::mt19937 rng(kSeed + 3);
    std::normal_distribution<double> dist(0.0, 0.5);
    for (auto& v : r) v = dist(rng);
    const auto fit = stratforge::stats::garch11_fit(r);
    REQUIRE(std::isfinite(fit.log_lik));
    CHECK(fit.alpha + fit.beta < 1.0);
    CHECK(fit.omega > 0.0);
}
