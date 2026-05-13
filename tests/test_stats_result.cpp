// SPDX-License-Identifier: MIT
//
// tests/test_stats_result.cpp — stratforge::stats::HypothesisResult.
//
//  §5 acceptance tests. Tag form [stats][result][regression].

#include <catch2/catch_test_macros.hpp>

#include <stratforge/stats/result.hpp>
// Alias headers must also compile through the public API.
#include <stratforge/stats/garch.hpp>
#include <stratforge/stats/hmm.hpp>
#include <stratforge/stats/hurst.hpp>

#include <string>
#include <vector>

TEST_CASE("HypothesisResult default-constructs to neutral values", "[stats][result][regression]") {
    stratforge::stats::HypothesisResult r{};

    REQUIRE(r.hypothesis_id.empty());
    REQUIRE(r.test_name.empty());
    REQUIRE(r.statistic == 0.0);
    REQUIRE(r.p_value   == 0.0);
    REQUIRE(r.passed    == false);
    REQUIRE(r.note.empty());
}

TEST_CASE("HypothesisResult round-trips assigned fields", "[stats][result][regression]") {
    stratforge::stats::HypothesisResult r{
        .hypothesis_id = "H42",
        .test_name     = "adf",
        .statistic     = -3.21,
        .p_value       = 0.014,
        .passed        = true,
        .note          = "constant-only regression",
    };

    REQUIRE(r.hypothesis_id == "H42");
    REQUIRE(r.test_name == "adf");
    REQUIRE(r.statistic == -3.21);
    REQUIRE(r.p_value == 0.014);
    REQUIRE(r.passed);
    REQUIRE(r.note == "constant-only regression");
}

TEST_CASE("HypothesisResult is usable as a vector element", "[stats][result][regression]") {
    std::vector<stratforge::stats::HypothesisResult> rs;
    rs.push_back({.hypothesis_id = "H1", .test_name = "hurst_rs", .passed = false});
    rs.push_back({.hypothesis_id = "H2", .test_name = "garch11",  .passed = true});

    REQUIRE(rs.size() == 2);
    REQUIRE(rs[0].test_name == "hurst_rs");
    REQUIRE(rs[1].passed);
}
