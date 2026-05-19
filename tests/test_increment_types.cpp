// SPDX-License-Identifier: MIT
//
// tests/test_increment_types.cpp
//
// -A acceptance suite for the IncrementSnapshot POD wire
// contract. Locks layout, default state, optional/enum defaults, and
// the TerminationReason enum value stability that downstream
// consumers (stratforge-runner JSON adapter, future  live
// sink) will rely on.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/observers/increment_types.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

using stratforge::Bar;
using stratforge::DateTime;
using stratforge::EquityPoint;
using stratforge::IncrementSnapshot;
using stratforge::MetricsSnapshot;
using stratforge::TerminationReason;
using stratforge::TradeRecord;

TEST_CASE("TradeRecord / EquityPoint / MetricsSnapshot are standard-layout PODs",
          "[observer][increment][regression]") {
    STATIC_REQUIRE(std::is_standard_layout_v<TradeRecord>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<TradeRecord>);
    STATIC_REQUIRE(std::is_default_constructible_v<TradeRecord>);
    STATIC_REQUIRE(std::is_aggregate_v<TradeRecord>);

    STATIC_REQUIRE(std::is_standard_layout_v<EquityPoint>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<EquityPoint>);
    STATIC_REQUIRE(std::is_default_constructible_v<EquityPoint>);
    STATIC_REQUIRE(std::is_aggregate_v<EquityPoint>);

    STATIC_REQUIRE(std::is_standard_layout_v<MetricsSnapshot>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<MetricsSnapshot>);
    STATIC_REQUIRE(std::is_default_constructible_v<MetricsSnapshot>);
    STATIC_REQUIRE(std::is_aggregate_v<MetricsSnapshot>);
}

TEST_CASE("IncrementSnapshot is default-constructible but not trivially copyable",
          "[observer][increment][regression]") {
    // By design: holds std::vector and std::optional members so the
    // wire envelope can grow without breaking PODs nested inside it.
    STATIC_REQUIRE(std::is_default_constructible_v<IncrementSnapshot>);
    STATIC_REQUIRE_FALSE(std::is_trivially_copyable_v<IncrementSnapshot>);
}

TEST_CASE("TradeRecord default-constructs to zeroed fields",
          "[observer][increment][regression]") {
    TradeRecord t{};

    REQUIRE(t.trade_id    == 0u);
    REQUIRE(t.data_index  == 0u);
    REQUIRE(t.size        == 0.0);
    REQUIRE(t.entry_price == 0.0);
    REQUIRE(t.exit_price  == 0.0);
    REQUIRE(t.commission  == 0.0);
    REQUIRE(t.pnl         == 0.0);
    REQUIRE(t.pnl_net     == 0.0);
    REQUIRE(t.entry_bar   == 0u);
    REQUIRE(t.exit_bar    == 0u);
    REQUIRE(t.status      == 0u);  // Open
}

TEST_CASE("EquityPoint default-constructs to epoch + zero",
          "[observer][increment][regression]") {
    EquityPoint p{};

    REQUIRE(p.timestamp       == DateTime{});
    REQUIRE(p.portfolio_value == 0.0);
    REQUIRE(p.cash            == 0.0);
}

TEST_CASE("MetricsSnapshot default-constructs to zeroed fields",
          "[observer][increment][regression]") {
    MetricsSnapshot m{};

    REQUIRE(m.realized_pnl     == 0.0);
    REQUIRE(m.unrealized_pnl   == 0.0);
    REQUIRE(m.total_return_pct == 0.0);
    REQUIRE(m.current_dd_pct   == 0.0);
    REQUIRE(m.max_dd_pct       == 0.0);
    REQUIRE(m.trade_count      == 0u);
}

TEST_CASE("IncrementSnapshot default-constructs to safe live-compat defaults",
          "[observer][increment][regression]") {
    IncrementSnapshot s{};

    REQUIRE(s.seq            == 0u);
    REQUIRE(s.new_bars.empty());
    REQUIRE(s.new_trades.empty());
    REQUIRE(s.new_equity_points.empty());
    REQUIRE(s.processed_bars == 0u);
    REQUIRE_FALSE(s.is_final);

    // Live-compat optional fields default to nullopt — this is the
    // marker that distinguishes "not yet populated" from "populated as
    // empty / Normal". 789-B's IncrementBatcher will set both on the
    // backtest terminal flush.
    REQUIRE_FALSE(s.total_bars.has_value());
    REQUIRE_FALSE(s.termination.has_value());
    REQUIRE(s.dropped_since_last_flush == 0u);
}

TEST_CASE("TerminationReason enum values are stable for wire compatibility",
          "[observer][increment][regression]") {
    // These values are part of the wire contract. Downstream JSON
    // consumers (stratforge-runner, the future  live sink)
    // will pin against them — never renumber.
    STATIC_REQUIRE(static_cast<std::uint8_t>(TerminationReason::Normal)         == 0u);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TerminationReason::ConnectorLost)  == 1u);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TerminationReason::RiskBreach)     == 2u);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TerminationReason::ExternalStop)   == 3u);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TerminationReason::FatalError)     == 4u);

    STATIC_REQUIRE(sizeof(TerminationReason) == 1u);
}

