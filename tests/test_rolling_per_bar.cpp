// SPDX-License-Identifier: MIT
//
// tests/test_rolling_per_bar.cpp -- Rolling sub-window per-bar scores.
//
//  acceptance suite. Tag form [stats][rolling][regression].
// : incremental ADF accuracy and performance tests.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/stats/rolling_per_bar.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

constexpr std::uint32_t kSeed = 42;
constexpr std::size_t kLookback = 64;

std::vector<double> make_random_walk(std::size_t n) {
    std::mt19937 rng(kSeed);
    std::normal_distribution<double> dist(0.0, 1.0);
    std::vector<double> out(n);
    out[0] = 100.0;
    for (std::size_t i = 1; i < n; ++i) {
        out[i] = out[i - 1] + dist(rng);
    }
    return out;
}

std::vector<double> to_returns(const std::vector<double>& prices) {
    std::vector<double> ret(prices.size(), 0.0);
    for (std::size_t i = 1; i < prices.size(); ++i) {
        if (prices[i - 1] != 0.0) {
            ret[i] = (prices[i] - prices[i - 1]) / prices[i - 1];
        }
    }
    return ret;
}

std::vector<double> make_two_regime(std::size_t n) {
    std::mt19937 rng(kSeed);
    std::normal_distribution<double> low(-1.0, 0.5);
    std::normal_distribution<double> high(+1.0, 0.5);
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = ((i / 100) % 2 == 0) ? low(rng) : high(rng);
    }
    return out;
}

}  // namespace

TEST_CASE("populate_hmm_per_bar produces per-bar vector matching series length",
          "[stats][rolling][regression]") {
    const auto series = make_two_regime(500);
    stratforge::stats::HypothesisResult result{};
    result.test_name = "hmm2_gaussian";

    stratforge::stats::populate_hmm_per_bar(series, result);

    REQUIRE(result.per_bar_scores.size() == series.size());
    bool has_variance = false;
    for (std::size_t i = 1; i < result.per_bar_scores.size(); ++i) {
        if (result.per_bar_scores[i] != result.per_bar_scores[0]) {
            has_variance = true;
            break;
        }
    }
    REQUIRE(has_variance);
    for (double v : result.per_bar_scores) {
        REQUIRE(v >= 0.0);
        REQUIRE(v <= 1.0);
    }
}

TEST_CASE("rolling_adf_scores produces per-bar scores with lookback warmup",
          "[stats][rolling][regression]") {
    const auto prices = make_random_walk(300);
    stratforge::stats::HypothesisResult result{};

    stratforge::stats::rolling_adf_scores(prices, result, kLookback);

    REQUIRE(result.per_bar_scores.size() == prices.size());
    for (std::size_t i = 0; i < kLookback - 1; ++i) {
        REQUIRE(result.per_bar_scores[i] == 0.0);
    }
    bool has_nonzero = false;
    for (std::size_t i = kLookback; i < result.per_bar_scores.size(); ++i) {
        if (result.per_bar_scores[i] != 0.0) {
            has_nonzero = true;
        }
        REQUIRE(result.per_bar_scores[i] >= 0.0);
    }
    REQUIRE(has_nonzero);
}

TEST_CASE("rolling_hurst_scores produces H in valid range with lookback warmup",
          "[stats][rolling][regression]") {
    const auto prices = make_random_walk(300);
    stratforge::stats::HypothesisResult result{};

    stratforge::stats::rolling_hurst_scores(prices, result, kLookback);

    REQUIRE(result.per_bar_scores.size() == prices.size());
    for (std::size_t i = 0; i < kLookback - 1; ++i) {
        REQUIRE(result.per_bar_scores[i] == 0.5);
    }
    bool has_nonhalf = false;
    for (std::size_t i = kLookback; i < result.per_bar_scores.size(); ++i) {
        if (result.per_bar_scores[i] != 0.5) {
            has_nonhalf = true;
        }
        REQUIRE(result.per_bar_scores[i] >= 0.0);
        REQUIRE(result.per_bar_scores[i] <= 2.0);
    }
    REQUIRE(has_nonhalf);
}

