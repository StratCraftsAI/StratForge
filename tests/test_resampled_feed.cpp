// SPDX-License-Identifier: MIT
//
// tests/test_resampled_feed.cpp -- ResampledFeed acceptance tests.
//
//  P1: Verify calendar-aligned OHLCV aggregation against
// hand-built expected buckets, including months with weekend gaps and
// variable lengths.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/data/resampled_feed.hpp>

#include "test_helpers.hpp"

#include <chrono>
#include <cstddef>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinAbs;

namespace {

/// Build an InMemoryFeed with explicit timestamps from the test helper.
using TestFeed = stratforge::test::InMemoryFeed;

/// Helper: build a DateTime from year/month/day/hour/minute.
DateTime make_dt(int y, unsigned m, unsigned d,
                 int hour = 0, int minute = 0) {
    namespace chrono = std::chrono;
    const chrono::year_month_day ymd{
        chrono::year{y}, chrono::month{m}, chrono::day{d}};
    return DateTime(chrono::sys_days{ymd}) +
           chrono::hours(hour) + chrono::minutes(minute);
}

} // namespace

TEST_CASE("ResampledFeed aggregates hourly bars into monthly buckets",
          "[resample][multi_tf]") {
    // Build 3 months of hourly bars: Jan, Feb, Mar 2024.
    // We put a few bars per month to keep the test tractable.
    // Jan 2024: 3 bars (2nd, 3rd, 4th at 10:00)
    // Feb 2024: 2 bars (1st, 2nd at 10:00)
    // Mar 2024: 2 bars (1st, 5th at 10:00 -- gap over weekend)
    std::vector<TestFeed::Bar> bars = {
        {make_dt(2024, 1, 2, 10, 0), 100.0, 105.0, 98.0,  102.0, 1000.0},
        {make_dt(2024, 1, 3, 10, 0), 102.0, 108.0, 101.0, 107.0, 1100.0},
        {make_dt(2024, 1, 4, 10, 0), 107.0, 110.0, 106.0, 109.0, 900.0},
        {make_dt(2024, 2, 1, 10, 0), 200.0, 210.0, 195.0, 205.0, 2000.0},
        {make_dt(2024, 2, 2, 10, 0), 205.0, 215.0, 200.0, 212.0, 2100.0},
        {make_dt(2024, 3, 1, 10, 0), 300.0, 310.0, 295.0, 305.0, 3000.0},
        {make_dt(2024, 3, 5, 10, 0), 305.0, 320.0, 300.0, 315.0, 3100.0},
    };

    TestFeed base(bars);
    REQUIRE(base.load());
    base.set_timeframe({TimeFrame::Minutes, 60}); // 1h

    auto monthly_tfc = *parse_interval("1M");
    ResampledFeed resampled(base, monthly_tfc);
    resampled.preload();

    REQUIRE(resampled.size() == 3); // Jan, Feb, Mar

    // Jan 2024 bucket: open=100, high=max(105,108,110)=110,
    //   low=min(98,101,106)=98, close=109, vol=1000+1100+900=3000
    CHECK_THAT(resampled.open().data()[0], WithinAbs(100.0, 1e-12));
    CHECK_THAT(resampled.high().data()[0], WithinAbs(110.0, 1e-12));
    CHECK_THAT(resampled.low().data()[0], WithinAbs(98.0, 1e-12));
    CHECK_THAT(resampled.close().data()[0], WithinAbs(109.0, 1e-12));
    CHECK_THAT(resampled.volume().data()[0], WithinAbs(3000.0, 1e-12));

    // Feb 2024 bucket: open=200, high=max(210,215)=215,
    //   low=min(195,200)=195, close=212, vol=2000+2100=4100
    CHECK_THAT(resampled.open().data()[1], WithinAbs(200.0, 1e-12));
    CHECK_THAT(resampled.high().data()[1], WithinAbs(215.0, 1e-12));
    CHECK_THAT(resampled.low().data()[1], WithinAbs(195.0, 1e-12));
    CHECK_THAT(resampled.close().data()[1], WithinAbs(212.0, 1e-12));
    CHECK_THAT(resampled.volume().data()[1], WithinAbs(4100.0, 1e-12));

    // Mar 2024 bucket (with weekend gap): open=300, high=max(310,320)=320,
    //   low=min(295,300)=295, close=315, vol=3000+3100=6100
    CHECK_THAT(resampled.open().data()[2], WithinAbs(300.0, 1e-12));
    CHECK_THAT(resampled.high().data()[2], WithinAbs(320.0, 1e-12));
    CHECK_THAT(resampled.low().data()[2], WithinAbs(295.0, 1e-12));
    CHECK_THAT(resampled.close().data()[2], WithinAbs(315.0, 1e-12));
    CHECK_THAT(resampled.volume().data()[2], WithinAbs(6100.0, 1e-12));

    // Timestamps are period_start of each month
    CHECK(resampled.datetime().data()[0] == make_dt(2024, 1, 1));
    CHECK(resampled.datetime().data()[1] == make_dt(2024, 2, 1));
    CHECK(resampled.datetime().data()[2] == make_dt(2024, 3, 1));

    // Timeframe is set correctly
    CHECK(resampled.timeframe().timeframe == TimeFrame::Months);
    CHECK(resampled.timeframe().compression == 1);
}

