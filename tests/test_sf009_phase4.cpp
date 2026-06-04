#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/stratforge.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <limits>
#include <vector>

using stratforge::test::make_line;
using stratforge::test::run_indicator;
using stratforge::test::run_indicator_cv;
using stratforge::test::run_indicator_hl;
using stratforge::test::run_indicator_hlc;
using stratforge::test::run_indicator_oc;
using stratforge::test::run_indicator_ohlc;
using stratforge::test::run_indicator_ohlcv;
using stratforge::test::run_indicator_full_ohlcv;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

namespace {

const std::vector<double> kClose = {
    44.34, 44.09, 43.61, 44.33, 44.83, 45.10, 45.42, 45.84,
    46.08, 45.89, 46.03, 45.61, 46.28, 46.28, 46.00, 46.03,
    46.41, 46.22, 45.64, 46.21, 46.25, 45.71, 46.45, 45.78
};

const std::vector<double> kHigh = {
    44.52, 44.34, 44.09, 44.56, 44.97, 45.32, 45.60, 46.00,
    46.20, 46.10, 46.15, 45.85, 46.50, 46.40, 46.20, 46.25,
    46.55, 46.40, 45.90, 46.35, 46.45, 46.00, 46.60, 46.10
};

const std::vector<double> kLow = {
    44.10, 43.95, 43.50, 44.10, 44.70, 44.95, 45.25, 45.60,
    45.85, 45.75, 45.90, 45.50, 46.10, 46.10, 45.85, 45.90,
    46.20, 46.00, 45.50, 46.00, 46.05, 45.55, 46.25, 45.65
};

const std::vector<double> kOpen = {
    44.25, 44.34, 44.05, 43.70, 44.40, 44.90, 45.15, 45.50,
    45.90, 46.05, 45.95, 46.00, 45.65, 46.30, 46.25, 46.05,
    46.10, 46.40, 46.20, 45.70, 46.25, 46.20, 45.80, 46.40
};

const std::vector<double> kVolume = {
    100000, 120000, 95000, 110000, 130000, 140000, 115000, 125000,
    105000, 135000, 110000, 98000, 142000, 108000, 100000, 115000,
    128000, 112000, 135000, 118000, 102000, 138000, 125000, 105000
};

} // namespace

// ============================================================================
// Alpha191 Step 1: #001 — #080 smoke tests
// ============================================================================

TEST_CASE("Alpha191_001 corr(rank delta log vol, rank ret, 6)", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_001 alpha(o, c, v);
    run_indicator_full_ohlcv(o, o, c, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 258);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_002 delta CLV", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_002 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 2);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[1]));
}

TEST_CASE("Alpha191_003 sum conditional", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_003 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 7);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[6]));
}

TEST_CASE("Alpha191_004 close-volume conditional", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_004 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_005 ts_rank high * volume", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_005 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 12);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[11]));
}

TEST_CASE("Alpha191_006 open-high ranked corr", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    stratforge::Alpha191_006 alpha(o, h);
    run_indicator_cv(o, h, alpha);

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kOpen.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_007 close-volume ts_rank", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_007 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 67);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_008 open-close ranked sum", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_008 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 267);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_009 conditional delta close", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_009 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 6);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[5]));
}

TEST_CASE("Alpha191_010 conditional ret stddev", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_010 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_011 volume-weighted momentum", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_011 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 6);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[5]));
}

TEST_CASE("Alpha191_012 delta close * volume sign", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_012 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 2);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[1]));
}

TEST_CASE("Alpha191_013 close-volume ranked corr", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_013 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_014 open-close-volume combo", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_014 alpha(o, c, v);
    run_indicator_full_ohlcv(o, o, c, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 266);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_015 high-volume corr", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_015 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 261);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_016 rank high-volume cov", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_016 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_017 close-volume ts_rank", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_017 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 266);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_018 open-close decay", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_018 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 262);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_019 close lag/return combo", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_019 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 510);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_020 open-high-low-close pct", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_020 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_021 SMA regression", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_021 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_022 close EWMA deviation", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_022 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_023 SMA conditional", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_023 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 40);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_024 close SMA difference", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_024 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 10);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[9]));
}

