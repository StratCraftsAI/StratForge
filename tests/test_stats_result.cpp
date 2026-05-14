// SPDX-License-Identifier: MIT
//
// tests/test_stats_result.cpp — stratforge::stats::HypothesisResult.
//
//  §5 acceptance tests. Tag form [stats][result][regression].
// The 8-field shape mirrors the backend Round 2 prompt SSOT; any drift here
// breaks LLM-generated test compilation.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/stats/result.hpp>
// Alias headers must also compile through the public API.
#include <stratforge/stats/garch.hpp>
#include <stratforge/stats/hmm.hpp>
#include <stratforge/stats/hurst.hpp>

#include <map>
#include <string>
#include <variant>
#include <vector>

using stratforge::stats::HypothesisParameterValue;
using stratforge::stats::HypothesisResult;

TEST_CASE("HypothesisResult default-constructs to fail-safe values", "[stats][result][regression]") {
    HypothesisResult r{};

    REQUIRE(r.hypothesis_id.empty());
    REQUIRE(r.test_name.empty());
    REQUIRE(r.p_value        == 1.0);    // fail-safe: no test run ⇒ not significant
    REQUIRE(r.test_statistic == 0.0);
    REQUIRE(r.effect_size    == 0.0);
    REQUIRE(r.is_significant == false);
    REQUIRE(r.summary.empty());
    REQUIRE(r.parameters.empty());
}

TEST_CASE("HypothesisResult brace-inits with 8 positional args", "[stats][result][regression]") {
    // Matches the StratCraft merger dispatcher fallback init shape exactly.
    HypothesisResult r{
        "H_mr_eurusd_1d", "adf",
        0.012,   // p_value
        -3.45,   // test_statistic
        0.31,    // effect_size
        true,    // is_significant
        "ADF rejects unit-root at 5%",
        {{"alpha", 0.05}, {"lags", 5LL}, {"trend", std::string{"c"}}}
    };

    REQUIRE(r.hypothesis_id  == "H_mr_eurusd_1d");
    REQUIRE(r.test_name      == "adf");
    REQUIRE(r.p_value        == 0.012);
    REQUIRE(r.test_statistic == -3.45);
    REQUIRE(r.effect_size    == 0.31);
    REQUIRE(r.is_significant);
    REQUIRE(r.summary        == "ADF rejects unit-root at 5%");

    REQUIRE(r.parameters.size() == 3);
    REQUIRE(std::get<double>     (r.parameters.at("alpha")) == 0.05);
    REQUIRE(std::get<long long>  (r.parameters.at("lags"))  == 5LL);
    REQUIRE(std::get<std::string>(r.parameters.at("trend")) == "c");
}

TEST_CASE("HypothesisResult round-trips designated-init mixed parameters", "[stats][result][regression]") {
    HypothesisResult r{
        .hypothesis_id  = "H42",
        .test_name      = "hurst_rs",
        .p_value        = 0.014,
        .test_statistic = -3.21,
        .effect_size    = 0.18,
        .is_significant = true,
        .summary        = "Hurst < 0.5 → mean-reverting regime",
        .parameters     = {
            {"alpha",   0.05},
            {"min_lag", 4LL},
            {"trend",   std::string{"nc"}},
            {"robust",  true},
        },
    };

    REQUIRE(r.hypothesis_id  == "H42");
    REQUIRE(r.test_name      == "hurst_rs");
    REQUIRE(r.p_value        == 0.014);
    REQUIRE(r.test_statistic == -3.21);
    REQUIRE(r.effect_size    == 0.18);
    REQUIRE(r.is_significant);
    REQUIRE(r.summary        == "Hurst < 0.5 → mean-reverting regime");

    REQUIRE(r.parameters.size() == 4);
    REQUIRE(std::get<double>     (r.parameters.at("alpha"))   == 0.05);
    REQUIRE(std::get<long long>  (r.parameters.at("min_lag")) == 4LL);
    REQUIRE(std::get<std::string>(r.parameters.at("trend"))   == "nc");
    REQUIRE(std::get<bool>       (r.parameters.at("robust")));
}

TEST_CASE("HypothesisResult is usable as a vector element", "[stats][result][regression]") {
    std::vector<HypothesisResult> rs;
    rs.push_back({.hypothesis_id = "H1", .test_name = "hurst_rs", .is_significant = false});
    rs.push_back({.hypothesis_id = "H2", .test_name = "garch11",  .is_significant = true});

    REQUIRE(rs.size() == 2);
    REQUIRE(rs[0].test_name == "hurst_rs");
    REQUIRE(rs[0].is_significant == false);
    REQUIRE(rs[1].test_name == "garch11");
    REQUIRE(rs[1].is_significant);
    // Defaulted fields stay at fail-safe values even when omitted from designated init.
    REQUIRE(rs[0].p_value == 1.0);
    REQUIRE(rs[1].p_value == 1.0);
}

TEST_CASE("HypothesisParameterValue carries all four scalar types", "[stats][result][regression]") {
    HypothesisParameterValue d  = 1.5;
    HypothesisParameterValue i  = 42LL;
    HypothesisParameterValue b  = true;
    HypothesisParameterValue s  = std::string{"hello"};

    REQUIRE(std::get<double>     (d) == 1.5);
    REQUIRE(std::get<long long>  (i) == 42LL);
    REQUIRE(std::get<bool>       (b));
    REQUIRE(std::get<std::string>(s) == "hello");
}
