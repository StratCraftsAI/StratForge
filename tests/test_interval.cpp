// SPDX-License-Identifier: MIT
//
// tests/test_interval.cpp —  P0 acceptance suite for the canonical
// interval vocabulary and UTC calendar period bucketing. Tag form
// [interval][regression].

#include <catch2/catch_test_macros.hpp>

#include <stratforge/data/interval.hpp>

#include <array>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using stratforge::DataError;
using stratforge::DateTime;
using stratforge::TimeFrame;
using stratforge::TimeFrameCompression;

/// UTC civil time -> DateTime (all anchors in this suite are UTC by contract).
constexpr DateTime utc(int y, unsigned mo, unsigned d, int hh = 0, int mi = 0, int ss = 0) {
    namespace chrono = std::chrono;
    return chrono::sys_days{chrono::year{y} / chrono::month{mo} / chrono::day{d}} +
           chrono::hours{hh} + chrono::minutes{mi} + chrono::seconds{ss};
}

/// Parse a token the suite knows is valid; REQUIREs success.
TimeFrameCompression must_parse(std::string_view token) {
    const auto parsed = stratforge::parse_interval(token);
    REQUIRE(parsed.has_value());
    return *parsed;
}

constexpr std::array<std::string_view, 10> kAllTokens{
    "1m", "5m", "15m", "30m", "1h", "2h", "4h", "1d", "1w", "1M",
};

} // namespace

TEST_CASE("parse_interval round-trips all 10 vocabulary tokens", "[interval][regression]") {
    for (const auto token : kAllTokens) {
        const auto parsed = stratforge::parse_interval(token);
        REQUIRE(parsed.has_value());
        REQUIRE(stratforge::to_string(*parsed) == token);
    }
}

TEST_CASE("parse_interval maps tokens to the engine timeframes", "[interval][regression]") {
    const auto expect = [](std::string_view token, TimeFrame tf, int compression) {
        const auto tfc = must_parse(token);
        REQUIRE(tfc.timeframe == tf);
        REQUIRE(tfc.compression == compression);
    };
    expect("1m", TimeFrame::Minutes, 1);
    expect("5m", TimeFrame::Minutes, 5);
    expect("15m", TimeFrame::Minutes, 15);
    expect("30m", TimeFrame::Minutes, 30);
    expect("1h", TimeFrame::Minutes, 60);
    expect("2h", TimeFrame::Minutes, 120);
    expect("4h", TimeFrame::Minutes, 240);
    expect("1d", TimeFrame::Days, 1);
    expect("1w", TimeFrame::Weeks, 1);
    expect("1M", TimeFrame::Months, 1);
}

TEST_CASE("1m and 1M are distinct case-sensitive tokens", "[interval][regression]") {
    const auto minute = must_parse("1m");
    const auto month = must_parse("1M");
    REQUIRE(minute.timeframe == TimeFrame::Minutes);
    REQUIRE(minute.compression == 1);
    REQUIRE(month.timeframe == TimeFrame::Months);
    REQUIRE(month.compression == 1);
    REQUIRE(stratforge::to_string(minute) == "1m");
    REQUIRE(stratforge::to_string(month) == "1M");
}

TEST_CASE("parse_interval rejects tokens outside the vocabulary", "[interval][regression]") {
    constexpr std::array<std::string_view, 14> invalid{
        "1H", "1D", "1W", "2H", "5M",       // wrong case
        "1x", "60m", "3h", "2d", "01m",     // outside vocabulary
        "", "1", "m", "1m ",                // malformed
    };
    for (const auto token : invalid) {
        const auto parsed = stratforge::parse_interval(token);
        REQUIRE_FALSE(parsed.has_value());
        REQUIRE(parsed.error() == DataError::InvalidInterval);
    }
    REQUIRE(stratforge::to_string(DataError::InvalidInterval) == "invalid interval token");
}

TEST_CASE("to_string returns empty for out-of-vocabulary timeframes", "[interval][regression]") {
    REQUIRE(stratforge::to_string({TimeFrame::Minutes, 7}).empty());
    REQUIRE(stratforge::to_string({TimeFrame::Seconds, 1}).empty());
    REQUIRE(stratforge::to_string({TimeFrame::Months, 2}).empty());
}

