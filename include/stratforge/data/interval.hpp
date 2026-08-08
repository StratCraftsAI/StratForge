// SPDX-License-Identifier: MIT
//
// include/stratforge/data/interval.hpp — canonical interval vocabulary and
// UTC calendar period bucketing.
//
//  P0: one case-sensitive vocabulary shared TS <-> C++
// (`1m 5m 15m 30m 1h 2h 4h 1d 1w 1M`), plus the calendar boundary functions
// that the multi-clock alignment scheduler (P1), the runner warmup window
// extension (P2), and ResampledFeed (P1) build on.
//
// Design (why calendar functions, not `open + duration`):
// - Weeks and months have variable span (month length, leap years) and data
//   has session gaps. The only correct way to bound a weekly/monthly period
//   is calendar arithmetic on the UTC civil calendar.
// - Weeks anchor Monday 00:00 UTC; months anchor the 1st 00:00 UTC.
// - Fixed-span timeframes (minutes/hours/days) bucket by flooring against
//   the UTC epoch. Every fixed-span token in the vocabulary divides 24h
//   evenly and the epoch is midnight UTC, so epoch-anchored flooring is
//   identical to day-aligned flooring.
// - `period_end` is exclusive: it returns the first instant of the next
//   period, i.e. `period_end(t, tfc) == period_start(<next period>, tfc)`.

#pragma once

#include <stratforge/bar.hpp>
#include <stratforge/core/error.hpp>
#include <stratforge/data/timeframe.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string_view>

namespace stratforge {

/// Minutes per hour, derived from std::chrono (no magic numbers, ).
inline constexpr int kMinutesPerHour =
    static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(std::chrono::hours{1}).count());

/// One vocabulary entry: canonical token and its engine timeframe.
struct IntervalToken {
    std::string_view token;
    TimeFrameCompression tfc;
};

/// The canonical 10-token vocabulary, ordered finest -> coarsest.
/// Case-sensitive: `1m` is one minute, `1M` is one month.
inline constexpr std::array<IntervalToken, 10> kIntervalVocabulary{{
    {"1m", {TimeFrame::Minutes, 1}},
    {"5m", {TimeFrame::Minutes, 5}},
    {"15m", {TimeFrame::Minutes, 15}},
    {"30m", {TimeFrame::Minutes, 30}},
    {"1h", {TimeFrame::Minutes, kMinutesPerHour}},
    {"2h", {TimeFrame::Minutes, 2 * kMinutesPerHour}},
    {"4h", {TimeFrame::Minutes, 4 * kMinutesPerHour}},
    {"1d", {TimeFrame::Days, 1}},
    {"1w", {TimeFrame::Weeks, 1}},
    {"1M", {TimeFrame::Months, 1}},
}};

/// Parse a canonical interval token (case-sensitive, exact match).
/// `1m` -> {Minutes,1}, `1h` -> {Minutes,60}, `1d` -> {Days,1},
/// `1w` -> {Weeks,1}, `1M` -> {Months,1}.
[[nodiscard]] constexpr std::expected<TimeFrameCompression, DataError>
parse_interval(std::string_view token) noexcept {
    for (const auto& entry : kIntervalVocabulary) {
        if (entry.token == token) {
            return entry.tfc;
        }
    }
    return std::unexpected(DataError::InvalidInterval);
}

/// Round-trip inverse of parse_interval. Returns the canonical token for a
/// vocabulary timeframe, or an empty view for out-of-vocabulary values
/// (callers that require a token must treat empty as a contract violation).
[[nodiscard]] constexpr std::string_view to_string(TimeFrameCompression tfc) noexcept {
    for (const auto& entry : kIntervalVocabulary) {
        if (entry.tfc.timeframe == tfc.timeframe && entry.tfc.compression == tfc.compression) {
            return entry.token;
        }
    }
    return {};
}

/// Wall-clock span of a fixed-span timeframe (Minutes/Days).
///
/// Weeks and Months are EXCLUDED BY CONTRACT: they have no fixed span, and
/// callers must use period_start/period_end/advance_periods instead.
/// Calling with a variable-span (or non-positive compression) timeframe is a
/// caller bug and throws std::invalid_argument (fail fast, ).
[[nodiscard]] constexpr std::chrono::seconds duration_of(TimeFrameCompression tfc) {
    if (tfc.compression <= 0) {
        throw std::invalid_argument("duration_of: compression must be positive");
    }
    switch (tfc.timeframe) {
    case TimeFrame::Minutes:
        return std::chrono::minutes(tfc.compression);
    case TimeFrame::Days:
        return std::chrono::days(tfc.compression);
    default:
        throw std::invalid_argument(
            "duration_of: variable-span timeframe; use period_start/period_end");
    }
}

