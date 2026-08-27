#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/indicators/candlestick_common.hpp>
#include <stratforge/indicators/talib_compat.hpp>
#include <stratforge/stratforge.hpp>

#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace candle = stratforge::candlestick;
using Catch::Approx;

TEST_CASE("SF011 Phase 5 freezes all TA-Lib 0.6.4 candle defaults",
          "[sf011][phase5][candlestick]") {
    static_assert(candle::setting_count == 11);
    constexpr std::array<candle::Setting, 11> oracle{{
        {candle::RangeType::RealBody, 10, 1.0},
        {candle::RangeType::RealBody, 10, 3.0},
        {candle::RangeType::RealBody, 10, 1.0},
        {candle::RangeType::HighLow, 10, 0.1},
        {candle::RangeType::RealBody, 0, 1.0},
        {candle::RangeType::RealBody, 0, 2.0},
        {candle::RangeType::Shadows, 10, 1.0},
        {candle::RangeType::HighLow, 10, 0.1},
        {candle::RangeType::HighLow, 5, 0.2},
        {candle::RangeType::HighLow, 5, 0.6},
        {candle::RangeType::HighLow, 5, 0.05},
    }};
    STATIC_REQUIRE(candle::default_settings == oracle);

    constexpr candle::Settings settings;
    STATIC_REQUIRE(settings.get(candle::SettingType::BodyDoji) == oracle[3]);
    STATIC_REQUIRE(settings.get(candle::SettingType::Equal) == oracle[10]);
}

TEST_CASE("SF011 Phase 5 candle measurements match TA-Lib macros",
          "[sf011][phase5][candlestick]") {
    constexpr candle::Candle bullish{10.0, 13.0, 8.0, 12.0};
    constexpr candle::Candle bearish{12.0, 13.0, 8.0, 9.0};
    constexpr candle::Candle doji{10.0, 11.0, 9.0, 10.0};

    STATIC_REQUIRE(candle::real_body(bullish) == 2.0);
    STATIC_REQUIRE(candle::upper_shadow(bullish) == 1.0);
    STATIC_REQUIRE(candle::lower_shadow(bullish) == 2.0);
    STATIC_REQUIRE(candle::high_low_range(bullish) == 5.0);
    STATIC_REQUIRE(candle::range(candle::RangeType::Shadows, bullish) == 3.0);
    STATIC_REQUIRE(candle::color(bullish) == 1);
    STATIC_REQUIRE(candle::color(bearish) == -1);
    STATIC_REQUIRE(candle::color(doji) == 1);
    STATIC_REQUIRE(candle::signal(doji) == 100.0);
    STATIC_REQUIRE(candle::signal(-1, 200) == -200.0);
    STATIC_REQUIRE(candle::signal(0) == 0.0);
}

TEST_CASE("SF011 Phase 5 gap helpers use strict TA-Lib boundaries",
          "[sf011][phase5][candlestick]") {
    constexpr candle::Candle previous{10.0, 12.0, 9.0, 11.0};
    constexpr candle::Candle body_gap{12.0, 12.5, 11.0, 12.2};
    constexpr candle::Candle full_gap{12.1, 13.0, 12.1, 12.7};
    constexpr candle::Candle touching{11.0, 12.2, 10.8, 12.0};

    STATIC_REQUIRE(candle::real_body_gap_up(body_gap, previous));
    STATIC_REQUIRE_FALSE(candle::candle_gap_up(body_gap, previous));
    STATIC_REQUIRE(candle::candle_gap_up(full_gap, previous));
    STATIC_REQUIRE_FALSE(candle::real_body_gap_up(touching, previous));
    STATIC_REQUIRE(candle::real_body_gap_down(previous, body_gap));
    STATIC_REQUIRE(candle::candle_gap_down(previous, full_gap));
}

TEST_CASE("SF011 Phase 5 candle averages exclude the current bar",
          "[sf011][phase5][candlestick]") {
    candle::AverageWindow window(
        {candle::RangeType::HighLow, 3, 0.2});
    const candle::Candle current{10.0, 20.0, 5.0, 11.0};

    window.advance({10.0, 12.0, 9.0, 11.0}); // range 3
    window.advance({10.0, 15.0, 9.0, 11.0}); // range 6
    REQUIRE_FALSE(window.ready());
    REQUIRE_THROWS_AS(window.threshold(current), std::logic_error);
    window.advance({10.0, 18.0, 9.0, 11.0}); // range 9

    REQUIRE(window.ready());
    REQUIRE(window.size() == 3);
    REQUIRE(window.total() == 18.0);
    REQUIRE(window.threshold(current) == Approx(1.2)); // 0.2 * mean(3,6,9)

    window.advance(current); // retain 6,9,15
    REQUIRE(window.total() == 30.0);
    REQUIRE(window.threshold(current) == 2.0);
    window.reset();
    REQUIRE_FALSE(window.ready());
}

