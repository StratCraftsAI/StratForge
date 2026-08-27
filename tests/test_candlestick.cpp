#include <catch2/catch_test_macros.hpp>

#include <stratforge/indicators/candlestick.hpp>

#include "test_helpers.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

template <typename Indicator, typename... Args>
std::vector<double> run(const std::vector<double>& open,
                        const std::vector<double>& high,
                        const std::vector<double>& low,
                        const std::vector<double>& close,
                        Args&&... args) {
    auto o = stratforge::test::make_line(open);
    auto h = stratforge::test::make_line(high);
    auto l = stratforge::test::make_line(low);
    auto c = stratforge::test::make_line(close);
    Indicator indicator(o, h, l, c, std::forward<Args>(args)...);
    stratforge::test::run_indicator_ohlc(o, h, l, c, indicator);
    return indicator.line().data();
}

} // namespace

TEST_CASE("TA-Lib engulfing preserves strict and boundary signal magnitudes",
          "[candlestick][engulfing][boundary]") {
    const std::vector<double> high(4, 14.0);
    const std::vector<double> low(4, 8.0);

    const auto strict = run<stratforge::CDLEngulfing>(
        {10.0, 12.0, 9.0, 13.0}, high, low, {11.0, 10.0, 12.5, 8.5});
    CHECK(strict[2] == 100.0);
    CHECK(strict[3] == -100.0);

    const auto boundary = run<stratforge::CDLEngulfing>(
        {10.0, 12.0, 9.5}, {13.0, 13.0, 13.0}, {8.0, 8.0, 8.0},
        {11.0, 10.0, 12.0});
    CHECK(boundary[2] == 80.0);
}

TEST_CASE("TA-Lib doji setting controls warmup and rolling threshold",
          "[candlestick][doji][settings]") {
    stratforge::candlestick::Settings settings;
    settings.set(stratforge::candlestick::SettingType::BodyDoji,
                 {stratforge::candlestick::RangeType::HighLow, 3, 0.1});
    const auto values = run<stratforge::CDLDoji>(
        std::vector<double>(6, 10.0),
        {11.0, 12.0, 13.0, 14.0, 11.0, 12.0},
        {9.0, 8.0, 7.0, 6.0, 9.0, 8.0},
        {10.5, 10.3, 10.1, 10.4, 10.2, 10.9}, settings);
    CHECK(values == std::vector<double>{0.0, 0.0, 0.0, 0.0, 100.0, 0.0});

    stratforge::Line<double> empty;
    stratforge::CDLDoji indicator(empty, empty, empty, empty, settings);
    CHECK(indicator.minimum_period() == 4);
}

TEST_CASE("TA-Lib penetration parameters reject invalid values",
          "[candlestick][penetration][robustness]") {
    stratforge::Line<double> line;
    CHECK_THROWS_AS(stratforge::CDLMorningStar(line, line, line, line, -0.1),
                    std::invalid_argument);
    CHECK_THROWS_AS(stratforge::CDLEveningStar(
        line, line, line, line, std::numeric_limits<double>::infinity()),
        std::invalid_argument);
    CHECK_THROWS_AS(stratforge::CDLDarkCloudCover(
        line, line, line, line, std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument);
    CHECK_NOTHROW(stratforge::CDLAbandonedBaby(line, line, line, line, 0.0));
}

TEST_CASE("TA-Lib candlestick minimum periods match default lookbacks",
          "[candlestick][lookback]") {
    stratforge::Line<double> line;
    const std::array<std::size_t, 24> actual{
        stratforge::CDLDoji(line, line, line, line).minimum_period(),
        stratforge::CDLHammer(line, line, line, line).minimum_period(),
        stratforge::CDLEngulfing(line, line, line, line).minimum_period(),
        stratforge::CDLMorningStar(line, line, line, line).minimum_period(),
        stratforge::CDLEveningStar(line, line, line, line).minimum_period(),
        stratforge::CDLHarami(line, line, line, line).minimum_period(),
        stratforge::CDLShootingStar(line, line, line, line).minimum_period(),
        stratforge::CDLHangingMan(line, line, line, line).minimum_period(),
        stratforge::CDLMarubozu(line, line, line, line).minimum_period(),
        stratforge::CDLSpinningTop(line, line, line, line).minimum_period(),
        stratforge::CDLThreeWhiteSoldiers(line, line, line, line).minimum_period(),
        stratforge::CDLThreeBlackCrows(line, line, line, line).minimum_period(),
        stratforge::CDLPiercingLine(line, line, line, line).minimum_period(),
        stratforge::CDLDarkCloudCover(line, line, line, line).minimum_period(),
        stratforge::CDLAbandonedBaby(line, line, line, line).minimum_period(),
        stratforge::CDLThreeInside(line, line, line, line).minimum_period(),
        stratforge::CDLThreeOutside(line, line, line, line).minimum_period(),
        stratforge::CDLKicking(line, line, line, line).minimum_period(),
        stratforge::CDLBeltHold(line, line, line, line).minimum_period(),
        stratforge::CDLCounterAttack(line, line, line, line).minimum_period(),
        stratforge::CDLHomingPigeon(line, line, line, line).minimum_period(),
        stratforge::CDLMatchingLow(line, line, line, line).minimum_period(),
        stratforge::CDLTasukiGap(line, line, line, line).minimum_period(),
        stratforge::CDLUpsideGap(line, line, line, line).minimum_period(),
    };
    const std::array<std::size_t, 24> expected{
        11, 12, 3, 13, 13, 12, 12, 12, 11, 11, 13, 14,
        12, 12, 13, 13, 4, 12, 11, 12, 12, 7, 8, 13,
    };
    CHECK(actual == expected);
}