TEST_CASE("Alpha191_025 close-volume decay large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_025 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 518);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_026 ts_rank volume-high", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_026 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 12);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[11]));
}

TEST_CASE("Alpha191_027 HLCV WMA combo", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_027 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 262);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_028 HLCV correlation", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_028 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 25);
    for (std::size_t i = 0; i < 24; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_029 close lag sum", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_029 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 270);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_030 close-volume delta regression", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_030 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_031 HLCV large period", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_031 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 276);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_032 HLC close-low combo", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_032 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 237);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_033 open-close ranked", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_033 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_034 close SMA reversal", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_034 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 259);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_035 HLCV volume-weighted SMA", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_035 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 33);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_036 HLCV ranked corr", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_036 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 262);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_037 open-close ranked sum", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_037 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 267);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_038 high SMA conditional", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    stratforge::Alpha191_038 alpha(h);
    run_indicator(h, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 22);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[21]));
}

TEST_CASE("Alpha191_039 OHLCV ranked volume-price", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_039 alpha(o, h, l, c, v);
    run_indicator_full_ohlcv(o, h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 283);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_040 high-volume differential", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_040 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 262);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_041 sqrt(H*L) - VWAP", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_041 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 1);
    CHECK(!std::isnan(alpha.line().data()[0]));
    const double expected = std::sqrt(kHigh[0] * kLow[0]) - (kHigh[0] + kLow[0] + kClose[0]) / 3.0;
    CHECK_THAT(alpha.line().data()[0], WithinRel(expected, 1e-12));
}

TEST_CASE("Alpha191_042 ranked close-volume corr", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_042 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_043 close-volume SMA conditional", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_043 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 27);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_044 high-volume ts_rank corr", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_044 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_045 close-volume ranked decay", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_045 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 277);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_046 close SMA ratio", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_046 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_047 HLC volume-weighted SMA", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_047 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 15);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[14]));
}

TEST_CASE("Alpha191_048 close-volume ranked large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_048 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 273);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_049 high-low sum conditional", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_049 alpha(h, l);
    run_indicator_hl(h, l, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
}

TEST_CASE("Alpha191_050 high-low conditional sum", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_050 alpha(h, l);
    run_indicator_hl(h, l, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
}

TEST_CASE("Alpha191_051 high-low conditional sum variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_051 alpha(h, l);
    run_indicator_hl(h, l, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
}

TEST_CASE("Alpha191_052 HLC rolling sum", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_052 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 27);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_053 close count conditional", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_053 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
}

TEST_CASE("Alpha191_054 open-close decay ranked", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_054 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 262);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_055 OHLC rolling sum", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_055 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_056 OHLCV ranked combo", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_056 alpha(o, h, l, c, v);
    run_indicator_full_ohlcv(o, h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 284);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_057 HLC WMA Bollinger", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_057 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 12);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[11]));
}

TEST_CASE("Alpha191_058 close count up", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_058 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_059 HLC sum conditional close", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_059 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_060 HLCV Williams %R variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_060 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_061 HLCV ranked decay large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_061 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 269);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_062 high-volume ranked corr", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_062 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_063 close SMA decay", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_063 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 7);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[6]));
}

TEST_CASE("Alpha191_064 HLCV ranked decay corr", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_064 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 283);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_065 close SMA close/mean", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_065 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 6);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[5]));
}

TEST_CASE("Alpha191_066 close SMA deviation", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_066 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 6);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[5]));
}

TEST_CASE("Alpha191_067 close SMA max deviation", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_067 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 25);
    for (std::size_t i = 0; i < 24; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_068 HLV volume SMA", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto v = make_line(kVolume);
    stratforge::Alpha191_068 alpha(h, l, v);
    run_indicator_hlc(h, l, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 16);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[15]));
}

TEST_CASE("Alpha191_069 OHL sum conditional", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_069 alpha(o, h, l);
    run_indicator_hlc(o, h, l, alpha);

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_070 close-volume stddev", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_070 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 6);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[5]));
}

TEST_CASE("Alpha191_071 close EMA deviation", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_071 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 24);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[23]));
}

