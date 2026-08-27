#include <catch2/catch_test_macros.hpp>

#include <stratforge/indicators/candlestick.hpp>
#include <stratforge/indicators/talib_compat.hpp>

#include "test_helpers.hpp"

#include <cstdlib>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

std::string load_fixture() {
    const std::string path = std::string(SF_SOURCE_DIR) +
        "/tools/golden_extract/output/talib_candles/talib_candles_reference.json";
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open TA-Lib fixture: " + path);
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
    if (position == std::string::npos || end == std::string::npos)
        throw std::runtime_error("invalid JSON array: " + key);
    std::vector<double> result;
    for (++position; position < end;) {
        while (position < end && (json[position] == ' ' || json[position] == '\n' ||
               json[position] == '\r' || json[position] == '\t' || json[position] == ','))
            ++position;
        if (position >= end) break;
        char* parsed_end = nullptr;
        const double value = std::strtod(json.c_str() + position, &parsed_end);
        if (parsed_end == json.c_str() + position)
            throw std::runtime_error("invalid number in JSON array: " + key);
        result.push_back(value);
        position = static_cast<std::size_t>(parsed_end - json.c_str());
    }
    return result;
}

std::vector<double> fixture_input(const std::string& json, const std::string& key) {
    const auto root_input = json.find("\n  \"input\": {");
    if (root_input == std::string::npos) throw std::runtime_error("missing root input");
    return parse_array_after(json, root_input, key);
}

std::vector<double> fixture_pattern(const std::string& json, const std::string& name) {
    auto position = json.find("\"name\": \"" + name + "\"");
    if (position == std::string::npos) throw std::runtime_error("missing pattern: " + name);
    // The extractor writes parameterized variants in ascending penetration
    // order. Select the TA-Lib default rather than the first variant.
    if (name == "CDLMATHOLD") position = json.find("\"penetration\": 0.5", position);
    else if (name == "CDLEVENINGDOJISTAR" || name == "CDLMORNINGDOJISTAR")
        position = json.find("\"penetration\": 0.3", position);
    return parse_array_after(json, position, "values");
}

template <typename IndicatorType>
void check_pattern(const std::string& name, const std::vector<double>& open,
                   const std::vector<double>& high, const std::vector<double>& low,
                   const std::vector<double>& close, const std::string& json) {
    auto o = stratforge::test::make_line(open);
    auto h = stratforge::test::make_line(high);
    auto l = stratforge::test::make_line(low);
    auto c = stratforge::test::make_line(close);
    IndicatorType indicator(o, h, l, c);
    stratforge::test::run_indicator_ohlc(o, h, l, c, indicator);
    const auto expected = fixture_pattern(json, name);
    REQUIRE(indicator.line().size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("pattern=" << name << " bar=" << i);
        CHECK(indicator.line().data()[i] == expected[i]);
    }
}

template <typename IndicatorType, typename... Args>
std::vector<double> run_pattern(const std::array<std::vector<double>, 4>& values,
                                Args&&... args) {
    auto o = stratforge::test::make_line(values[0]);
    auto h = stratforge::test::make_line(values[1]);
    auto l = stratforge::test::make_line(values[2]);
    auto c = stratforge::test::make_line(values[3]);
    IndicatorType indicator(o, h, l, c, std::forward<Args>(args)...);
    stratforge::test::run_indicator_ohlc(o, h, l, c, indicator);
    return indicator.line().data();
}