TEST_CASE("duration_of covers all fixed-span tokens", "[interval][regression]") {
    using std::chrono::seconds;
    const auto expect = [](std::string_view token, std::int64_t secs) {
        REQUIRE(stratforge::duration_of(must_parse(token)) == seconds{secs});
    };
    expect("1m", 60);
    expect("5m", 300);
    expect("15m", 900);
    expect("30m", 1800);
    expect("1h", 3600);
    expect("2h", 7200);
    expect("4h", 14400);
    expect("1d", 86400);
}

TEST_CASE("duration_of refuses variable-span and invalid timeframes", "[interval][regression]") {
    REQUIRE_THROWS_AS(stratforge::duration_of(must_parse("1w")), std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::duration_of(must_parse("1M")), std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::duration_of({TimeFrame::Minutes, 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::duration_of({TimeFrame::Minutes, -5}), std::invalid_argument);
}

TEST_CASE("period_start floors fixed-span timeframes day-aligned", "[interval][regression]") {
    REQUIRE(stratforge::period_start(utc(2026, 7, 12, 3, 37, 11), must_parse("1m")) ==
            utc(2026, 7, 12, 3, 37, 0));
    REQUIRE(stratforge::period_start(utc(2026, 7, 12, 3, 37, 11), must_parse("2h")) ==
            utc(2026, 7, 12, 2, 0, 0));
    REQUIRE(stratforge::period_start(utc(2026, 7, 12, 23, 59, 59), must_parse("4h")) ==
            utc(2026, 7, 12, 20, 0, 0));
    REQUIRE(stratforge::period_start(utc(2026, 7, 12, 15, 0, 0), must_parse("1d")) ==
            utc(2026, 7, 12, 0, 0, 0));
    // An exact boundary is its own period start.
    REQUIRE(stratforge::period_start(utc(2026, 7, 12, 2, 0, 0), must_parse("2h")) ==
            utc(2026, 7, 12, 2, 0, 0));
}

TEST_CASE("period_start floor-divides pre-epoch instants", "[interval][regression]") {
    REQUIRE(stratforge::period_start(utc(1969, 12, 31, 23, 30, 0), must_parse("1h")) ==
            utc(1969, 12, 31, 23, 0, 0));
    REQUIRE(stratforge::period_start(utc(1969, 12, 31, 23, 30, 0), must_parse("1d")) ==
            utc(1969, 12, 31, 0, 0, 0));
}

TEST_CASE("weeks anchor Monday 00:00 UTC", "[interval][regression]") {
    const auto week = must_parse("1w");
    // 2024-01-01 was a Monday.
    REQUIRE(std::chrono::weekday{std::chrono::sys_days{std::chrono::year{2024} /
                                                       std::chrono::month{1} /
                                                       std::chrono::day{1}}} ==
            std::chrono::Monday);
    // Wednesday mid-week -> preceding Monday.
    REQUIRE(stratforge::period_start(utc(2024, 1, 3, 15, 30, 0), week) == utc(2024, 1, 1));
    // Monday itself -> same day 00:00.
    REQUIRE(stratforge::period_start(utc(2024, 1, 1, 5, 0, 0), week) == utc(2024, 1, 1));
    // Sunday (last day of the week) -> the Monday six days earlier.
    REQUIRE(stratforge::period_start(utc(2024, 1, 7, 23, 59, 59), week) == utc(2024, 1, 1));
    // Exclusive end = next Monday 00:00.
    REQUIRE(stratforge::period_end(utc(2024, 1, 3, 15, 30, 0), week) == utc(2024, 1, 8));
}

TEST_CASE("month boundaries: Jan 31, leap February, Dec 31", "[interval][regression]") {
    const auto month = must_parse("1M");
    // Jan 31 -> [Jan 1, Feb 1).
    REQUIRE(stratforge::period_start(utc(2026, 1, 31, 23, 0, 0), month) == utc(2026, 1, 1));
    REQUIRE(stratforge::period_end(utc(2026, 1, 31, 23, 0, 0), month) == utc(2026, 2, 1));
    // Leap February 2024 -> [Feb 1, Mar 1); Feb 29 belongs to February.
    REQUIRE(stratforge::period_start(utc(2024, 2, 29, 12, 0, 0), month) == utc(2024, 2, 1));
    REQUIRE(stratforge::period_end(utc(2024, 2, 15), month) == utc(2024, 3, 1));
    // Dec 31 -> [Dec 1, Jan 1 next year).
    REQUIRE(stratforge::period_start(utc(2025, 12, 31, 23, 59, 59), month) == utc(2025, 12, 1));
    REQUIRE(stratforge::period_end(utc(2025, 12, 31, 23, 59, 59), month) == utc(2026, 1, 1));
}

TEST_CASE("period_end equals period_start of the next period", "[interval][regression]") {
    const auto month = must_parse("1M");
    const DateTime t = utc(2026, 6, 10, 14, 5, 0);
    const DateTime end = stratforge::period_end(t, month);
    // The design-doc invariant: period_end(t, 1M) == period_start(first
    // instant of the next month, 1M).
    REQUIRE(end == stratforge::period_start(utc(2026, 7, 1), month));
    // A boundary instant is its own period start.
    REQUIRE(stratforge::period_start(end, month) == end);

    const auto hour = must_parse("1h");
    REQUIRE(stratforge::period_end(utc(2026, 7, 12, 3, 37, 11), hour) ==
            stratforge::period_start(utc(2026, 7, 12, 4, 0, 0), hour));
}

TEST_CASE("period functions refuse non-vocabulary timeframes", "[interval][regression]") {
    const DateTime t = utc(2026, 7, 12);
    REQUIRE_THROWS_AS(stratforge::period_start(t, {TimeFrame::Years, 1}), std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::period_start(t, {TimeFrame::Weeks, 2}), std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::period_start(t, {TimeFrame::Months, 3}), std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::period_start(t, {TimeFrame::Minutes, 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::advance_periods(t, {TimeFrame::Seconds, 1}, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(stratforge::period_end(t, {TimeFrame::Tick, 1}), std::invalid_argument);
}

TEST_CASE("advance_periods steps months across year boundaries", "[interval][regression]") {
    const auto month = must_parse("1M");
    // Negative n across a year boundary.
    REQUIRE(stratforge::advance_periods(utc(2026, 1, 15, 10, 0, 0), month, -2) ==
            utc(2025, 11, 1));
    // Positive n across a year boundary.
    REQUIRE(stratforge::advance_periods(utc(2025, 11, 30), month, 3) == utc(2026, 2, 1));
    // Across the leap February (anchored to the grid, no end-of-month overflow).
    REQUIRE(stratforge::advance_periods(utc(2024, 1, 31), month, 1) == utc(2024, 2, 1));
    // n = 0 anchors to the period start.
    REQUIRE(stratforge::advance_periods(utc(2026, 1, 15, 10, 0, 0), month, 0) == utc(2026, 1, 1));
    // Round trip: -n then +n restores the boundary.
    const DateTime anchor = stratforge::advance_periods(utc(2026, 6, 9), month, -27);
    REQUIRE(anchor == utc(2024, 3, 1));
    REQUIRE(stratforge::advance_periods(anchor, month, 27) == utc(2026, 6, 1));
}

TEST_CASE("advance_periods steps weeks and fixed spans", "[interval][regression]") {
    const auto week = must_parse("1w");
    // Negative n across a year boundary lands on a Monday (2023-12-25).
    REQUIRE(stratforge::advance_periods(utc(2024, 1, 3, 15, 30, 0), week, -1) ==
            utc(2023, 12, 25));
    REQUIRE(stratforge::advance_periods(utc(2024, 1, 3), week, 2) == utc(2024, 1, 15));

    const auto hour = must_parse("1h");
    REQUIRE(stratforge::advance_periods(utc(2026, 7, 12, 3, 37, 11), hour, -3) ==
            utc(2026, 7, 12, 0, 0, 0));
    const auto day = must_parse("1d");
    // Fixed-span stepping across a year boundary.
    REQUIRE(stratforge::advance_periods(utc(2026, 1, 1, 5, 0, 0), day, -1) == utc(2025, 12, 31));
}

TEST_CASE("finer_than is a strict total order over the vocabulary", "[interval][regression]") {
    std::vector<TimeFrameCompression> ranked;
    ranked.reserve(kAllTokens.size());
    for (const auto token : kAllTokens) {
        ranked.push_back(must_parse(token));
    }
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        REQUIRE_FALSE(stratforge::finer_than(ranked[i], ranked[i])); // irreflexive
        for (std::size_t j = i + 1; j < ranked.size(); ++j) {
            REQUIRE(stratforge::finer_than(ranked[i], ranked[j]));
            REQUIRE_FALSE(stratforge::finer_than(ranked[j], ranked[i])); // asymmetric
        }
    }
}