TEST_CASE("rolling_garch_scores produces conditional vol series",
          "[stats][rolling][regression]") {
    const auto prices = make_random_walk(500);
    const auto returns = to_returns(prices);
    stratforge::stats::HypothesisResult result{};

    stratforge::stats::rolling_garch_scores(returns, result);

    REQUIRE(result.per_bar_scores.size() == returns.size());
    bool has_variance = false;
    for (std::size_t i = 1; i < result.per_bar_scores.size(); ++i) {
        if (result.per_bar_scores[i] != result.per_bar_scores[0]) {
            has_variance = true;
            break;
        }
    }
    REQUIRE(has_variance);
    for (double v : result.per_bar_scores) {
        REQUIRE(v >= 0.0);
    }
}

TEST_CASE("populate_rolling_per_bar dispatches by test_name",
          "[stats][rolling][regression]") {
    const auto prices = make_random_walk(300);
    const auto returns = to_returns(prices);

    SECTION("adf") {
        stratforge::stats::HypothesisResult result{};
        result.test_name = "adf_test";
        bool ok = stratforge::stats::populate_rolling_per_bar(
            "adf_test", prices, returns, prices.size(), result);
        REQUIRE(ok);
        REQUIRE(result.per_bar_scores.size() == prices.size());
    }

    SECTION("hurst") {
        stratforge::stats::HypothesisResult result{};
        result.test_name = "hurst_rs";
        bool ok = stratforge::stats::populate_rolling_per_bar(
            "hurst_rs", prices, returns, prices.size(), result);
        REQUIRE(ok);
        REQUIRE(result.per_bar_scores.size() == prices.size());
    }

    SECTION("hmm") {
        stratforge::stats::HypothesisResult result{};
        result.test_name = "hmm2_gaussian";
        bool ok = stratforge::stats::populate_rolling_per_bar(
            "hmm2_gaussian", prices, returns, prices.size(), result);
        REQUIRE(ok);
        REQUIRE(result.per_bar_scores.size() == prices.size());
    }

    SECTION("garch") {
        stratforge::stats::HypothesisResult result{};
        result.test_name = "garch11_fit";
        bool ok = stratforge::stats::populate_rolling_per_bar(
            "garch11_fit", prices, returns, prices.size(), result);
        REQUIRE(ok);
        REQUIRE(result.per_bar_scores.size() == returns.size());
    }

    SECTION("unknown test_name") {
        stratforge::stats::HypothesisResult result{};
        result.test_name = "unknown_test";
        bool ok = stratforge::stats::populate_rolling_per_bar(
            "unknown_test", prices, returns, prices.size(), result);
        REQUIRE_FALSE(ok);
        REQUIRE(result.per_bar_scores.empty());
    }
}

TEST_CASE("rolling functions handle degenerate input",
          "[stats][rolling][regression]") {
    std::vector<double> tiny = {1.0, 2.0, 3.0};
    stratforge::stats::HypothesisResult result{};

    SECTION("adf with series shorter than lookback") {
        stratforge::stats::rolling_adf_scores(tiny, result, 252);
        REQUIRE(result.per_bar_scores.size() == tiny.size());
        for (double v : result.per_bar_scores) {
            REQUIRE(v == 0.0);
        }
    }

    SECTION("hurst with series shorter than lookback") {
        stratforge::stats::rolling_hurst_scores(tiny, result, 252);
        REQUIRE(result.per_bar_scores.size() == tiny.size());
        for (double v : result.per_bar_scores) {
            REQUIRE(v == 0.5);
        }
    }

    SECTION("garch with series shorter than 10") {
        stratforge::stats::rolling_garch_scores(tiny, result);
        REQUIRE(result.per_bar_scores.size() == tiny.size());
        for (double v : result.per_bar_scores) {
            REQUIRE(v == 0.0);
        }
    }

    SECTION("hmm with series shorter than 10") {
        stratforge::stats::populate_hmm_per_bar(tiny, result);
        REQUIRE(result.per_bar_scores.empty());
    }
}

