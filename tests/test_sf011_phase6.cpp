#include <catch2/catch_test_macros.hpp>

#include <stratforge/indicators/candlestick.hpp>

#include "test_helpers.hpp"

#include <cstdlib>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
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
    const auto position = json.find("\"name\": \"" + name + "\"");
    if (position == std::string::npos) throw std::runtime_error("missing pattern: " + name);
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

template <typename IndicatorType>
double final_signal(const std::array<std::vector<double>, 4>& input,
                    double penetration) {
    auto o = stratforge::test::make_line(input[0]);
    auto h = stratforge::test::make_line(input[1]);
    auto l = stratforge::test::make_line(input[2]);
    auto c = stratforge::test::make_line(input[3]);
    IndicatorType indicator(o, h, l, c, penetration);
    stratforge::test::run_indicator_ohlc(o, h, l, c, indicator);
    return indicator.line().data().back();
}

std::array<std::vector<double>, 4> with_tail(
    std::initializer_list<double> open, std::initializer_list<double> high,
    std::initializer_list<double> low, std::initializer_list<double> close) {
    std::array<std::vector<double>, 4> result{
        std::vector<double>(10, 10.0), std::vector<double>(10, 10.6),
        std::vector<double>(10, 9.4), std::vector<double>(10, 10.5),
    };
    result[0].insert(result[0].end(), open);
    result[1].insert(result[1].end(), high);
    result[2].insert(result[2].end(), low);
    result[3].insert(result[3].end(), close);
    return result;
}

} // namespace

TEST_CASE("SF011 existing 24 candlestick indicators match pinned TA-Lib 0.6.4",
          "[sf011][phase6][candlestick][oracle]") {
    const auto json = load_fixture();
    const auto open = fixture_input(json, "open");
    const auto high = fixture_input(json, "high");
    const auto low = fixture_input(json, "low");
    const auto close = fixture_input(json, "close");
    REQUIRE(open.size() == 360);

    check_pattern<stratforge::CDLDoji>("CDLDOJI", open, high, low, close, json);
    check_pattern<stratforge::CDLHammer>("CDLHAMMER", open, high, low, close, json);
    check_pattern<stratforge::CDLEngulfing>("CDLENGULFING", open, high, low, close, json);
    check_pattern<stratforge::CDLMorningStar>("CDLMORNINGSTAR", open, high, low, close, json);
    check_pattern<stratforge::CDLEveningStar>("CDLEVENINGSTAR", open, high, low, close, json);
    check_pattern<stratforge::CDLHarami>("CDLHARAMI", open, high, low, close, json);
    check_pattern<stratforge::CDLShootingStar>("CDLSHOOTINGSTAR", open, high, low, close, json);
    check_pattern<stratforge::CDLHangingMan>("CDLHANGINGMAN", open, high, low, close, json);
    check_pattern<stratforge::CDLMarubozu>("CDLMARUBOZU", open, high, low, close, json);
    check_pattern<stratforge::CDLSpinningTop>("CDLSPINNINGTOP", open, high, low, close, json);
    check_pattern<stratforge::CDLThreeWhiteSoldiers>("CDL3WHITESOLDIERS", open, high, low, close, json);
    check_pattern<stratforge::CDLThreeBlackCrows>("CDL3BLACKCROWS", open, high, low, close, json);
    check_pattern<stratforge::CDLPiercingLine>("CDLPIERCING", open, high, low, close, json);
    check_pattern<stratforge::CDLDarkCloudCover>("CDLDARKCLOUDCOVER", open, high, low, close, json);
    check_pattern<stratforge::CDLAbandonedBaby>("CDLABANDONEDBABY", open, high, low, close, json);
    check_pattern<stratforge::CDLThreeInside>("CDL3INSIDE", open, high, low, close, json);
    check_pattern<stratforge::CDLThreeOutside>("CDL3OUTSIDE", open, high, low, close, json);
    check_pattern<stratforge::CDLKicking>("CDLKICKING", open, high, low, close, json);
    check_pattern<stratforge::CDLBeltHold>("CDLBELTHOLD", open, high, low, close, json);
    check_pattern<stratforge::CDLCounterAttack>("CDLCOUNTERATTACK", open, high, low, close, json);
    check_pattern<stratforge::CDLHomingPigeon>("CDLHOMINGPIGEON", open, high, low, close, json);
    check_pattern<stratforge::CDLMatchingLow>("CDLMATCHINGLOW", open, high, low, close, json);
    check_pattern<stratforge::CDLTasukiGap>("CDLTASUKIGAP", open, high, low, close, json);
    check_pattern<stratforge::CDLUpsideGap>("CDLUPSIDEGAP2CROWS", open, high, low, close, json);
}

TEST_CASE("SF011 candlestick lookbacks translate TA-Lib output starts to minimum periods",
          "[sf011][phase6][candlestick][lookback]") {
    stratforge::Line<double> line;
    CHECK(stratforge::CDLDoji(line, line, line, line).minimum_period() == 11);
    CHECK(stratforge::CDLHammer(line, line, line, line).minimum_period() == 12);
    CHECK(stratforge::CDLEngulfing(line, line, line, line).minimum_period() == 3);
    CHECK(stratforge::CDLMorningStar(line, line, line, line).minimum_period() == 13);
    CHECK(stratforge::CDLThreeBlackCrows(line, line, line, line).minimum_period() == 14);
    CHECK(stratforge::CDLThreeOutside(line, line, line, line).minimum_period() == 4);
}

TEST_CASE("SF011 penetration boundaries match pinned TA-Lib 0.6.4",
          "[sf011][phase6][candlestick][oracle][penetration]") {
    // These sequences and all five outputs are frozen under the fixture's
    // penetration_boundary_probes key by extract_talib_candles.py.
    const auto morning = with_tail(
        {12.0, 9.5, 9.6}, {12.2, 9.7, 11.2},
        {9.8, 9.3, 9.5}, {10.0, 9.4, 11.0});
    const auto evening = with_tail(
        {10.0, 12.5, 12.4}, {12.2, 12.7, 12.5},
        {9.8, 12.3, 10.8}, {12.0, 12.6, 11.0});
    const auto dark_cloud = with_tail(
        {10.0, 12.5}, {12.1, 12.6}, {9.9, 11.4}, {12.0, 11.5});
    const auto abandoned = with_tail(
        {12.0, 9.0, 9.5}, {12.2, 9.1, 11.2},
        {9.8, 8.9, 9.4}, {10.0, 9.0, 11.0});
    constexpr std::array<double, 5> penetration{0.0, 0.1, 0.3, 0.5, 0.75};
    constexpr std::array<double, 5> star_expected{100.0, 100.0, 100.0, 0.0, 0.0};
    constexpr std::array<double, 5> dark_expected{-100.0, -100.0, 0.0, 0.0, 0.0};

    for (std::size_t i = 0; i < penetration.size(); ++i) {
        INFO("penetration=" << penetration[i]);
        CHECK(final_signal<stratforge::CDLMorningStar>(morning, penetration[i]) ==
              star_expected[i]);
        CHECK(final_signal<stratforge::CDLEveningStar>(evening, penetration[i]) ==
              -star_expected[i]);
        CHECK(final_signal<stratforge::CDLDarkCloudCover>(dark_cloud, penetration[i]) ==
              dark_expected[i]);
        CHECK(final_signal<stratforge::CDLAbandonedBaby>(abandoned, penetration[i]) ==
              star_expected[i]);
    }
}