TEST_CASE("ResampledFeed aggregates 5min bars into weekly buckets",
          "[resample][multi_tf]") {
    // Two weeks: Mon Jan 1 2024 to Sun Jan 7, and Mon Jan 8 to Sun Jan 14.
    // A few 5-min bars in each week.
    std::vector<TestFeed::Bar> bars = {
        // Week 1 (Mon Jan 1 - Sun Jan 7)
        {make_dt(2024, 1, 1, 9, 0),  50.0, 52.0, 49.0, 51.0, 500.0},
        {make_dt(2024, 1, 1, 9, 5),  51.0, 53.0, 50.0, 52.5, 600.0},
        {make_dt(2024, 1, 3, 14, 0), 52.0, 55.0, 51.0, 54.0, 700.0},
        // Week 2 (Mon Jan 8 - Sun Jan 14)
        {make_dt(2024, 1, 8, 9, 0),  60.0, 62.0, 59.0, 61.0, 800.0},
        {make_dt(2024, 1, 10, 9, 0), 61.0, 65.0, 60.0, 64.0, 900.0},
    };

    TestFeed base(bars);
    REQUIRE(base.load());
    base.set_timeframe({TimeFrame::Minutes, 5}); // 5m

    auto weekly_tfc = *parse_interval("1w");
    ResampledFeed resampled(base, weekly_tfc);
    resampled.preload();

    REQUIRE(resampled.size() == 2);

    // Week 1: open=50, high=max(52,53,55)=55, low=min(49,50,51)=49,
    //   close=54, vol=500+600+700=1800
    CHECK_THAT(resampled.open().data()[0], WithinAbs(50.0, 1e-12));
    CHECK_THAT(resampled.high().data()[0], WithinAbs(55.0, 1e-12));
    CHECK_THAT(resampled.low().data()[0], WithinAbs(49.0, 1e-12));
    CHECK_THAT(resampled.close().data()[0], WithinAbs(54.0, 1e-12));
    CHECK_THAT(resampled.volume().data()[0], WithinAbs(1800.0, 1e-12));

    // Week 2: open=60, high=max(62,65)=65, low=min(59,60)=59,
    //   close=64, vol=800+900=1700
    CHECK_THAT(resampled.open().data()[1], WithinAbs(60.0, 1e-12));
    CHECK_THAT(resampled.high().data()[1], WithinAbs(65.0, 1e-12));
    CHECK_THAT(resampled.low().data()[1], WithinAbs(59.0, 1e-12));
    CHECK_THAT(resampled.close().data()[1], WithinAbs(64.0, 1e-12));
    CHECK_THAT(resampled.volume().data()[1], WithinAbs(1700.0, 1e-12));
}

TEST_CASE("ResampledFeed single bar in source produces single bar output",
          "[resample][multi_tf]") {
    std::vector<TestFeed::Bar> bars = {
        {make_dt(2024, 6, 15, 10, 0), 100.0, 105.0, 95.0, 102.0, 5000.0},
    };

    TestFeed base(bars);
    REQUIRE(base.load());
    base.set_timeframe({TimeFrame::Minutes, 60});

    auto monthly_tfc = *parse_interval("1M");
    ResampledFeed resampled(base, monthly_tfc);
    resampled.preload();

    REQUIRE(resampled.size() == 1);
    CHECK_THAT(resampled.open().data()[0], WithinAbs(100.0, 1e-12));
    CHECK_THAT(resampled.close().data()[0], WithinAbs(102.0, 1e-12));
}

TEST_CASE("ResampledFeed empty source produces empty output",
          "[resample][multi_tf]") {
    std::vector<TestFeed::Bar> bars;
    TestFeed base(bars);
    static_cast<void>(base.load()); // empty span returns false, expected
    base.set_timeframe({TimeFrame::Minutes, 60});

    auto monthly_tfc = *parse_interval("1M");
    ResampledFeed resampled(base, monthly_tfc);
    resampled.preload();

    REQUIRE(resampled.size() == 0);
}