/// First instant of the period containing `t` (UTC calendar bucketing).
/// - Minutes/Days: floor against the UTC epoch (span divides 24h -> day-aligned).
/// - Weeks: Monday 00:00 UTC of the week containing `t` (compression 1 only).
/// - Months: the 1st 00:00 UTC of the month containing `t` (compression 1 only).
/// Any other timeframe (or invalid compression) throws std::invalid_argument.
[[nodiscard]] constexpr DateTime period_start(DateTime t, TimeFrameCompression tfc) {
    namespace chrono = std::chrono;
    switch (tfc.timeframe) {
    case TimeFrame::Minutes:
    case TimeFrame::Days: {
        const std::int64_t span = duration_of(tfc).count();
        const std::int64_t sec = chrono::floor<chrono::seconds>(t.time_since_epoch()).count();
        std::int64_t bucket = sec / span;
        if (sec % span != 0 && sec < 0) {
            --bucket; // floor division for pre-epoch instants
        }
        return DateTime(chrono::seconds(bucket * span));
    }
    case TimeFrame::Weeks: {
        if (tfc.compression != 1) {
            throw std::invalid_argument("period_start: weeks support compression 1 only");
        }
        const auto day = chrono::floor<chrono::days>(t);
        const chrono::weekday wd{day};
        return DateTime(day - (wd - chrono::Monday));
    }
    case TimeFrame::Months: {
        if (tfc.compression != 1) {
            throw std::invalid_argument("period_start: months support compression 1 only");
        }
        const chrono::year_month_day ymd{chrono::floor<chrono::days>(t)};
        return DateTime(chrono::sys_days{ymd.year() / ymd.month() / chrono::day{1}});
    }
    default:
        throw std::invalid_argument("period_start: timeframe outside the canonical vocabulary");
    }
}

/// Step `n` periods (n may be negative) on the period grid.
/// The result is always a period boundary: `t` is first anchored to
/// `period_start(t, tfc)`, then stepped calendar-safely (months step on the
/// civil calendar, so Jan 31 anchoring to Jan 1 stepping +1 yields Feb 1 —
/// no end-of-month overflow is possible). `advance_periods(t, tfc, 0)` is
/// exactly `period_start(t, tfc)`. Needed by the P2 warmup window extension.
[[nodiscard]] constexpr DateTime advance_periods(DateTime t, TimeFrameCompression tfc,
                                                 std::int64_t n) {
    namespace chrono = std::chrono;
    const DateTime start = period_start(t, tfc); // validates tfc (throws on contract violation)
    switch (tfc.timeframe) {
    case TimeFrame::Minutes:
    case TimeFrame::Days:
        return start + n * duration_of(tfc);
    case TimeFrame::Weeks:
        return start + chrono::weeks(n);
    case TimeFrame::Months: {
        const chrono::year_month_day ymd{chrono::floor<chrono::days>(start)};
        const chrono::year_month shifted = ymd.year() / ymd.month() + chrono::months(n);
        return DateTime(chrono::sys_days{shifted / chrono::day{1}});
    }
    default:
        throw std::invalid_argument("advance_periods: timeframe outside the canonical vocabulary");
    }
}

/// Exclusive end of the period containing `t`: the first instant of the next
/// period. Invariant: period_end(t, tfc) == period_start of the next period.
[[nodiscard]] constexpr DateTime period_end(DateTime t, TimeFrameCompression tfc) {
    return advance_periods(t, tfc, 1);
}

/// Canonical rank comparison: true iff `a` is a strictly finer timeframe
/// than `b`. Total order: TimeFrame enum order (Tick < Seconds < Minutes <
/// Days < Weeks < Months < Years) then compression. Over the vocabulary this
/// matches the kIntervalVocabulary finest -> coarsest ordering.
[[nodiscard]] constexpr bool finer_than(TimeFrameCompression a, TimeFrameCompression b) noexcept {
    if (a.timeframe != b.timeframe) {
        return static_cast<std::uint8_t>(a.timeframe) < static_cast<std::uint8_t>(b.timeframe);
    }
    return a.compression < b.compression;
}

} // namespace stratforge