TEST_CASE("SF011 Phase 5 rolling BodyDoji matches pinned TA-Lib probes",
          "[sf011][phase5][candlestick][oracle]") {
    // Frozen by extract_talib_candles.py from Python TA-Lib 0.6.8 / C 0.6.4.
    const std::array<candle::Candle, 8> bars{{
        {10.0, 11.0, 9.0, 10.5}, {10.0, 12.0, 8.0, 10.3},
        {10.0, 13.0, 7.0, 10.1}, {10.0, 14.0, 6.0, 10.4},
        {10.0, 11.0, 9.0, 10.2}, {10.0, 12.0, 8.0, 10.9},
        {10.0, 13.0, 7.0, 9.8}, {10.0, 14.0, 6.0, 10.05},
    }};

    const auto run = [&bars](std::size_t period, double factor) {
        candle::AverageWindow window(
            {candle::RangeType::HighLow, period, factor});
        std::array<int, 8> output{};
        for (std::size_t i = 0; i < bars.size(); ++i) {
            if (window.ready() &&
                candle::real_body(bars[i]) <= window.threshold(bars[i])) {
                output[i] = 100;
            }
            window.advance(bars[i]);
        }
        return output;
    };

    REQUIRE(run(3, 0.1) ==
            std::array<int, 8>{0, 0, 0, 0, 100, 0, 100, 100});
    REQUIRE(run(3, 0.25) ==
            std::array<int, 8>{0, 0, 0, 100, 100, 100, 100, 100});
    REQUIRE(run(0, 0.1) ==
            std::array<int, 8>{0, 100, 100, 100, 100, 0, 100, 100});
}

TEST_CASE("SF011 Phase 5 zero-period and shadow averages match TA-Lib",
          "[sf011][phase5][candlestick]") {
    constexpr candle::Candle bar{10.0, 14.0, 7.0, 12.0};
    constexpr candle::Setting current_body{
        candle::RangeType::RealBody, 0, 2.0};
    constexpr candle::Setting shadow_mean{
        candle::RangeType::Shadows, 2, 1.0};

    STATIC_REQUIRE(candle::average(current_body, 999.0, bar) == 4.0);
    STATIC_REQUIRE(candle::average(shadow_mean, 10.0, bar) == 2.5);

    candle::AverageWindow window(current_body);
    REQUIRE(window.ready());
    REQUIRE(window.threshold(bar) == 4.0);
    window.advance(bar);
    REQUIRE(window.size() == 0);
}

TEST_CASE("SF011 Phase 5 settings drive lookback and minimum period",
          "[sf011][phase5][candlestick]") {
    candle::Settings settings;
    REQUIRE(candle::lookback(settings, 2,
                {candle::SettingType::BodyLong,
                 candle::SettingType::Near}) == 12);
    REQUIRE(candle::minimum_period(settings, 3,
                {candle::SettingType::BodyLong,
                 candle::SettingType::Near}) == 13);

    settings.set(candle::SettingType::Near,
                 {candle::RangeType::HighLow, 20, 0.25});
    REQUIRE(candle::minimum_period(settings, 3,
                {candle::SettingType::BodyLong,
                 candle::SettingType::Near}) == 23);
    settings.restore(candle::SettingType::Near);
    REQUIRE(settings.get(candle::SettingType::Near) ==
            candle::default_settings[candle::index(candle::SettingType::Near)]);
}

TEST_CASE("SF011 Phase 5 rejects invalid configuration",
          "[sf011][phase5][candlestick]") {
    candle::Settings settings;
    REQUIRE_THROWS_AS(
        settings.set(candle::SettingType::BodyLong,
                     {candle::RangeType::RealBody, 10, -0.1}),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        settings.set(candle::SettingType::BodyLong,
                     {candle::RangeType::RealBody, 10,
                      std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
    REQUIRE_THROWS_AS(candle::validate_penetration(-0.1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(
        candle::validate_penetration(
            std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument);
    REQUIRE(candle::validate_penetration(1.5) == 1.5);
    REQUIRE_THROWS_AS(candle::minimum_period(settings, 0, {}),
                      std::invalid_argument);
}

TEST_CASE("SF011 Phase 5 compatibility aliases expose candle settings",
          "[sf011][phase5][candlestick]") {
    STATIC_REQUIRE(std::is_same_v<stratforge::talib::Candle,
                                  candle::Candle>);
    STATIC_REQUIRE(std::is_same_v<stratforge::talib::CandleSettings,
                                  candle::Settings>);
}
