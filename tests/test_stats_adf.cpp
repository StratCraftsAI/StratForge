// SPDX-License-Identifier: MIT
//
// tests/test_stats_adf.cpp — Augmented Dickey-Fuller (stratforge::stats::adf_test).
//
//  acceptance suite. Tag form [stats][adf][regression].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/stats/adf.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr std::uint32_t kSeed = 42;

[[nodiscard]] std::vector<double> random_walk(std::size_t n,
                                              double sigma = 1.0) {
    std::vector<double> y(n);
    std::mt19937 rng(kSeed);
    std::normal_distribution<double> dist(0.0, sigma);
    y[0] = 0.0;
    for (std::size_t i = 1; i < n; ++i) {
        y[i] = y[i - 1] + dist(rng);
    }
    return y;
}

[[nodiscard]] std::vector<double> stationary_ar1(std::size_t n,
                                                 double phi = 0.3,
                                                 double sigma = 1.0) {
    std::vector<double> y(n);
    std::mt19937 rng(kSeed + 1);
    std::normal_distribution<double> dist(0.0, sigma);
    y[0] = dist(rng);
    for (std::size_t i = 1; i < n; ++i) {
        y[i] = phi * y[i - 1] + dist(rng);
    }
    return y;
}

}  // namespace

TEST_CASE("ADF rejects unit root on stationary AR(1)",
          "[stats][adf][regression]") {
    const auto y = stationary_ar1(500, /*phi=*/0.3);
    const auto r = stratforge::stats::adf_test(y);

    INFO("statistic=" << r.statistic << " p_value=" << r.p_value
                      << " lags=" << r.lags);
    REQUIRE(std::isfinite(r.statistic));
    REQUIRE(std::isfinite(r.p_value));
    CHECK(r.statistic < 0.0);             // unit-root direction
    CHECK(r.p_value < 0.05);              // reject at 5%
}

TEST_CASE("ADF does not reject unit root on random walk",
          "[stats][adf][regression]") {
    const auto y = random_walk(500);
    const auto r = stratforge::stats::adf_test(y);

    INFO("statistic=" << r.statistic << " p_value=" << r.p_value
                      << " lags=" << r.lags);
    REQUIRE(std::isfinite(r.statistic));
    REQUIRE(std::isfinite(r.p_value));
    CHECK(r.p_value > 0.05);              // do NOT reject at 5%
}

TEST_CASE("ADF respects explicit max_lags", "[stats][adf]") {
    const auto y = stationary_ar1(300);
    const auto r = stratforge::stats::adf_test(y, /*max_lags=*/4);
    CHECK(r.lags == 4);
}

TEST_CASE("ADF AIC lag selection stays within Schwert bound", "[stats][adf]") {
    const auto y = random_walk(400);
    const auto r = stratforge::stats::adf_test(y);
    // Schwert upper for n=400: ceil(12 * (4)^0.25) = ceil(12 * 1.4142) = 17
    CHECK(r.lags >= 0);
    CHECK(r.lags <= 17);
}

TEST_CASE("ADF degenerates gracefully on tiny input", "[stats][adf]") {
    std::vector<double> tiny{1.0, 2.0, 3.0};
    const auto r = stratforge::stats::adf_test(tiny);
    CHECK(std::isnan(r.statistic));
    CHECK(std::isnan(r.p_value));
}

TEST_CASE("ADF p-value is bounded in (0, 1)", "[stats][adf]") {
    const auto y_stat = stationary_ar1(200);
    const auto y_walk = random_walk(200);
    const auto r1 = stratforge::stats::adf_test(y_stat);
    const auto r2 = stratforge::stats::adf_test(y_walk);
    REQUIRE(std::isfinite(r1.p_value));
    REQUIRE(std::isfinite(r2.p_value));
    CHECK(r1.p_value > 0.0);
    CHECK(r1.p_value < 1.0);
    CHECK(r2.p_value > 0.0);
    CHECK(r2.p_value < 1.0);
}