TEST_CASE("Alpha191_072 HLC volume-weighted momentum", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_072 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_073 HLCV decay ranked large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_073 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 287);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_074 HLCV ranked sum large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_074 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 279);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_075 open-close count up", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_075 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 51);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_076 close-volume stddev/mean", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_076 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_077 HLCV ranked decay", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_077 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 295);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_078 HLC momentum ratio", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_078 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 24);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[23]));
}

TEST_CASE("Alpha191_079 close SMA conditional", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_079 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
}

TEST_CASE("Alpha191_080 volume delta ratio", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_080 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 6);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[5]));
}

// ============================================================================
// Alpha191 Step 2: #081 — #120 smoke tests
// ============================================================================

TEST_CASE("Alpha191_081 SMA of volume", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_081 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_082 RSI(6) variant", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_082 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 7);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[6]));
    CHECK(alpha.line().data()[6] >= 0.0);
    CHECK(alpha.line().data()[6] <= 100.0);
}

TEST_CASE("Alpha191_084 signed volume accumulation", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_084 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_088 20-bar momentum", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_088 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
    const double expected = (kClose[20] - kClose[0]) / kClose[0] * 100.0;
    CHECK_THAT(alpha.line().data()[20], WithinRel(expected, 1e-12));
}

TEST_CASE("Alpha191_089 MACD variant", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_089 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 50);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_093 open-low directional", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto l = make_line(kLow);
    stratforge::Alpha191_093 alpha(o, l);
    for (std::size_t i = 0; i < kOpen.size(); ++i) {
        alpha.next();
        if (i + 1 < kOpen.size()) { o.advance(); l.advance(); }
    }

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_095 stddev(amount,20)", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_095 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
    CHECK(alpha.line().data()[19] > 0.0);
}

TEST_CASE("Alpha191_097 stddev(volume,10)", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_097 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 10);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[9]));
    CHECK(alpha.line().data()[9] > 0.0);
}

TEST_CASE("Alpha191_100 stddev(volume,20)", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_100 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_101 body ratio", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_101 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 1);
    const double expected = (kClose[0] - kOpen[0]) / ((kHigh[0] - kLow[0]) + 0.001);
    CHECK_THAT(alpha.line().data()[0], WithinRel(expected, 1e-12));
}

TEST_CASE("Alpha191_102 volume RSI", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_102 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 7);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[6]));
    CHECK(alpha.line().data()[6] >= 0.0);
    CHECK(alpha.line().data()[6] <= 100.0);
}

TEST_CASE("Alpha191_103 lowday(20)", "[indicator][alpha191][regression]") {
    auto l = make_line(kLow);
    stratforge::Alpha191_103 alpha(l);
    run_indicator(l, alpha);

    REQUIRE(alpha.line().size() == kLow.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
    CHECK(alpha.line().data()[19] >= 0.0);
    CHECK(alpha.line().data()[19] < 20.0);
}

TEST_CASE("Alpha191_106 close momentum 20", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_106 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
    CHECK_THAT(alpha.line().data()[20], WithinAbs(kClose[20] - kClose[0], 1e-12));
}

TEST_CASE("Alpha191_109 range EMA ratio", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_109 alpha(h, l);
    for (std::size_t i = 0; i < kHigh.size(); ++i) {
        alpha.next();
        if (i + 1 < kHigh.size()) { h.advance(); l.advance(); }
    }

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_110 ATR-like ratio", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_110 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
    CHECK(alpha.line().data()[20] > 0.0);
}

TEST_CASE("Alpha191_112 up-down ratio", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_112 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
    CHECK(alpha.line().data()[12] >= -100.0);
    CHECK(alpha.line().data()[12] <= 100.0);
}

TEST_CASE("Alpha191_116 regression slope", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_116 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_118 upper-lower range ratio", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_118 alpha(o, h, l);
    for (std::size_t i = 0; i < kOpen.size(); ++i) {
        alpha.next();
        if (i + 1 < kOpen.size()) { o.advance(); h.advance(); l.advance(); }
    }

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
    CHECK(alpha.line().data()[19] > 0.0);
}

// ============================================================================
// Alpha191 Step 3: #121 — #191 smoke tests
// ============================================================================

TEST_CASE("Alpha191_126 typical price", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_126 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 1);
    const double expected = (kClose[0] + kHigh[0] + kLow[0]) / 3.0;
    CHECK_THAT(alpha.line().data()[0], WithinRel(expected, 1e-12));
}