TEST_CASE("PODs round-trip through copy and equality of explicit fields",
          "[observer][increment][regression]") {
    using namespace std::chrono_literals;
    const DateTime t0 = DateTime{} + 3h;

    SECTION("TradeRecord copy preserves all fields") {
        const TradeRecord src{
            .trade_id    = 42,
            .data_index  = 1,
            .size        = -100.0,        // short
            .entry_price = 101.5,
            .exit_price  = 99.25,
            .commission  = 2.0,
            .pnl         = 225.0,
            .pnl_net     = 223.0,
            .entry_bar   = 50,
            .exit_bar    = 73,
            .status      = 1,             // Closed
        };
        const TradeRecord dst = src;

        REQUIRE(dst.trade_id    == 42u);
        REQUIRE(dst.data_index  == 1u);
        REQUIRE(dst.size        == -100.0);
        REQUIRE(dst.entry_price == 101.5);
        REQUIRE(dst.exit_price  == 99.25);
        REQUIRE(dst.commission  == 2.0);
        REQUIRE(dst.pnl         == 225.0);
        REQUIRE(dst.pnl_net     == 223.0);
        REQUIRE(dst.entry_bar   == 50u);
        REQUIRE(dst.exit_bar    == 73u);
        REQUIRE(dst.status      == 1u);
    }

    SECTION("EquityPoint copy preserves timestamp and values") {
        const EquityPoint src{ .timestamp = t0, .portfolio_value = 12345.67, .cash = 1000.0 };
        const EquityPoint dst = src;

        REQUIRE(dst.timestamp       == t0);
        REQUIRE(dst.portfolio_value == 12345.67);
        REQUIRE(dst.cash            == 1000.0);
    }

    SECTION("MetricsSnapshot copy preserves all fields") {
        const MetricsSnapshot src{
            .realized_pnl     = 500.0,
            .unrealized_pnl   = -75.0,
            .total_return_pct = 4.25,
            .current_dd_pct   = 1.5,
            .max_dd_pct       = 3.75,
            .trade_count      = 17,
        };
        const MetricsSnapshot dst = src;

        REQUIRE(dst.realized_pnl     == 500.0);
        REQUIRE(dst.unrealized_pnl   == -75.0);
        REQUIRE(dst.total_return_pct == 4.25);
        REQUIRE(dst.current_dd_pct   == 1.5);
        REQUIRE(dst.max_dd_pct       == 3.75);
        REQUIRE(dst.trade_count      == 17u);
    }
}

TEST_CASE("IncrementSnapshot carries vectors and live-compat fields under move",
          "[observer][increment][regression]") {
    using namespace std::chrono_literals;
    const DateTime t0 = DateTime{} + 1h;

    IncrementSnapshot src{};
    src.seq = 7;
    src.new_bars.push_back(Bar{ t0, 10.0, 11.0, 9.5, 10.5, 1000.0 });
    src.new_trades.push_back(TradeRecord{ .trade_id = 1, .size = 100.0 });
    src.new_equity_points.push_back(EquityPoint{ .timestamp = t0, .portfolio_value = 10500.0 });
    src.current_metrics.realized_pnl = 50.0;
    src.processed_bars = 1;
    src.total_bars     = std::optional<std::size_t>{1000};
    src.is_final       = true;
    src.termination    = TerminationReason::Normal;
    src.dropped_since_last_flush = 0;

    const IncrementSnapshot dst = std::move(src);

    REQUIRE(dst.seq                       == 7u);
    REQUIRE(dst.new_bars.size()           == 1u);
    REQUIRE(dst.new_trades.size()         == 1u);
    REQUIRE(dst.new_equity_points.size()  == 1u);
    REQUIRE(dst.new_trades[0].trade_id    == 1u);
    REQUIRE(dst.new_trades[0].size        == 100.0);
    REQUIRE(dst.new_bars[0].close         == 10.5);
    REQUIRE(dst.current_metrics.realized_pnl == 50.0);
    REQUIRE(dst.processed_bars            == 1u);
    REQUIRE(dst.total_bars.has_value());
    REQUIRE(*dst.total_bars               == 1000u);
    REQUIRE(dst.is_final);
    REQUIRE(dst.termination.has_value());
    REQUIRE(*dst.termination              == TerminationReason::Normal);
    REQUIRE(dst.dropped_since_last_flush  == 0u);
}

TEST_CASE("Live-compat fields support the unbounded-stream representation",
          "[observer][increment][regression]") {
    // The 789-A contract reserves these shapes for . Backtest
    // never produces them, but the POD must accept them today so the
    // wire shape is frozen.
    IncrementSnapshot s{};
    s.total_bars  = std::nullopt;                       // unbounded
    s.is_final    = true;
    s.termination = TerminationReason::ConnectorLost;   // abnormal
    s.dropped_since_last_flush = 12345;

    REQUIRE_FALSE(s.total_bars.has_value());
    REQUIRE(s.is_final);
    REQUIRE(s.termination.has_value());
    REQUIRE(*s.termination == TerminationReason::ConnectorLost);
    REQUIRE(s.dropped_since_last_flush == 12345u);
}
