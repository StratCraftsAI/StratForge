#include <catch2/catch_test_macros.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/truerange.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using stratforge::test::make_line;

TEST_CASE("TrueRange emits NaN on first bar and then max of range and gap distances", "[indicator][truerange]") {
    auto high = make_line({10.0, 13.0, 14.0});
    auto low = make_line({8.0, 9.0, 11.0});
    auto close = make_line({9.0, 12.0, 12.5});

    stratforge::TrueRange tr(high, low, close);

    for (std::size_t i = 0; i < close.size(); ++i) {
        tr.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(tr.line().size() == 3);
    REQUIRE(std::isnan(tr.line().data()[0]));
    REQUIRE(tr.line().data()[1] == 4.0);
    REQUIRE(tr.line().data()[2] == 3.0);
}
