#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/stratforge.hpp>

#include "test_helpers.hpp"

#include <array>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

template <typename IndicatorType>
double evaluate(double open, double high, double low, double close) {
    auto o = stratforge::test::make_line({open});
    auto h = stratforge::test::make_line({high});
    auto l = stratforge::test::make_line({low});
    auto c = stratforge::test::make_line({close});
    IndicatorType indicator(o, h, l, c);
    indicator.next();
    REQUIRE(indicator.minimum_period() == 1);
    return indicator.line().data().front();
}

std::string load_qlib_fixture() {
    const std::string path = std::string(SF_SOURCE_DIR) +
        "/tools/golden_extract/output/alpha158/alpha158_reference.json";
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open Qlib fixture: " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::vector<double> parse_array_after(const std::string& json, std::size_t position,
                                      const std::string& key) {
    position = json.find('"' + key + '"', position);
    if (position == std::string::npos) throw std::runtime_error("missing JSON key: " + key);
    position = json.find('[', position);
    const auto end = json.find(']', position);
    if (position == std::string::npos || end == std::string::npos) {
        throw std::runtime_error("invalid JSON array: " + key);
    }

    std::vector<double> result;
    ++position;
    while (position < end) {
        while (position < end && (json[position] == ' ' || json[position] == '\n' ||
                                  json[position] == '\r' || json[position] == '\t' ||
                                  json[position] == ',')) ++position;
        if (position >= end) break;
        if (json.compare(position, 4, "null") == 0) {
            result.push_back(std::numeric_limits<double>::quiet_NaN());
            position += 4;
            continue;
        }
        char* parsed_end = nullptr;
        const double value = std::strtod(json.c_str() + position, &parsed_end);
        if (parsed_end == json.c_str() + position) {
            throw std::runtime_error("invalid number in JSON array: " + key);
        }
        result.push_back(value);
        position = static_cast<std::size_t>(parsed_end - json.c_str());
    }
    return result;
}

std::vector<double> fixture_input(const std::string& json, const std::string& name) {
    return parse_array_after(json, json.find("\"input\""), name);
}

std::vector<double> fixture_factor(const std::string& json, const std::string& name) {
    const auto factor = json.find("\"name\": \"" + name + "\"");
    if (factor == std::string::npos) throw std::runtime_error("missing Qlib factor: " + name);
    return parse_array_after(json, factor, "values");
}

template <typename IndicatorType>
std::vector<double> evaluate_series(const std::vector<double>& open,
                                    const std::vector<double>& high,
                                    const std::vector<double>& low,
                                    const std::vector<double>& close) {
    auto o = stratforge::test::make_line(open);
    auto h = stratforge::test::make_line(high);
    auto l = stratforge::test::make_line(low);
    auto c = stratforge::test::make_line(close);
    IndicatorType indicator(o, h, l, c);
    stratforge::test::run_indicator_ohlc(o, h, l, c, indicator);
    return indicator.line().data();
}

template <typename IndicatorType>
void check_qlib_factor(const std::string& name, const std::vector<double>& open,
                       const std::vector<double>& high, const std::vector<double>& low,
                       const std::vector<double>& close, const std::string& json) {
    const auto actual = evaluate_series<IndicatorType>(open, high, low, close);
    const auto expected = fixture_factor(json, name);
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const bool supported_input = std::isfinite(open[i]) && std::isfinite(high[i]) &&
            std::isfinite(low[i]) && std::isfinite(close[i]) && open[i] != 0.0 &&
            high[i] >= low[i] && high[i] >= std::max(open[i], close[i]) &&
            low[i] <= std::min(open[i], close[i]);
        if (!supported_input) {
            CHECK(std::isnan(actual[i]));
            continue;
        }
        INFO("factor=" << name << " bar=" << i);
        REQUIRE(std::isfinite(expected[i]));
        CHECK_THAT(actual[i], WithinAbs(expected[i], 2e-7));
    }
}

} // namespace

TEST_CASE("Qlib K-bar public aliases are independently constructible", "[sf011][qlib][api]") {
    static_assert(std::is_constructible_v<stratforge::KMID,
        const stratforge::Line<double>&, const stratforge::Line<double>&,
        const stratforge::Line<double>&, const stratforge::Line<double>&>);
    static_assert(!std::is_same_v<stratforge::KMID, stratforge::KLEN>);
    static_assert(!std::is_same_v<stratforge::KUP, stratforge::KUP2>);
    static_assert(!std::is_same_v<stratforge::KLOW, stratforge::KLOW2>);
}

