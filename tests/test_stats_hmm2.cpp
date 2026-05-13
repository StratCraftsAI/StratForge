// SPDX-License-Identifier: MIT
//
// tests/test_stats_hmm2.cpp — 2-state Gaussian HMM (stratforge::stats::hmm2_gaussian).
//
//  acceptance suite. Tag form [stats][hmm][regression].

#include <catch2/catch_test_macros.hpp>

#include <stratforge/stats/hmm2.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr std::uint32_t kSeed = 42;

// Two-regime synthetic series: blocks of low-mean Gaussian followed by
// blocks of high-mean Gaussian. Returns (series, ground_truth_state[]).
struct RegimeSeries {
    std::vector<double> y;
    std::vector<int> true_state; // 0 = low regime, 1 = high regime
};

[[nodiscard]] RegimeSeries make_two_regime(std::size_t n_per_block,
                                           std::size_t n_blocks) {
    RegimeSeries out;
    const std::size_t total = n_per_block * n_blocks;
    out.y.reserve(total);
    out.true_state.reserve(total);

    std::mt19937 rng(kSeed);
    std::normal_distribution<double> low_dist(-1.0, 0.5);
    std::normal_distribution<double> high_dist(+1.0, 0.5);
    for (std::size_t b = 0; b < n_blocks; ++b) {
        const int state = static_cast<int>(b % 2);
        for (std::size_t i = 0; i < n_per_block; ++i) {
            const double v = (state == 0) ? low_dist(rng) : high_dist(rng);
            out.y.push_back(v);
            out.true_state.push_back(state);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("HMM2 identifies majority regime on two-regime series",
          "[stats][hmm][regression]") {
    const auto rs = make_two_regime(/*n_per_block=*/100, /*n_blocks=*/10);
    const auto r = stratforge::stats::hmm2_gaussian(rs.y);

    REQUIRE(r.p_state0.size() == rs.y.size());
    REQUIRE(std::isfinite(r.log_lik));

    // The label-permutation symmetry of HMM means "state 0" can map to
    // either ground-truth label. Count agreement under both assignments.
    std::size_t agree_normal = 0, agree_flipped = 0;
    for (std::size_t i = 0; i < rs.y.size(); ++i) {
        const int pred = r.p_state0[i] > 0.5 ? 0 : 1;
        if (pred == rs.true_state[i]) ++agree_normal;
        if (pred == (1 - rs.true_state[i])) ++agree_flipped;
    }
    const std::size_t agreement = std::max(agree_normal, agree_flipped);
    const double rate = static_cast<double>(agreement) / static_cast<double>(rs.y.size());
    INFO("agreement rate = " << rate);
    CHECK(rate > 0.85);  // generous bound; well-separated regimes
}

TEST_CASE("HMM2 p_state0 lies in [0, 1]", "[stats][hmm]") {
    const auto rs = make_two_regime(50, 4);
    const auto r = stratforge::stats::hmm2_gaussian(rs.y);
    for (std::size_t i = 0; i < r.p_state0.size(); ++i) {
        INFO("i=" << i << " p=" << r.p_state0[i]);
        CHECK(r.p_state0[i] >= 0.0);
        CHECK(r.p_state0[i] <= 1.0);
    }
}

TEST_CASE("HMM2 returns empty + NaN on tiny input", "[stats][hmm]") {
    std::vector<double> tiny{1.0, 2.0, 3.0};
    const auto r = stratforge::stats::hmm2_gaussian(tiny);
    CHECK(r.p_state0.empty());
    CHECK(std::isnan(r.log_lik));
}

TEST_CASE("HMM2 returns empty + NaN on constant input", "[stats][hmm]") {
    std::vector<double> flat(200, 7.0);
    const auto r = stratforge::stats::hmm2_gaussian(flat);
    CHECK(r.p_state0.empty());
    CHECK(std::isnan(r.log_lik));
}

TEST_CASE("HMM2 respects max_iter cap", "[stats][hmm]") {
    const auto rs = make_two_regime(40, 4);
    // max_iter=1 just to confirm no crash and basic shape.
    const auto r = stratforge::stats::hmm2_gaussian(rs.y, /*max_iter=*/1);
    REQUIRE(r.p_state0.size() == rs.y.size());
    REQUIRE(std::isfinite(r.log_lik));
}