TEST_CASE("Alpha191_129 sum of down moves", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_129 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
    CHECK(alpha.line().data()[12] >= 0.0);
}

TEST_CASE("Alpha191_132 mean amount", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_132 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
    CHECK(alpha.line().data()[19] > 0.0);
}

TEST_CASE("Alpha191_133 highday-lowday spread", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_133 alpha(h, l);
    for (std::size_t i = 0; i < kHigh.size(); ++i) {
        alpha.next();
        if (i + 1 < kHigh.size()) { h.advance(); l.advance(); }
    }

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_134 momentum-volume product", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_134 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
}

TEST_CASE("Alpha191_137 Williams AD variant", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_137 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 2);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[1]));
}

TEST_CASE("Alpha191_139 neg corr open-volume", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto v = make_line(kVolume);
    stratforge::Alpha191_139 alpha(o, v);
    for (std::size_t i = 0; i < kOpen.size(); ++i) {
        alpha.next();
        if (i + 1 < kOpen.size()) { o.advance(); v.advance(); }
    }

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 10);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[9]));
}

TEST_CASE("Alpha191_143 simple returns", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_143 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 2);
    CHECK(std::isnan(alpha.line().data()[0]));
    const double expected = (kClose[1] - kClose[0]) / kClose[0];
    CHECK_THAT(alpha.line().data()[1], WithinRel(expected, 1e-12));
}

TEST_CASE("Alpha191_145 volume MACD", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_145 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 26);
    for (std::size_t i = 0; i < 24; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_150 typical price * volume", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_150 alpha(h, l, c, v);
    run_indicator_full_ohlcv(h, h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 1);
    const double expected = (kClose[0] + kHigh[0] + kLow[0]) / 3.0 * kVolume[0];
    CHECK_THAT(alpha.line().data()[0], WithinRel(expected, 1e-12));
}

TEST_CASE("Alpha191_153 multi-SMA average", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_153 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 24);
    for (std::size_t i = 0; i < 23; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
    CHECK(!std::isnan(alpha.line().data()[23]));
}

TEST_CASE("Alpha191_161 ATR(12)", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_161 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
    CHECK(alpha.line().data()[12] > 0.0);
}

TEST_CASE("Alpha191_162 RSI(12) SMA variant", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_162 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
    CHECK(alpha.line().data()[12] >= 0.0);
    CHECK(alpha.line().data()[12] <= 100.0);
}

TEST_CASE("Alpha191_166 rolling skewness", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_166 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_167 sum of up moves", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_167 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 13);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[12]));
    CHECK(alpha.line().data()[12] >= 0.0);
}

TEST_CASE("Alpha191_168 neg volume ratio", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_168 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
    CHECK(alpha.line().data()[19] < 0.0);
}

TEST_CASE("Alpha191_171 OHLC power ratio", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_171 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 1);
    CHECK(!std::isnan(alpha.line().data()[0]));
}

TEST_CASE("Alpha191_172 ADX(14,6)", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_172 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
    CHECK(alpha.line().data()[20] >= 0.0);
}

TEST_CASE("Alpha191_175 ATR(6)", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_175 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 7);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[6]));
    CHECK(alpha.line().data()[6] > 0.0);
}

TEST_CASE("Alpha191_177 highday ratio", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    stratforge::Alpha191_177 alpha(h);
    run_indicator(h, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
    CHECK(alpha.line().data()[19] >= 0.0);
    CHECK(alpha.line().data()[19] <= 100.0);
}

TEST_CASE("Alpha191_178 return * volume", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_178 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 2);
    CHECK(std::isnan(alpha.line().data()[0]));
    const double expected = (kClose[1] - kClose[0]) / kClose[0] * kVolume[1];
    CHECK_THAT(alpha.line().data()[1], WithinRel(expected, 1e-12));
}

TEST_CASE("Alpha191_182 bullish bar count", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_182 alpha(o, c);
    for (std::size_t i = 0; i < kOpen.size(); ++i) {
        alpha.next();
        if (i + 1 < kOpen.size()) { o.advance(); c.advance(); }
    }

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
    CHECK(alpha.line().data()[19] >= 0.0);
    CHECK(alpha.line().data()[19] <= 1.0);
}