// : verify incremental ADF matches batch ADF (fixed lag) within FP tolerance.
TEST_CASE("incremental ADF matches batch ADF scores",
          "[stats][rolling][regression][1179]") {
    const auto prices = make_random_walk(2000);

    constexpr std::size_t lookback = 252;
    const std::size_t n = prices.size();
    std::vector<double> batch_scores(n, 0.0);

    // Get lag count from first window — matches incremental's fixed-lag choice.
    auto first_adf = stratforge::stats::adf_test(
        std::span<const double>(prices.data(), lookback));
    const int fixed_p = std::max(first_adf.lags, 0);

    // Batch: run_regression with fixed lag per window, then compute t-stat & p-value.
    // Cannot use adf_test(win, 0) because max_lags=0 triggers AIC search.
    const std::size_t n_eff = lookback - 1 - static_cast<std::size_t>(fixed_p);

    for (std::size_t i = lookback; i <= n; ++i) {
        auto window = std::span<const double>(prices.data() + i - lookback, lookback);
        auto reg = stratforge::stats::detail::adf::run_regression(window, fixed_p);
        if (!reg.ols.ok || reg.ols.se.size() < 2 || !(reg.ols.se[1] > 0.0)) continue;

        const double gamma = reg.ols.beta[1];
        const double se = reg.ols.se[1];
        const double tstat = gamma / se;
        const double pval = stratforge::stats::detail::adf::tstat_to_pvalue(tstat, n_eff);

        if (std::isfinite(pval) && pval > 0.0) {
            batch_scores[i - 1] = -std::log10(std::max(pval, 1e-8));
        }
    }

    // Incremental ADF.
    stratforge::stats::HypothesisResult result{};
    stratforge::stats::rolling_adf_scores(prices, result, lookback);

    REQUIRE(result.per_bar_scores.size() == n);

    for (std::size_t i = 0; i < lookback - 1; ++i) {
        REQUIRE(result.per_bar_scores[i] == 0.0);
    }

    std::size_t compared = 0;
    std::size_t mismatches = 0;
    double max_rel_err = 0.0;
    for (std::size_t i = lookback - 1; i < n; ++i) {
        const double batch = batch_scores[i];
        const double incr = result.per_bar_scores[i];

        if (batch == 0.0 && incr == 0.0) continue;
        ++compared;

        const double abs_err = std::fabs(batch - incr);
        const double denom = std::max(std::fabs(batch), std::fabs(incr));
        const double rel_err = denom > 0.0 ? abs_err / denom : abs_err;

        if (rel_err > max_rel_err) max_rel_err = rel_err;

        if (rel_err > 1e-6) {
            ++mismatches;
            if (mismatches <= 5) {
                std::printf("  mismatch at bar %zu: batch=%.10f incr=%.10f rel_err=%.2e\n",
                            i, batch, incr, rel_err);
            }
        }
    }

    std::printf("  fixed_p=%d, compared %zu bars, max_rel_err=%.2e, mismatches=%zu\n",
                fixed_p, compared, max_rel_err, mismatches);
    REQUIRE(mismatches == 0);
}

// : performance — incremental ADF on 100k bars should finish fast.
TEST_CASE("incremental ADF performance on large series",
          "[stats][rolling][regression][1179][.perf]") {
    constexpr std::size_t N = 100'000;
    const auto prices = make_random_walk(N);

    auto t0 = std::chrono::steady_clock::now();
    stratforge::stats::HypothesisResult result{};
    stratforge::stats::rolling_adf_scores(prices, result, 252);
    auto t1 = std::chrono::steady_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("  incremental ADF on %zu bars: %.1f ms\n", N, ms);

    REQUIRE(result.per_bar_scores.size() == N);
    // Sanity: must finish within 30s even on slow CI.
    REQUIRE(ms < 30'000.0);
}