TEST_CASE("ResampledFeed variable month lengths (Feb leap year)",
          "[resample][multi_tf]") {
    // Feb 2024 is a leap year (29 days). Two bars: Feb 1 and Feb 29.
    // Mar 2024: one bar Mar 1.
    std::vector<TestFeed::Bar> bars = {
        {make_dt(2024, 2, 1, 10, 0),  10.0, 15.0, 9.0,  12.0, 100.0},
        {make_dt(2024, 2, 29, 10, 0), 12.0, 18.0, 11.0, 16.0, 200.0},
        {make_dt(2024, 3, 1, 10, 0),  20.0, 25.0, 19.0, 22.0, 300.0},
    };

    TestFeed base(bars);
    REQUIRE(base.load());
    base.set_timeframe({TimeFrame::Minutes, 60});

    auto monthly_tfc = *parse_interval("1M");
    ResampledFeed resampled(base, monthly_tfc);
    resampled.preload();

    REQUIRE(resampled.size() == 2); // Feb, Mar

    // Feb: open=10, high=max(15,18)=18, low=min(9,11)=9, close=16, vol=300
    CHECK_THAT(resampled.open().data()[0], WithinAbs(10.0, 1e-12));
    CHECK_THAT(resampled.high().data()[0], WithinAbs(18.0, 1e-12));
    CHECK_THAT(resampled.low().data()[0], WithinAbs(9.0, 1e-12));
    CHECK_THAT(resampled.close().data()[0], WithinAbs(16.0, 1e-12));
    CHECK_THAT(resampled.volume().data()[0], WithinAbs(300.0, 1e-12));

    // Mar: single bar
    CHECK_THAT(resampled.open().data()[1], WithinAbs(20.0, 1e-12));
    CHECK_THAT(resampled.close().data()[1], WithinAbs(22.0, 1e-12));
}

TEST_CASE("ResampledFeed 1min to 1h aggregation",
          "[resample][multi_tf]") {
    // 3 one-minute bars in the 09:00 hour, 2 in the 10:00 hour
    std::vector<TestFeed::Bar> bars = {
        {make_dt(2024, 1, 2, 9, 0),  1.0, 1.5, 0.8, 1.2, 10.0},
        {make_dt(2024, 1, 2, 9, 1),  1.2, 1.8, 1.0, 1.5, 20.0},
        {make_dt(2024, 1, 2, 9, 2),  1.5, 2.0, 1.4, 1.9, 30.0},
        {make_dt(2024, 1, 2, 10, 0), 3.0, 3.5, 2.8, 3.2, 40.0},
        {make_dt(2024, 1, 2, 10, 1), 3.2, 3.8, 3.0, 3.6, 50.0},
    };

    TestFeed base(bars);
    REQUIRE(base.load());
    base.set_timeframe({TimeFrame::Minutes, 1});

    auto hourly_tfc = *parse_interval("1h");
    ResampledFeed resampled(base, hourly_tfc);
    resampled.preload();

    REQUIRE(resampled.size() == 2);

    // 09:00 bucket: open=1.0, high=2.0, low=0.8, close=1.9, vol=60
    CHECK_THAT(resampled.open().data()[0], WithinAbs(1.0, 1e-12));
    CHECK_THAT(resampled.high().data()[0], WithinAbs(2.0, 1e-12));
    CHECK_THAT(resampled.low().data()[0], WithinAbs(0.8, 1e-12));
    CHECK_THAT(resampled.close().data()[0], WithinAbs(1.9, 1e-12));
    CHECK_THAT(resampled.volume().data()[0], WithinAbs(60.0, 1e-12));

    // 10:00 bucket: open=3.0, high=3.8, low=2.8, close=3.6, vol=90
    CHECK_THAT(resampled.open().data()[1], WithinAbs(3.0, 1e-12));
    CHECK_THAT(resampled.high().data()[1], WithinAbs(3.8, 1e-12));
    CHECK_THAT(resampled.low().data()[1], WithinAbs(2.8, 1e-12));
    CHECK_THAT(resampled.close().data()[1], WithinAbs(3.6, 1e-12));
    CHECK_THAT(resampled.volume().data()[1], WithinAbs(90.0, 1e-12));
}

TEST_CASE("ResampledFeed index starts at 0 after preload (navigable)",
          "[resample][multi_tf]") {
    std::vector<TestFeed::Bar> bars = {
        {make_dt(2024, 1, 2, 10, 0), 100.0, 105.0, 98.0, 102.0, 1000.0},
        {make_dt(2024, 2, 1, 10, 0), 200.0, 210.0, 195.0, 205.0, 2000.0},
    };

    TestFeed base(bars);
    REQUIRE(base.load());
    base.set_timeframe({TimeFrame::Minutes, 60});

    auto monthly_tfc = *parse_interval("1M");
    ResampledFeed resampled(base, monthly_tfc);
    resampled.preload();

    REQUIRE(resampled.size() == 2);
    CHECK(resampled.index() == 0);
    CHECK_THAT(resampled.close()[0], WithinAbs(102.0, 1e-12));

    resampled.advance();
    CHECK(resampled.index() == 1);
    CHECK_THAT(resampled.close()[0], WithinAbs(205.0, 1e-12));
}