TEST_CASE("Qlib K-bar indicators implement the pinned formulas", "[sf011][qlib][unit]") {
    constexpr double o = 10.0;
    constexpr double h = 13.0;
    constexpr double l = 8.0;
    constexpr double c = 12.0;
    constexpr double range = h - l + 1e-12;

    CHECK_THAT(evaluate<stratforge::KMID>(o, h, l, c), WithinAbs(0.2, 1e-14));
    CHECK_THAT(evaluate<stratforge::KLEN>(o, h, l, c), WithinAbs(0.5, 1e-14));
    CHECK_THAT(evaluate<stratforge::KMID2>(o, h, l, c), WithinAbs(2.0 / range, 1e-14));
    CHECK_THAT(evaluate<stratforge::KUP>(o, h, l, c), WithinAbs(0.1, 1e-14));
    CHECK_THAT(evaluate<stratforge::KUP2>(o, h, l, c), WithinAbs(1.0 / range, 1e-14));
    CHECK_THAT(evaluate<stratforge::KLOW>(o, h, l, c), WithinAbs(0.2, 1e-14));
    CHECK_THAT(evaluate<stratforge::KLOW2>(o, h, l, c), WithinAbs(2.0 / range, 1e-14));
    CHECK_THAT(evaluate<stratforge::KSFT>(o, h, l, c), WithinAbs(0.3, 1e-14));
    CHECK_THAT(evaluate<stratforge::KSFT2>(o, h, l, c), WithinAbs(3.0 / range, 1e-14));
}

TEST_CASE("Qlib K-bar indicators reject invalid OHLC consistently", "[sf011][qlib][robustness]") {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(std::isnan(evaluate<stratforge::KMID>(0.0, 2.0, -1.0, 1.0)));
    CHECK(std::isnan(evaluate<stratforge::KUP2>(10.0, 9.0, 11.0, 10.5)));
    CHECK(std::isnan(evaluate<stratforge::KLOW>(10.0, 11.0, 9.0, nan)));
}

TEST_CASE("Qlib K-bar features are scale invariant on valid bars", "[sf011][qlib][property]") {
    const std::array<double, 5> scales{0.01, 0.5, 1.0, 17.0, 1000.0};
    for (double scale : scales) {
        INFO("scale=" << scale);
        CHECK_THAT(evaluate<stratforge::KMID>(10 * scale, 13 * scale, 8 * scale, 12 * scale),
                   WithinAbs(0.2, 1e-12));
        CHECK_THAT(evaluate<stratforge::KUP>(10 * scale, 13 * scale, 8 * scale, 12 * scale),
                   WithinAbs(0.1, 1e-12));
        CHECK_THAT(evaluate<stratforge::KLOW>(10 * scale, 13 * scale, 8 * scale, 12 * scale),
                   WithinAbs(0.2, 1e-12));
        CHECK_THAT(evaluate<stratforge::KSFT>(10 * scale, 13 * scale, 8 * scale, 12 * scale),
                   WithinAbs(0.3, 1e-12));
    }
}

TEST_CASE("Qlib range-normalized K-bar fields preserve flat-bar epsilon semantics",
          "[sf011][qlib][boundary]") {
    CHECK(evaluate<stratforge::KMID2>(10.0, 10.0, 10.0, 10.0) == 0.0);
    CHECK(evaluate<stratforge::KUP2>(10.0, 10.0, 10.0, 10.0) == 0.0);
    CHECK(evaluate<stratforge::KLOW2>(10.0, 10.0, 10.0, 10.0) == 0.0);
    CHECK(evaluate<stratforge::KSFT2>(10.0, 10.0, 10.0, 10.0) == 0.0);
}

TEST_CASE("all nine K-bar indicators match the pinned Qlib engine fixture",
          "[sf011][qlib][golden]") {
    const auto json = load_qlib_fixture();
    const auto open = fixture_input(json, "open");
    const auto high = fixture_input(json, "high");
    const auto low = fixture_input(json, "low");
    const auto close = fixture_input(json, "close");
    REQUIRE(open.size() == 200);

    check_qlib_factor<stratforge::KMID>("KMID", open, high, low, close, json);
    check_qlib_factor<stratforge::KLEN>("KLEN", open, high, low, close, json);
    check_qlib_factor<stratforge::KMID2>("KMID2", open, high, low, close, json);
    check_qlib_factor<stratforge::KUP>("KUP", open, high, low, close, json);
    check_qlib_factor<stratforge::KUP2>("KUP2", open, high, low, close, json);
    check_qlib_factor<stratforge::KLOW>("KLOW", open, high, low, close, json);
    check_qlib_factor<stratforge::KLOW2>("KLOW2", open, high, low, close, json);
    check_qlib_factor<stratforge::KSFT>("KSFT", open, high, low, close, json);
    check_qlib_factor<stratforge::KSFT2>("KSFT2", open, high, low, close, json);
}