#define SF011_MISSING_PATTERNS(X) \
    X(CDLTwoCrows, CDL2CROWS) X(CDLThreeLineStrike, CDL3LINESTRIKE) \
    X(CDLThreeStarsInSouth, CDL3STARSINSOUTH) X(CDLAdvanceBlock, CDLADVANCEBLOCK) \
    X(CDLBreakaway, CDLBREAKAWAY) X(CDLClosingMarubozu, CDLCLOSINGMARUBOZU) \
    X(CDLConcealingBabySwallow, CDLCONCEALBABYSWALL) X(CDLDojiStar, CDLDOJISTAR) \
    X(CDLDragonflyDoji, CDLDRAGONFLYDOJI) X(CDLEveningDojiStar, CDLEVENINGDOJISTAR) \
    X(CDLGapSideSideWhite, CDLGAPSIDESIDEWHITE) X(CDLGravestoneDoji, CDLGRAVESTONEDOJI) \
    X(CDLHaramiCross, CDLHARAMICROSS) X(CDLHighWave, CDLHIGHWAVE) \
    X(CDLHikkake, CDLHIKKAKE) X(CDLModifiedHikkake, CDLHIKKAKEMOD) \
    X(CDLIdenticalThreeCrows, CDLIDENTICAL3CROWS) X(CDLInNeck, CDLINNECK) \
    X(CDLInvertedHammer, CDLINVERTEDHAMMER) X(CDLKickingByLength, CDLKICKINGBYLENGTH) \
    X(CDLLadderBottom, CDLLADDERBOTTOM) X(CDLLongLeggedDoji, CDLLONGLEGGEDDOJI) \
    X(CDLLongLine, CDLLONGLINE) X(CDLMatHold, CDLMATHOLD) \
    X(CDLMorningDojiStar, CDLMORNINGDOJISTAR) X(CDLOnNeck, CDLONNECK) \
    X(CDLRickshawMan, CDLRICKSHAWMAN) X(CDLRiseFallThreeMethods, CDLRISEFALL3METHODS) \
    X(CDLSeparatingLines, CDLSEPARATINGLINES) X(CDLShortLine, CDLSHORTLINE) \
    X(CDLStalledPattern, CDLSTALLEDPATTERN) X(CDLStickSandwich, CDLSTICKSANDWICH) \
    X(CDLTakuri, CDLTAKURI) X(CDLThrusting, CDLTHRUSTING) X(CDLTristar, CDLTRISTAR) \
    X(CDLUniqueThreeRiver, CDLUNIQUE3RIVER) X(CDLXSideGapThreeMethods, CDLXSIDEGAP3METHODS)

} // namespace

