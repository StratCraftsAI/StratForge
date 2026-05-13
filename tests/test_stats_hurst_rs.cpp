// SPDX-License-Identifier: MIT
//
// tests/test_stats_hurst_rs.cpp — Hurst R/S (stratforge::stats::hurst_rs).
//
//  acceptance suite. Tag form [stats][hurst][regression].

#include <catch2/catch_test_macros.hpp>

#include <stratforge/stats/hurst_rs.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr std::uint32_t kSeed = 42;

[[nodiscard]] std::vector<double> iid_gaussian(std::size_t n) {
    std::vector<double> y(n);
    std::mt19937 rng(kSeed);
    std::normal_distribution<double> dist(0.0, 1.0);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = dist(rng);
    }
    return y;
}

// Integrated random walk: cumsum of iid gaussian. H should be close to 1.0
// (the integral of a Brownian motion is anti-persistent in the sense that
// R/S grows almost linearly with n, not as n^0.5).
[[nodiscard]] std::vector<double> integrated_random_walk(std::size_t n) {
    auto eps = iid_gaussian(n);
    std::vector<double> y(n);
    y[0] = eps[0];
    for (std::size_t i = 1; i < n; ++i) {
        y[i] = y[i - 1] + eps[i];
    }
    return y;
}

// Mean-reverting AR(1) with small phi: anti-persistent (H < 0.5).
[[nodiscard]] std::vector<double> mean_reverting(std::size_t n) {
    std::vector<double> y(n);
    std::mt19937 rng(kSeed + 2);
    std::normal_distribution<double> dist(0.0, 1.0);
    y[0] = dist(rng);
    for (std::size_t i = 1; i < n; ++i) {
        // Negative AR(1) coefficient → strong oscillation → low Hurst.
        y[i] = -0.5 * y[i - 1] + dist(rng);
    }
    return y;
}

}  // namespace

TEST_CASE("Hurst R/S returns ~0.5 for iid Gaussian", "[stats][hurst][regression]") {
    const auto y = iid_gaussian(2048);
    const double h = stratforge::stats::hurst_rs(y);
    INFO("H=" << h);
    REQUIRE(std::isfinite(h));
    CHECK(h > 0.35);
    CHECK(h < 0.65);
}

TEST_CASE("Hurst R/S returns persistent value for integrated random walk",
          "[stats][hurst][regression]") {
    const auto y = integrated_random_walk(2048);
    const double h = stratforge::stats::hurst_rs(y);
    INFO("H=" << h);
    REQUIRE(std::isfinite(h));
    CHECK(h > 0.7);  // strongly trending
}

TEST_CASE("Hurst R/S returns anti-persistent value for mean-reverting series",
          "[stats][hurst][regression]") {
    const auto y = mean_reverting(2048);
    const double h = stratforge::stats::hurst_rs(y);
    INFO("H=" << h);
    REQUIRE(std::isfinite(h));
    CHECK(h < 0.5);
}

TEST_CASE("Hurst R/S handles tiny input gracefully", "[stats][hurst]") {
    std::vector<double> tiny(10, 1.0);
    const double h = stratforge::stats::hurst_rs(tiny);
    CHECK(std::isnan(h));
}

TEST_CASE("Hurst R/S handles constant series", "[stats][hurst]") {
    std::vector<double> flat(512, 3.14);
    const double h = stratforge::stats::hurst_rs(flat);
    CHECK(std::isnan(h));
}