TEST_CASE("Alpha191_187 open breakout sum", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    stratforge::Alpha191_187 alpha(o, h);
    for (std::size_t i = 0; i < kOpen.size(); ++i) {
        alpha.next();
        if (i + 1 < kOpen.size()) { o.advance(); h.advance(); }
    }

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
    CHECK(alpha.line().data()[20] >= 0.0);
}

TEST_CASE("Alpha191_188 range EMA deviation", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    stratforge::Alpha191_188 alpha(h, l);
    for (std::size_t i = 0; i < kHigh.size(); ++i) {
        alpha.next();
        if (i + 1 < kHigh.size()) { h.advance(); l.advance(); }
    }

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 11);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[10]));
}

TEST_CASE("Alpha191_189 MAD(6)", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_189 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 12);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[11]));
    CHECK(alpha.line().data()[11] >= 0.0);
}

TEST_CASE("Alpha191_083 rank high-volume cov variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_083 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_085 close-volume conditional sum", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_085 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 27);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_086 close SMA neg delta", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_086 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_087 OHLC ranked decay large", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_087 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 270);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_090 HLCV ranked corr variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_090 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_091 low-close-volume ranked decay", "[indicator][alpha191][regression]") {
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_091 alpha(l, c, v);
    run_indicator_hlc(l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 297);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_092 HLCV ranked large period", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_092 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 283);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_094 close-volume sum conditional", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_094 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 31);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_096 HLC stochastic variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_096 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 15);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[14]));
}

TEST_CASE("Alpha191_098 HLCV ranked corr large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_098 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 38);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_099 close-volume ranked corr large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_099 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 257);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_104 high-close-volume ranked decay", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_104 alpha(h, c, v);
    run_indicator_hlc(h, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 262);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_105 open-volume ranked corr", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto v = make_line(kVolume);
    stratforge::Alpha191_105 alpha(o, v);
    run_indicator_cv(o, v, alpha);

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 262);
    for (std::size_t i = 0; i < kOpen.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_107 OHLC ranked large", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_107 alpha(o, h, l, c);
    run_indicator_ohlc(o, h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_108 HLCV ranked corr decay", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_108 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 278);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_111 HLCV range SMA", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_111 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 12);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[11]));
}

TEST_CASE("Alpha191_113 close-volume ranked decay large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_113 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 277);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_114 HLCV ranked HL diff", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_114 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 259);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_115 HLCV ranked corr variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_115 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 272);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_117 HLCV return-volume rank", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_117 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 33);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_119 OHLCV ranked large period", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_119 alpha(o, h, l, c, v);
    run_indicator_full_ohlcv(o, h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 295);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_120 HLC close-low ranked", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_120 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_121 HLC ranked large variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_121 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_122 close SMA log ratio", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_122 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 40);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_123 HLV ranked decay large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto v = make_line(kVolume);
    stratforge::Alpha191_123 alpha(h, l, v);
    run_indicator_hlc(h, l, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 292);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_124 HLC ranked decay large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_124 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 284);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_125 HLCV ranked corr very large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_125 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 370);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_127 close power SMA", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_127 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 24);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[23]));
}

TEST_CASE("Alpha191_128 HLCV weighted product", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_128 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 15);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[14]));
}

TEST_CASE("Alpha191_130 HLCV ranked very large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_130 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 310);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_131 HLCV ranked very large variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_131 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 322);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_135 close SMA decay long", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_135 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 42);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_136 open-close-volume ranked decay", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_136 alpha(o, c, v);
    run_indicator_full_ohlcv(o, o, c, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 266);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_138 HLCV ranked decay very large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_138 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 300);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_140 OHLCV ranked very large", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_140 alpha(o, h, l, c, v);
    run_indicator_full_ohlcv(o, h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 350);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_141 high-volume ranked decay large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto v = make_line(kVolume);
    stratforge::Alpha191_141 alpha(h, v);
    run_indicator_cv(h, v, alpha);

    REQUIRE(alpha.line().size() == kHigh.size());
    CHECK(alpha.minimum_period() == 276);
    for (std::size_t i = 0; i < kHigh.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_142 close-volume ranked decay large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_142 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 275);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_144 close-volume conditional ratio", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_144 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_146 close SMA conditional large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_146 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 82);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_147 close SMA regression variant", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_147 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 24);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[23]));
}

