// SPDX-License-Identifier: MIT
//
// tests/test_stats_result_json.cpp — canonical JSON serialisation of
// stratforge::stats::HypothesisResult.
//
// The wire format is owned by StratForge so the StratCraft Round 3 host
// binary does not redeclare it. Keys, key order, and
// number formatting are load-bearing for downstream parsers.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/stats/result.hpp>
#include <stratforge/stats/result_json.hpp>

#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <vector>

using stratforge::stats::HypothesisResult;
using stratforge::stats::to_json;

TEST_CASE("to_json emits all 8 keys in declared order with fail-safe defaults",
          "[stats][result][regression]") {
    HypothesisResult r{};
    r.hypothesis_id = "H0";
    r.test_name     = "noop";

    const std::string s = to_json(r);

    // Exact byte match — keys, order, defaults.
    const std::string expected =
        R"({"hypothesis_id":"H0","test_name":"noop","p_value":1,"test_statistic":0,)"
        R"("effect_size":0,"is_significant":false,"summary":"","parameters":{}})";
    REQUIRE(s == expected);
}

TEST_CASE("to_json serialises the full 8-field row with mixed parameters",
          "[stats][result][regression]") {
    HypothesisResult r{
        "H_mr_eurusd_1d", "adf",
        0.012, -3.45, 0.31, true,
        "ADF rejects unit-root at 5%",
        {{"alpha", 0.05}, {"lags", 5LL}, {"robust", true}, {"trend", std::string{"c"}}}
    };

    const std::string s = to_json(r);

    INFO("json=" << s);

    // hypothesis_id / test_name / scalars. Doubles use %.17g (exact round-trip
    // form), so 0.012 / 0.31 / 0.05 render as their full IEEE-754 decimal,
    // -3.45 as -3.4500000000000002, etc.
    REQUIRE(s.find(R"("hypothesis_id":"H_mr_eurusd_1d")")  != std::string::npos);
    REQUIRE(s.find(R"("test_name":"adf")")                 != std::string::npos);
    REQUIRE(s.find(R"("p_value":0.012)")                   != std::string::npos);
    REQUIRE(s.find(R"("test_statistic":-3.4500000000000002)") != std::string::npos);
    REQUIRE(s.find(R"("effect_size":0.31)")                != std::string::npos);
    REQUIRE(s.find(R"("is_significant":true)")             != std::string::npos);
    REQUIRE(s.find(R"("summary":"ADF rejects unit-root at 5%")") != std::string::npos);

    // Parameters: std::map iterates lexicographically — alpha, lags, robust, trend.
    REQUIRE(s.find(R"("parameters":{"alpha":0.050000000000000003,"lags":5,"robust":true,"trend":"c"}})")
            != std::string::npos);

    // Field order is the declared 8-field order.
    REQUIRE(s.find("\"hypothesis_id\"")  < s.find("\"test_name\""));
    REQUIRE(s.find("\"test_name\"")      < s.find("\"p_value\""));
    REQUIRE(s.find("\"p_value\"")        < s.find("\"test_statistic\""));
    REQUIRE(s.find("\"test_statistic\"") < s.find("\"effect_size\""));
    REQUIRE(s.find("\"effect_size\"")    < s.find("\"is_significant\""));
    REQUIRE(s.find("\"is_significant\"") < s.find("\"summary\""));
    REQUIRE(s.find("\"summary\"")        < s.find("\"parameters\""));
}

TEST_CASE("to_json escapes strings and quotes/backslashes/control chars",
          "[stats][result][regression]") {
    HypothesisResult r{};
    r.hypothesis_id = R"(H "quoted" \ backslash)";
    r.test_name     = "noop";
    r.summary       = "line1\nline2\ttab";

    const std::string s = to_json(r);
    INFO("json=" << s);

    REQUIRE(s.find(R"("hypothesis_id":"H \"quoted\" \\ backslash")") != std::string::npos);
    REQUIRE(s.find(R"("summary":"line1\nline2\ttab")")               != std::string::npos);
}

TEST_CASE("to_json emits null for NaN / Inf doubles", "[stats][result][regression]") {
    HypothesisResult r{};
    r.hypothesis_id  = "Hnan";
    r.test_name      = "degenerate";
    r.p_value        = std::numeric_limits<double>::quiet_NaN();
    r.test_statistic = std::numeric_limits<double>::infinity();
    r.effect_size    = -std::numeric_limits<double>::infinity();

    const std::string s = to_json(r);
    INFO("json=" << s);

    REQUIRE(s.find(R"("p_value":null)")        != std::string::npos);
    REQUIRE(s.find(R"("test_statistic":null)") != std::string::npos);
    REQUIRE(s.find(R"("effect_size":null)")    != std::string::npos);
}

TEST_CASE("to_json over span produces a JSON array", "[stats][result][regression]") {
    std::vector<HypothesisResult> rs;
    rs.push_back({.hypothesis_id = "H1", .test_name = "a"});
    rs.push_back({.hypothesis_id = "H2", .test_name = "b"});

    const std::string empty = to_json(std::span<const HypothesisResult>{});
    REQUIRE(empty == "[]");

    const std::string s = to_json(std::span<const HypothesisResult>{rs});
    INFO("json=" << s);

    REQUIRE(s.front() == '[');
    REQUIRE(s.back()  == ']');
    REQUIRE(s.find(R"("hypothesis_id":"H1")") != std::string::npos);
    REQUIRE(s.find(R"("hypothesis_id":"H2")") != std::string::npos);
    REQUIRE(s.find(R"("hypothesis_id":"H1")") < s.find(R"("hypothesis_id":"H2")"));
    // Exactly one comma between the two objects.
    REQUIRE(s.find("},{") != std::string::npos);
}
