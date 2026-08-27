#include <catch2/catch_test_macros.hpp>

#include <stratforge/indicators/candle_structure.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

TEST_CASE("OutsideBar freezes inclusive and strict equality boundaries",
          "[sf012][structure][outside]") {
    auto high = stratforge::test::make_line({10.0, 10.0, 11.0, 12.0, 13.0});
    auto low = stratforge::test::make_line({5.0, 4.0, 4.0, 3.0, 4.0});
    stratforge::OutsideBar inclusive(high, low);
    stratforge::test::run_indicator_hl(high, low, inclusive);
    CHECK(inclusive.minimum_period() == 2uz);
    CHECK(inclusive.boundary() == stratforge::BoundaryMode::inclusive);
    CHECK(inclusive.line().data() == std::vector<double>{0.0, 1.0, 1.0, 1.0, 0.0});

    high.home();
    low.home();
    stratforge::OutsideBar strict(high, low, stratforge::BoundaryMode::strict);
    stratforge::test::run_indicator_hl(high, low, strict);
    CHECK(strict.line().data() == std::vector<double>{0.0, 0.0, 0.0, 1.0, 0.0});
}

TEST_CASE("OutsideBar rejects invalid pairs and their immediate successors",
          "[sf012][structure][outside][invalid]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto high = stratforge::test::make_line({10.0, 9.0, 12.0, 13.0, 14.0});
    auto low = stratforge::test::make_line({5.0, 10.0, 4.0, nan, 3.0});
    stratforge::OutsideBar outside(high, low);
    stratforge::test::run_indicator_hl(high, low, outside);
    CHECK(outside.line().data() == std::vector<double>{0.0, 0.0, 0.0, 0.0, 0.0});
}

TEST_CASE("InsideBarDepth retains its mother through nested bars and resets after breakout",
          "[sf012][structure][inside]") {
    auto high = stratforge::test::make_line({10.0, 9.0, 8.0, 11.0, 12.0, 13.0});
    auto low = stratforge::test::make_line({0.0, 1.0, 2.0, 1.0, -1.0, -2.0});
    stratforge::InsideBarDepth inside(high, low);
    stratforge::test::run_indicator_hl(high, low, inside);

    CHECK(inside.minimum_period() == 1uz);
    CHECK(inside.accessor_minimum_period("breakout") == 1uz);
    CHECK(inside.depth().data() == std::vector<double>{0.0, 1.0, 2.0, 0.0, 0.0, 0.0});
    CHECK(inside.mother_range().data() ==
          std::vector<double>{10.0, 10.0, 10.0, 10.0, 13.0, 15.0});
    CHECK(inside.breakout().data() ==
          std::vector<double>{0.0, 0.0, 0.0, 1.0, 0.0, 0.0});
}

TEST_CASE("InsideBarDepth boundary contact differs by mode",
          "[sf012][structure][inside][boundary]") {
    auto high = stratforge::test::make_line({10.0, 10.0, 9.0, 11.0});
    auto low = stratforge::test::make_line({0.0, 1.0, 0.0, -1.0});
    stratforge::InsideBarDepth inclusive(high, low);
    stratforge::test::run_indicator_hl(high, low, inclusive);
    CHECK(inclusive.depth().data() == std::vector<double>{0.0, 1.0, 2.0, 0.0});
    CHECK(inclusive.breakout().data() == std::vector<double>{0.0, 0.0, 0.0, 0.0});

    high.home();
    low.home();
    stratforge::InsideBarDepth strict(high, low, stratforge::BoundaryMode::strict);
    stratforge::test::run_indicator_hl(high, low, strict);
    CHECK(strict.depth().data() == std::vector<double>{0.0, 0.0, 0.0, 0.0});
    CHECK(strict.breakout().data() == std::vector<double>{0.0, 1.0, -1.0, 0.0});
}

TEST_CASE("InsideBarDepth clears invalid state and recovers with a new mother",
          "[sf012][structure][inside][invalid]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto high = stratforge::test::make_line({10.0, 9.0, nan, 8.0, 7.0});
    auto low = stratforge::test::make_line({0.0, 1.0, 2.0, 3.0, 4.0});
    stratforge::InsideBarDepth inside(high, low);
    stratforge::test::run_indicator_hl(high, low, inside);
    CHECK(inside.depth().data() == std::vector<double>{0.0, 1.0, 0.0, 0.0, 1.0});
    CHECK(std::isnan(inside.mother_range().data()[2]));
    CHECK(inside.mother_range().data()[3] == 5.0);
}

TEST_CASE("NarrowRange and WideRange use strict extrema including the current bar",
          "[sf012][structure][range]") {
    auto open = stratforge::test::make_line({5.0, 5.0, 5.0, 5.0, 5.0});
    auto high = stratforge::test::make_line({9.0, 8.0, 8.0, 10.0, 11.0});
    auto low = stratforge::test::make_line({1.0, 2.0, 2.0, 0.0, -1.0});
    auto close = stratforge::test::make_line({5.0, 5.0, 5.0, 5.0, 5.0});
    stratforge::NarrowRange narrow(open, high, low, close, 2uz);
    stratforge::WideRange wide(open, high, low, close, 2uz);
    for (std::size_t i = 0; i < close.size(); ++i) {
        narrow.next();
        wide.next();
        if (i + 1uz < close.size()) {
            open.advance(); high.advance(); low.advance(); close.advance();
        }
    }
    CHECK(narrow.minimum_period() == 2uz);
    CHECK(narrow.period() == 2uz);
    CHECK(narrow.line().data() == std::vector<double>{0.0, 1.0, 0.0, 0.0, 0.0});
    CHECK(wide.line().data() == std::vector<double>{0.0, 0.0, 0.0, 1.0, 1.0});
}

TEST_CASE("Range detectors enforce period and consecutive valid OHLC",
          "[sf012][structure][range][invalid]") {
    auto open = stratforge::test::make_line({1.0, 2.0, 3.0, 4.0});
    auto high = stratforge::test::make_line({2.0, 1.0, 4.0, 5.0});
    auto low = stratforge::test::make_line({0.0, 0.0, 2.0, 3.0});
    auto close = stratforge::test::make_line({1.0, 0.5, 3.0, 4.0});
    CHECK_THROWS_AS(stratforge::NarrowRange(open, high, low, close, 0uz),
                    std::invalid_argument);

    stratforge::NarrowRange narrow(open, high, low, close, 2uz);
    stratforge::test::run_indicator_ohlc(open, high, low, close, narrow);
    CHECK(narrow.line().data() == std::vector<double>{0.0, 0.0, 0.0, 0.0});

    open.home(); high.home(); low.home(); close.home();
    stratforge::WideRange singleton(open, high, low, close, 1uz);
    stratforge::test::run_indicator_ohlc(open, high, low, close, singleton);
    CHECK(singleton.line().data() == std::vector<double>{1.0, 0.0, 1.0, 1.0});
}
