#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/adaptive.hpp>
#include <stratforge/indicators/pattern.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;
using stratforge::test::make_line;
using stratforge::test::run_indicator;

TEST_CASE("VIDYA adapts smoothing to momentum and emits finite values after warmup", "[indicator][phase8][vidya]") {
    auto source = make_line({10, 11, 12, 13, 12, 11, 13, 16, 15, 17});

    stratforge::VIDYA vidya(source, 4, 3);
    run_indicator(source, vidya);

    REQUIRE(std::isnan(vidya.line().data()[2]));
    REQUIRE(std::isfinite(vidya.line().data()[3]));
    REQUIRE(vidya.line().data()[7] > vidya.line().data()[6]);
    REQUIRE(std::fabs(vidya.line().data()[9] - source.data()[9]) < 3.0);
}

TEST_CASE("FRAMA emits finite adaptive averages after full even-period warmup", "[indicator][phase8][frama]") {
    auto source = make_line({10, 10.5, 11, 11.5, 11.2, 11.8, 12.5, 12.2, 13.0, 13.5, 13.2, 14.0});

    stratforge::FRAMA frama(source, 6, 2, 20);
    run_indicator(source, frama);

    REQUIRE(std::isnan(frama.line().data()[4]));
    REQUIRE(std::isfinite(frama.line().data()[5]));
    REQUIRE(frama.line().data()[11] > frama.line().data()[5]);
    REQUIRE(std::fabs(frama.line().data()[11] - source.data()[11]) < 2.0);
}

TEST_CASE("Fisher transform exposes trigger lag and bounded warmup behavior", "[indicator][phase8][fisher]") {
    auto high = make_line({10, 11, 12, 13, 14, 13, 15, 16});
    auto low = make_line({8, 9, 10, 11, 12, 11, 12, 13});

    stratforge::Fisher fisher(high, low, 4);
    for (std::size_t i = 0; i < high.size(); ++i) {
        fisher.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
        }
    }

    REQUIRE(std::isnan(fisher.fisher().data()[2]));
    REQUIRE(std::isfinite(fisher.fisher().data()[3]));
    REQUIRE(fisher.trigger().data()[4] == Approx(fisher.fisher().data()[3]));
    REQUIRE(std::isfinite(fisher.fisher().data().back()));
}

TEST_CASE("Fractal backfills 5-bar pivot locations on the center bar", "[indicator][phase8][fractal]") {
    auto high = make_line({10, 12, 15, 11, 9, 10, 14, 13, 12});
    auto low = make_line({8, 7, 6, 7, 8, 7, 5, 6, 7});

    stratforge::Fractal fractal(high, low);
    for (std::size_t i = 0; i < high.size(); ++i) {
        fractal.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
        }
    }

    REQUIRE(fractal.up_fractal().data()[2] == Approx(15.0));
    REQUIRE(fractal.down_fractal().data()[2] == Approx(6.0));
    REQUIRE(std::isnan(fractal.up_fractal().data()[0]));
    REQUIRE(std::isnan(fractal.down_fractal().data()[8]));
}
