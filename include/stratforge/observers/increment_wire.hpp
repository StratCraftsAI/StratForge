// SPDX-License-Identifier: MIT
//
// include/stratforge/observers/increment_wire.hpp
//
// : Single source of truth for the `[INCREMENT_V2]` wire
// shape consumed by StratCraft Electron's ExecutorService. Hand-rolled
// because the runner has no JSON dep today and the snapshot shape is
// bounded per flush.
//
// The wire contract is fixed by:
//   apps/desktop/src/main/services/executor-service.ts (interface IncrementalResult)
//   plugins/quant-lab-nexus/.../hooks/mergeIncrement.ts (consumer side)
//
// Do NOT emit any POD-only field name here (portfolioValue / cash /
// realizedPnl / unrealizedPnl / currentDdPct / maxDdPct / tradeCount /
// tradeId / dataIndex / pnlNet / status / entryBar / exitBar / size).
// The POD is internal; the wire is the contract.

#pragma once

#include <stratforge/data/data_feed.hpp>
#include <stratforge/observers/increment_types.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace stratforge {

namespace detail {

/// Escape a string for safe embedding between JSON double quotes.
/// Handles the JSON-required set; sufficient for feed symbols and
/// the empty `"reason"` placeholder.
inline void json_escape_into(std::ostringstream& os, std::string_view s) {
    for (char ch : s) {
        switch (ch) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(ch)));
                    os << buf;
                } else {
                    os << ch;
                }
        }
    }
}

}  // namespace detail

/// Serialize a single IncrementSnapshot to the consumer-contract JSON.
/// `feeds` is required to resolve `symbol` by `data_index`; pass an empty
/// list if no feeds are available (symbol falls back to `""`).
[[nodiscard]] inline std::string snapshot_to_increment_v2_json(
        const IncrementSnapshot& s,
        const std::vector<DataFeed*>& feeds) {
    std::ostringstream os;
    os << std::setprecision(17);
    os << '{'
       << R"("seq":)" << s.seq
       << R"(,"isFinal":)" << (s.is_final ? "true" : "false")
       << R"(,"droppedSinceLastFlush":)" << s.dropped_since_last_flush
       << R"(,"processedBars":)" << s.processed_bars;
    os << R"(,"totalBars":)";
    if (s.total_bars.has_value()) {
        os << *s.total_bars;
    } else {
        os << "null";
    }

    os << R"(,"newCandles":[)";
    for (std::size_t i = 0; i < s.new_bars.size(); ++i) {
        const auto& b = s.new_bars[i];
        if (i) os << ',';
        os << '{'
           << R"("timestamp":)" << std::chrono::duration_cast<std::chrono::milliseconds>(
                  b.timestamp.time_since_epoch()).count()
           << R"(,"open":)"   << b.open
           << R"(,"high":)"   << b.high
           << R"(,"low":)"    << b.low
           << R"(,"close":)"  << b.close
           << R"(,"volume":)" << b.volume
           << '}';
    }
    os << ']';

    os << R"(,"newEquityPoints":[)";
    for (std::size_t i = 0; i < s.new_equity_points.size(); ++i) {
        const auto& e = s.new_equity_points[i];
        if (i) os << ',';
        os << '{'
           << R"("timestamp":)" << std::chrono::duration_cast<std::chrono::milliseconds>(
                  e.timestamp.time_since_epoch()).count()
           << R"(,"equity":)"   << e.portfolio_value
           << R"(,"drawdown":)" << e.drawdown_pct
           << '}';
    }
    os << ']';

    os << R"(,"newTrades":[)";
    for (std::size_t i = 0; i < s.new_trades.size(); ++i) {
        const auto& t = s.new_trades[i];
        if (i) os << ',';
        std::string_view symbol_sv;
        if (t.data_index < feeds.size() && feeds[t.data_index] != nullptr) {
            symbol_sv = feeds[t.data_index]->name();
        }
        const char* side = (t.size > 0.0) ? "long"
                          : (t.size < 0.0) ? "short" : "flat";
        const double quantity = std::abs(t.size);

        os << '{'
           << R"("entryTime":)" << std::chrono::duration_cast<std::chrono::milliseconds>(
                  t.entry_time.time_since_epoch()).count()
           << R"(,"exitTime":)" << std::chrono::duration_cast<std::chrono::milliseconds>(
                  t.exit_time.time_since_epoch()).count()
           << R"(,"symbol":")";
        detail::json_escape_into(os, symbol_sv);
        os << R"(","side":")" << side << R"(")"
           << R"(,"entryPrice":)" << t.entry_price
           << R"(,"exitPrice":)"  << t.exit_price
           << R"(,"commission":)" << t.commission
           << R"(,"pnl":)"        << t.pnl
           << R"(,"quantity":)"   << quantity
           << R"(,"reason":"")"
           << '}';
    }
    os << ']';

    const auto& m = s.current_metrics;
    const double total_pnl = m.realized_pnl + m.unrealized_pnl;
    os << R"(,"currentMetrics":{)"
       << R"("totalPnl":)"       << total_pnl
       << R"(,"totalReturn":)"   << m.total_return_pct
       << R"(,"totalTrades":)"   << m.trade_count
       << R"(,"winningTrades":)" << m.winning_count
       << R"(,"losingTrades":)"  << m.losing_count
       << R"(,"winRate":)"       << m.win_rate_pct
       << '}';

    os << '}';
    return os.str();
}

}  // namespace stratforge
