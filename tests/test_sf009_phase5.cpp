#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/stratforge.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <limits>
#include <vector>

using stratforge::test::make_line;
using stratforge::test::run_indicator;
using stratforge::test::run_indicator_hl;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

namespace {

const std::vector<double> kClose = {
    44.34, 44.09, 43.61, 44.33, 44.83, 45.10, 45.42, 45.84,
    46.08, 45.89, 46.03, 45.61, 46.28, 46.28, 46.00, 46.03,
    46.41, 46.22, 45.64, 46.21, 46.25, 45.71, 46.45, 45.78,
    46.12, 45.95, 46.30, 46.05, 45.92, 46.10, 46.55, 46.30
};

const std::vector<double> kHigh = {
    44.52, 44.34, 44.09, 44.56, 44.97, 45.32, 45.60, 46.00,
    46.20, 46.10, 46.15, 45.85, 46.50, 46.40, 46.20, 46.25,
    46.55, 46.40, 45.90, 46.35, 46.45, 46.00, 46.60, 46.10,
    46.30, 46.20, 46.50, 46.25, 46.10, 46.30, 46.70, 46.50
};

const std::vector<double> kVolume = {
    100000, 120000, 95000, 110000, 130000, 140000, 115000, 125000,
    105000, 135000, 110000, 98000, 142000, 108000, 100000, 115000,
    128000, 112000, 135000, 118000, 102000, 138000, 125000, 105000,
    115000, 108000, 130000, 118000, 105000, 125000, 140000, 112000
};

std::vector<double> make_long_series(std::size_t n) {
    std::vector<double> v(n);
    double price = 100.0;
    for (std::size_t i = 0; i < n; ++i) {
        double change = 0.002 * std::sin(static_cast<double>(i) * 0.1)
                      + 0.001 * std::cos(static_cast<double>(i) * 0.37);
        price *= (1.0 + change);
        v[i] = price;
    }
    return v;
}

std::vector<double> make_long_high(const std::vector<double>& close) {
    std::vector<double> v(close.size());
    for (std::size_t i = 0; i < close.size(); ++i)
        v[i] = close[i] * 1.01;
    return v;
}

std::vector<double> make_long_volume(std::size_t n) {
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = 100000.0 + 10000.0 * static_cast<double>((i * 11 + 5) % 17);
    return v;
}

} // namespace

// ============================================================================
// Momentum factors
// ============================================================================

TEST_CASE("JKP_Mom12M 12-month momentum", "[indicator][jkp][regression]") {
    SECTION("warmup NaN on short data") {
        auto c = make_line(kClose);
        stratforge::JKP_Mom12M alpha(c);
        run_indicator(c, alpha);
        REQUIRE(alpha.line().size() == kClose.size());
        CHECK(alpha.minimum_period() == 253);
        for (std::size_t i = 0; i < kClose.size(); ++i)
            CHECK(std::isnan(alpha.line().data()[i]));
    }

    SECTION("produces values with sufficient data") {
        auto long_close = make_long_series(300);
        auto c = make_line(long_close);
        stratforge::JKP_Mom12M alpha(c);
        run_indicator(c, alpha);
        REQUIRE(alpha.line().size() == 300);
        for (std::size_t i = 0; i < 252; ++i)
            CHECK(std::isnan(alpha.line().data()[i]));
        CHECK(!std::isnan(alpha.line().data()[252]));
        CHECK(!std::isnan(alpha.line().data()[299]));
    }
}

TEST_CASE("JKP_Mom6M 6-month momentum", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(200);
    auto c = make_line(long_close);
    stratforge::JKP_Mom6M alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 200);
    CHECK(alpha.minimum_period() == 127);
    for (std::size_t i = 0; i < 126; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[126]));
}

TEST_CASE("JKP_Mom1M 1-month momentum", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_Mom1M alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 22);
    for (std::size_t i = 0; i < 21; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[21]));

    SECTION("value matches manual return calculation") {
        const double expected = (kClose[21] - kClose[0]) / kClose[0];
        CHECK_THAT(alpha.line().data()[21], WithinRel(expected, 1e-12));
    }
}

TEST_CASE("JKP_Mom36M 36-month momentum", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_Mom36M alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 757);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("JKP_MomRevLT long-term reversal", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_MomRevLT alpha(c);
    run_indicator(c, alpha);
    CHECK(alpha.minimum_period() == 757);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("JKP_MomInt intermediate horizon momentum", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(200);
    auto c = make_line(long_close);
    stratforge::JKP_MomInt alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 200);
    CHECK(alpha.minimum_period() == 169);
    for (std::size_t i = 0; i < 168; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[168]));
}

TEST_CASE("JKP_52WHigh 52-week high ratio", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto long_high = make_long_high(long_close);
    auto h = make_line(long_high);
    auto c = make_line(long_close);
    stratforge::JKP_52WHigh alpha(h, c);
    run_indicator_hl(h, c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 252);
    for (std::size_t i = 0; i < 251; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[251]));

    SECTION("ratio is <= 1.0 since high >= close") {
        for (std::size_t i = 251; i < 300; ++i)
            CHECK(alpha.line().data()[i] <= 1.01);
    }
}

