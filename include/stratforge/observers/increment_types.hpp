// SPDX-License-Identifier: MIT
//
// include/stratforge/observers/increment_types.hpp
//
// -A: POD wire contract for progressive backtest streaming.
//
// Defines the four PODs (TradeRecord, EquityPoint, MetricsSnapshot,
// IncrementSnapshot) consumed by 789-B's IncrementBatcher and the future
//  AsyncIncrementSink. Field reservations marked [live-compat]
// are unused by IncrementBatcher in backtest (set to safe defaults) but
// load-bearing for the live sink. See  §12 for the audit.

#pragma once

#include <stratforge/bar.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace stratforge {

/// Termination reason carried on the terminal `is_final` flush.
///
/// In backtest, only `Normal` is ever produced. The other variants are
/// reserved for the future live sink so the POD shape does
/// not need to change when live wiring lands.
enum class TerminationReason : std::uint8_t {
    Normal,         // backtest always uses this on is_final flush
    ConnectorLost,  // [live-only, ]
    RiskBreach,     // [live-only, ]
    ExternalStop,   // [live-only, ]
    FatalError,     // [live-only, ]
};

/// Flat POD projection of `broker::Trade` for wire-shape snapshots.
///
/// Projection rules (broker/trade.hpp ↔ TradeRecord):
///   trade_id     = Trade::id            (monotonic per backtest)
///   data_index   = Trade::data_index    (which feed this trade is on)
///   size         = Trade::size          (signed; 0 ⇒ fully closed)
///   entry_price  = Trade::entry_price
///   exit_price   = Trade::exit_price    (0.0 if still open)
///   commission   = Trade::commission    (total)
///   pnl          = Trade::pnl           (gross realized)
///   pnl_net      = Trade::pnlcomm       (net realized)
///   entry_bar    = Trade::entry_bar
///   exit_bar     = Trade::exit_bar      (0 if still open)
///   status       = static_cast<uint8_t>(Trade::status)  // 0=Open, 1=Closed
///
/// `Trade::barlen` is intentionally omitted (= exit_bar - entry_bar; cheap
/// to recompute downstream) to keep the POD compact.
struct TradeRecord {
    std::uint64_t  trade_id     = 0;
    std::size_t    data_index   = 0;
    double         size         = 0.0;
    double         entry_price  = 0.0;
    double         exit_price   = 0.0;
    double         commission   = 0.0;
    double         pnl          = 0.0;
    double         pnl_net      = 0.0;
    std::size_t    entry_bar    = 0;
    std::size_t    exit_bar     = 0;
    std::uint8_t   status       = 0;  // 0=Open, 1=Closed (mirrors broker::TradeStatus)
};

/// One equity sample per bar.
///
/// `timestamp` is the bar's system_clock time (NOT wall-clock-of-emit) so
/// downstream consumers can align equity points to bars deterministically.
/// `steady_clock` is reserved for in-process flush-interval gating only
/// and never crosses the POD/wire boundary.
struct EquityPoint {
    DateTime  timestamp{};
    double    portfolio_value = 0.0;
    double    cash            = 0.0;
};

/// Incrementally-cheap metrics computed per flush.
///
/// Full Sharpe / Sortino / detailed analytics stay in the `Analyzer`
/// final output — this struct is for live UI rendering, not for
/// replacing analyzers.
struct MetricsSnapshot {
    double         realized_pnl     = 0.0;  // sum over closed trades
    double         unrealized_pnl   = 0.0;  // portfolio_value − cash − initial_cash + closed_realized
    double         total_return_pct = 0.0;  // (portfolio_value / initial_cash − 1) × 100
    double         current_dd_pct   = 0.0;  // (peak − value) / peak × 100
    double         max_dd_pct       = 0.0;  // running maximum of current_dd_pct
    std::uint64_t  trade_count      = 0;    // closed trade count
};

/// A snapshot of all data accumulated since the previous flush.
///
/// `seq` is monotonically increasing within a single run (backtest or
/// live). Fields marked [live-compat] are unused by IncrementBatcher in
/// backtest (safe defaults) but load-bearing for the future live sink
///. See  §12 for the audit.
struct IncrementSnapshot {
    std::uint64_t                          seq             = 0;  // 1-indexed
    std::vector<Bar>                       new_bars;
    std::vector<TradeRecord>               new_trades;
    std::vector<EquityPoint>               new_equity_points;
    MetricsSnapshot                        current_metrics{};
    std::size_t                            processed_bars  = 0;
    std::optional<std::size_t>             total_bars;            // [live-compat] nullopt ⇒ unbounded stream
    bool                                   is_final        = false;
    std::optional<TerminationReason>       termination;           // [live-compat] set only when is_final
    std::uint32_t                          dropped_since_last_flush = 0;  // [live-compat] always 0 in backtest
};

static_assert(std::is_standard_layout_v<TradeRecord>);
static_assert(std::is_trivially_copyable_v<TradeRecord>);
static_assert(std::is_standard_layout_v<EquityPoint>);
static_assert(std::is_trivially_copyable_v<EquityPoint>);
static_assert(std::is_standard_layout_v<MetricsSnapshot>);
static_assert(std::is_trivially_copyable_v<MetricsSnapshot>);
// IncrementSnapshot is NOT trivially_copyable (vectors + optional) — by design.

}  // namespace stratforge