TEST_CASE("Alpha191_148 open-volume ranked very large", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto v = make_line(kVolume);
    stratforge::Alpha191_148 alpha(o, v);
    run_indicator_cv(o, v, alpha);

    REQUIRE(alpha.line().size() == kOpen.size());
    CHECK(alpha.minimum_period() == 330);
    for (std::size_t i = 0; i < kOpen.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_149 close ranked conditional large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_149 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_151 close SMA decay variant", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_151 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 41);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_152 close SMA delayed", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_152 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 60);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_154 HLCV ranked very large period", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_154 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 435);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_155 volume SMA combo", "[indicator][alpha191][regression]") {
    auto v = make_line(kVolume);
    stratforge::Alpha191_155 alpha(v);
    run_indicator(v, alpha);

    REQUIRE(alpha.line().size() == kVolume.size());
    CHECK(alpha.minimum_period() == 40);
    for (std::size_t i = 0; i < kVolume.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_156 OHLCV ranked large variant", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_156 alpha(o, h, l, c, v);
    run_indicator_full_ohlcv(o, h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 280);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_157 close log return SMA", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_157 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 20);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[19]));
}

TEST_CASE("Alpha191_158 HLC SMA combo", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_158 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 15);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[14]));
}

TEST_CASE("Alpha191_159 HLC Stoch-like Bollinger", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_159 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 25);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[24]));
}

TEST_CASE("Alpha191_160 close SMA stddev conditional", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_160 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 40);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_163 HLCV ranked decay large variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_163 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_164 HLC SMA conditional combo", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_164 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 27);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_165 close SMA decay large", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_165 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 48);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_169 close SMA decay long period", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_169 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 50);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_170 HLCV ranked large variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_170 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_173 close EMA decay triple", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_173 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 40);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_174 close SMA stddev variant", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_174 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 40);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_176 HLCV ranked corr large period", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_176 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 270);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_179 HLCV ranked very large variant", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_179 alpha(h, l, c, v);
    run_indicator_ohlcv(h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 315);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_180 close-volume mean revert", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_180 alpha(c, v);
    run_indicator_cv(c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 68);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_181 close return SMA ratio", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_181 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_183 close EMA combo", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_183 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 24);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[23]));
}

TEST_CASE("Alpha191_184 open-close ranked very large", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_184 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 454);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_185 open-close ranked large", "[indicator][alpha191][regression]") {
    auto o = make_line(kOpen);
    auto c = make_line(kClose);
    stratforge::Alpha191_185 alpha(o, c);
    run_indicator_oc(o, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 253);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_186 HLC ADX variant large", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    stratforge::Alpha191_186 alpha(h, l, c);
    run_indicator_hlc(h, l, c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 28);
    for (std::size_t i = 0; i < kClose.size(); ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}

TEST_CASE("Alpha191_190 close conditional ratio", "[indicator][alpha191][regression]") {
    auto c = make_line(kClose);
    stratforge::Alpha191_190 alpha(c);
    run_indicator(c, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 21);
    CHECK(std::isnan(alpha.line().data()[0]));
    CHECK(!std::isnan(alpha.line().data()[20]));
}

TEST_CASE("Alpha191_191 corr + midpoint - close", "[indicator][alpha191][regression]") {
    auto h = make_line(kHigh);
    auto l = make_line(kLow);
    auto c = make_line(kClose);
    auto v = make_line(kVolume);
    stratforge::Alpha191_191 alpha(h, l, c, v);
    run_indicator_full_ohlcv(h, h, l, c, v, alpha);

    REQUIRE(alpha.line().size() == kClose.size());
    CHECK(alpha.minimum_period() == 25);
    CHECK(std::isnan(alpha.line().data()[0]));
    for (std::size_t i = 0; i < 24; ++i)
        CHECK(std::isnan(alpha.line().data()[i]));
}