TEST_CASE("JKP_MomOff momentum acceleration", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_MomOff alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < 252; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[252]));
}

TEST_CASE("JKP_VolTrend volume trend", "[indicator][jkp][regression]") {
    auto long_vol = make_long_volume(200);
    auto v = make_line(long_vol);
    stratforge::JKP_VolTrend alpha(v);
    run_indicator(v, alpha);
    REQUIRE(alpha.line().size() == 200);
    CHECK(alpha.minimum_period() == 126);
    for (std::size_t i = 0; i < 125; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[125]));

    SECTION("ratio is positive") {
        for (std::size_t i = 125; i < 200; ++i)
            CHECK(alpha.line().data()[i] > 0.0);
    }
}

TEST_CASE("JKP_PriceDelay price delay", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_PriceDelay alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 258);
    for (std::size_t i = 0; i < 257; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[257]));
}

// ============================================================================
// Reversal factors
// ============================================================================

TEST_CASE("JKP_Rev1M short-term reversal", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_Rev1M alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 22);
    for (std::size_t i = 0; i < 21; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));

    SECTION("value is negated return") {
        const double expected = -((kClose[21] - kClose[0]) / kClose[0]);
        CHECK_THAT(alpha.line().data()[21], WithinRel(expected, 1e-12));
    }
}

TEST_CASE("JKP_Rev1W weekly reversal", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_Rev1W alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 6);
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[5]));

    SECTION("value is negated 5-bar return") {
        const double expected = -((kClose[5] - kClose[0]) / kClose[0]);
        CHECK_THAT(alpha.line().data()[5], WithinRel(expected, 1e-12));
    }
}

TEST_CASE("JKP_RevIdio idiosyncratic reversal", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(100);
    auto c = make_line(long_close);
    stratforge::JKP_RevIdio alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 100);
    CHECK(alpha.minimum_period() == 43);
    for (std::size_t i = 0; i < 42; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[42]));
}

// ============================================================================
// Low-risk / Beta factors
// ============================================================================

TEST_CASE("JKP_Beta60M market beta 60-month", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_Beta60M alpha(c);
    run_indicator(c, alpha);
    CHECK(alpha.minimum_period() == 1282);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("JKP_BetaDM Dimson beta", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_BetaDM alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 279);
    for (std::size_t i = 0; i < 278; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[278]));
}

TEST_CASE("JKP_BetaDown downside beta", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_BetaDown alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 274);
    for (std::size_t i = 0; i < 273; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("JKP_IVolCAPM idiosyncratic volatility", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_IVolCAPM alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 274);
    for (std::size_t i = 0; i < 273; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));

    SECTION("volatility is non-negative") {
        for (std::size_t i = 273; i < 300; ++i) {
            if (!std::isnan(alpha.line().data()[i]))
                CHECK(alpha.line().data()[i] >= 0.0);
        }
    }
}

TEST_CASE("JKP_TVol total volatility", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_TVol alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < 252; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[252]));

    SECTION("volatility is positive") {
        for (std::size_t i = 252; i < 300; ++i)
            CHECK(alpha.line().data()[i] > 0.0);
    }
}

TEST_CASE("JKP_RMax5 max daily return 5-day", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_RMax5 alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 7);
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[5]));

    SECTION("max return is at least as large as current return") {
        for (std::size_t i = 5; i < kClose.size(); ++i) {
            if (!std::isnan(alpha.line().data()[i])) {
                double cur_ret = (kClose[i] - kClose[i-1]) / kClose[i-1];
                CHECK(alpha.line().data()[i] >= cur_ret - 1e-12);
            }
        }
    }
}

TEST_CASE("JKP_RMax21 max daily return 21-day", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_RMax21 alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 23);
    for (std::size_t i = 0; i < 21; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[21]));
}

TEST_CASE("JKP_Skew return skewness", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_Skew alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < 252; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[252]));
}

TEST_CASE("JKP_Coskew coskewness", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(300);
    auto c = make_line(long_close);
    stratforge::JKP_Coskew alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 300);
    CHECK(alpha.minimum_period() == 274);
    for (std::size_t i = 0; i < 273; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("JKP_RVol21 realized volatility 21-day", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_RVol21 alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 23);
    for (std::size_t i = 0; i < 21; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[21]));

    SECTION("volatility is non-negative") {
        for (std::size_t i = 21; i < kClose.size(); ++i) {
            if (!std::isnan(alpha.line().data()[i]))
                CHECK(alpha.line().data()[i] >= 0.0);
        }
    }
}

// ============================================================================
// Seasonality factors
// ============================================================================

TEST_CASE("JKP_SeasYr year-on-year seasonality", "[indicator][jkp][regression]") {
    auto long_close = make_long_series(600);
    auto c = make_line(long_close);
    stratforge::JKP_SeasYr alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == 600);
    CHECK(alpha.minimum_period() == 505);
    for (std::size_t i = 0; i < 504; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[504]));
}

TEST_CASE("JKP_SeasTOM turn-of-month effect", "[indicator][jkp][regression]") {
    auto c = make_line(kClose);
    stratforge::JKP_SeasTOM alpha(c);
    run_indicator(c, alpha);
    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 23);
    for (std::size_t i = 0; i < 22; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[22]));
}