TEST_CASE("SF011 remaining 37 candlestick indicators match pinned TA-Lib 0.6.4",
          "[sf011][phase7][candlestick][oracle]") {
    const auto json = load_fixture();
    const auto open = fixture_input(json, "open");
    const auto high = fixture_input(json, "high");
    const auto low = fixture_input(json, "low");
    const auto close = fixture_input(json, "close");
    REQUIRE(open.size() == 360);
#define CHECK_PATTERN(Type, Oracle) \
    check_pattern<stratforge::Type>(#Oracle, open, high, low, close, json);
    SF011_MISSING_PATTERNS(CHECK_PATTERN)
#undef CHECK_PATTERN
}

TEST_CASE("SF011 all 37 exact TA-Lib aliases are stable public mappings",
          "[sf011][phase7][candlestick][aliases]") {
#define CHECK_ALIAS(Type, Oracle) \
    STATIC_REQUIRE(std::is_same_v<stratforge::Type, stratforge::talib::Oracle>);
    SF011_MISSING_PATTERNS(CHECK_ALIAS)
#undef CHECK_ALIAS
}

TEST_CASE("SF011 exceptional and long-sequence lookbacks match TA-Lib",
          "[sf011][phase7][candlestick][lookback]") {
    stratforge::Line<double> line;
    CHECK(stratforge::CDLHikkake(line, line, line, line).minimum_period() == 6);
    CHECK(stratforge::CDLModifiedHikkake(line, line, line, line).minimum_period() == 11);
    CHECK(stratforge::CDLBreakaway(line, line, line, line).minimum_period() == 15);
    CHECK(stratforge::CDLMatHold(line, line, line, line).minimum_period() == 15);
    CHECK(stratforge::CDLXSideGapThreeMethods(line, line, line, line).minimum_period() == 3);
}

TEST_CASE("SF011 Hikkake detection and confirmation preserve exceptional encoding",
          "[sf011][phase7][candlestick][edge]") {
    const std::array<std::vector<double>, 4> values{{
        {10, 10, 10, 10, 10, 10, 10, 9.5, 10.5},
        {11, 11, 11, 11, 11, 12, 11, 10.5, 12},
        {9, 9, 9, 9, 9, 8, 9, 8.5, 10},
        {10.5, 10.5, 10.5, 10.5, 10.5, 11, 10, 9, 11.5},
    }};
    const auto output = run_pattern<stratforge::CDLHikkake>(values);
    REQUIRE(output.size() == values[0].size());
    CHECK(output[7] == 100.0);
    CHECK(output[8] == 200.0);
}

TEST_CASE("SF011 new one-bar patterns honor per-indicator candle settings",
          "[sf011][phase7][candlestick][edge][settings]") {
    std::array<std::vector<double>, 4> values{{
        std::vector<double>(10, 10.0), std::vector<double>(10, 11.0),
        std::vector<double>(10, 9.0), std::vector<double>(10, 10.5),
    }};
    values[0].push_back(11.9);
    values[1].push_back(12.0);
    values[2].push_back(8.0);
    values[3].push_back(12.0);

    CHECK(run_pattern<stratforge::CDLDragonflyDoji>(values).back() == 100.0);
    stratforge::candlestick::Settings strict;
    strict.set(stratforge::candlestick::SettingType::BodyDoji,
               {stratforge::candlestick::RangeType::HighLow, 10, 0.01});
    CHECK(run_pattern<stratforge::CDLDragonflyDoji>(values, strict).back() == 0.0);
}

TEST_CASE("SF011 new penetration patterns reject invalid parameters",
          "[sf011][phase7][candlestick][edge][penetration]") {
    stratforge::Line<double> line;
    CHECK_THROWS_AS(stratforge::CDLEveningDojiStar(line, line, line, line, -0.1),
                    std::invalid_argument);
    CHECK_THROWS_AS(stratforge::CDLMatHold(line, line, line, line, -0.1),
                    std::invalid_argument);
    CHECK_THROWS_AS(stratforge::CDLMorningDojiStar(line, line, line, line, -0.1),
                    std::invalid_argument);
}

TEST_CASE("SF011 new penetration patterns match pinned boundary probes",
          "[sf011][phase7][candlestick][oracle][penetration]") {
    const auto prefixed = [](std::initializer_list<double> open,
                             std::initializer_list<double> high,
                             std::initializer_list<double> low,
                             std::initializer_list<double> close) {
        std::array<std::vector<double>, 4> values{
            std::vector<double>(10, 10.0), std::vector<double>(10, 10.6),
            std::vector<double>(10, 9.4), std::vector<double>(10, 10.5),
        };
        values[0].insert(values[0].end(), open);
        values[1].insert(values[1].end(), high);
        values[2].insert(values[2].end(), low);
        values[3].insert(values[3].end(), close);
        return values;
    };
    const auto morning = prefixed(
        {12.0, 9.4, 9.6}, {12.2, 9.5, 11.2},
        {9.8, 9.3, 9.5}, {10.0, 9.4, 11.0});
    const auto evening = prefixed(
        {10.0, 12.6, 12.4}, {12.2, 12.7, 12.5},
        {9.8, 12.5, 10.8}, {12.0, 12.6, 11.0});
    const auto mat_hold = prefixed(
        {10.0, 12.8, 12.2, 12.0, 12.1},
        {12.2, 13.0, 12.3, 12.1, 13.3},
        {9.8, 12.4, 11.8, 11.5, 12.0},
        {12.0, 12.5, 11.9, 11.6, 13.2});
    constexpr std::array<double, 5> penetration{0.0, 0.1, 0.3, 0.5, 0.75};
    constexpr std::array<double, 5> doji_star{100.0, 100.0, 100.0, 0.0, 0.0};
    constexpr std::array<double, 5> mat_hold_expected{0.0, 0.0, 100.0, 100.0, 100.0};
    for (std::size_t i = 0; i < penetration.size(); ++i) {
        INFO("penetration=" << penetration[i]);
        CHECK(run_pattern<stratforge::CDLMorningDojiStar>(morning, penetration[i]).back() ==
              doji_star[i]);
        CHECK(run_pattern<stratforge::CDLEveningDojiStar>(evening, penetration[i]).back() ==
              -doji_star[i]);
        CHECK(run_pattern<stratforge::CDLMatHold>(mat_hold, penetration[i]).back() ==
              mat_hold_expected[i]);
    }
}

#undef SF011_MISSING_PATTERNS
