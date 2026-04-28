#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/pattern.hpp>
#include <stratforge/indicators/volume.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;
using stratforge::test::make_line;

TEST_CASE("Klinger oscillator produces finite oscillator and signal outputs after initialization", "[indicator][phase8][klinger]") {
    auto high = make_line({10, 11, 12, 13, 12, 11, 13, 15, 14, 16});
    auto low = make_line({8, 9, 10, 11, 10, 9, 11, 13, 12, 14});
    auto close = make_line({9, 10, 11, 12, 11, 10, 12, 14, 13, 15});
    auto volume = make_line({100, 120, 130, 140, 110, 100, 150, 160, 145, 170});

    stratforge::Klinger klinger(high, low, close, volume, 3, 5, 2);
    for (std::size_t i = 0; i < close.size(); ++i) {
        klinger.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
            volume.advance();
        }
    }

    REQUIRE(std::isnan(klinger.kvo().data()[0]));
    REQUIRE(std::isfinite(klinger.kvo().data()[2]));
    REQUIRE(std::isfinite(klinger.signal().data()[2]));
    REQUIRE(klinger.kvo().data().back() != Approx(klinger.signal().data().back()));
}

TEST_CASE("ZigZag backfills confirmed pivot bars once reversals exceed retrace threshold", "[indicator][phase8][zigzag]") {
    auto source = make_line({100, 105, 110, 103, 95, 98, 108, 101, 92, 96});

    stratforge::ZigZag zigzag(source, 0.08);
    for (std::size_t i = 0; i < source.size(); ++i) {
        zigzag.next();
        if (i + 1 < source.size()) {
            source.advance();
        }
    }

    REQUIRE(zigzag.line().data()[2] == Approx(110.0));
    REQUIRE(zigzag.line().data()[4] == Approx(95.0));
    REQUIRE(zigzag.line().data()[6] == Approx(108.0));
    REQUIRE(std::isnan(zigzag.line().data()[9]));
}
