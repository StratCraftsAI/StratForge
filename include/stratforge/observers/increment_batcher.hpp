// SPDX-License-Identifier: MIT
//
// include/stratforge/observers/increment_batcher.hpp
//
// -B: Built-in Observer that batches per-bar state into
// IncrementSnapshots and invokes a caller-supplied flush callback on
// configurable thresholds. Designed as the streaming seam between
// Cerebro::run() and out-of-process UI consumers (StratCraft desktop's
// stratforge-runner). See  §4 for the full contract.
//
// Flush thresholds (whichever fires first):
//   * max_bars_per_batch bars accumulated, OR
//   * max_interval wall-clock elapsed since the previous flush
//
// On stop(), a final flush is always emitted with is_final = true,
// termination = Normal — the terminal end-of-stream sentinel.
//
// Live-compat: the future  AsyncIncrementSink consumes the
// same IncrementSnapshot POD; the [live-compat] fields (total_bars,
// termination, dropped_since_last_flush) are populated here with safe
// backtest defaults. See  §12.

#pragma once

#include <stratforge/bar.hpp>
#include <stratforge/broker/broker.hpp>
#include <stratforge/broker/trade.hpp>
#include <stratforge/data/data_feed.hpp>
#include <stratforge/observers/increment_types.hpp>
#include <stratforge/observers/observer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace stratforge {

class IncrementBatcher final : public Observer {
public:
    struct Config {
        std::size_t                 max_bars_per_batch         = 500;
        std::chrono::milliseconds   max_interval               = std::chrono::milliseconds{100};
        bool                        emit_first_bar_immediately = true;
    };

    /// Flush callback receives the snapshot AND the current feed list so
    /// serializers can resolve symbol-by-data_index without re-plumbing.
    /// `feeds` is a non-owning view valid only for the duration of the call.
    ///  §4.3: keeps `symbol` off the POD while still letting the
    /// wire emit it.
    using FlushCallback = std::function<void(const IncrementSnapshot&,
                                             const std::vector<DataFeed*>&)>;

    IncrementBatcher(Config config, FlushCallback on_flush) noexcept
        : config_(config)
        , on_flush_(std::move(on_flush)) {}

    void start() override {
        // feeds_ are not available here (Observer::start takes no params).
        // total_bars latch + buffer reserves happen lazily on first next().
        last_flush_at_ = std::chrono::steady_clock::now();
    }

    void next(const BackBroker& broker, const std::vector<DataFeed*>& feeds) override {
        last_feeds_ = &feeds;
        if (!first_bar_seen_) {
            first_bar_seen_ = true;
            if (!feeds.empty() && feeds[0] != nullptr) {
                total_bars_ = feeds[0]->size();
            }
            pending_bars_.reserve(config_.max_bars_per_batch);
            pending_equity_.reserve(config_.max_bars_per_batch);
            initial_cash_ = broker.initial_cash();
        }

        if (!feeds.empty() && feeds[0] != nullptr) {
            pending_bars_.push_back(snapshot_current_bar_(*feeds[0]));
        }

        const double pv = broker.portfolio_value(feeds);
        const double cash = broker.cash();
        const DateTime ts = (!feeds.empty() && feeds[0] != nullptr)
                          ? feeds[0]->datetime()[0]
                          : DateTime{};

        // Running drawdown — mirrors analyzers/drawdown.hpp math; inlined
        // to avoid coupling Observer ordering to Analyzer ordering.
        // Computed BEFORE push so the per-point dd reaches EquityPoint.
        if (pv > peak_) {
            peak_ = pv;
        }
        const double dd = (peak_ > 0.0) ? ((peak_ - pv) / peak_ * 100.0) : 0.0;
        if (dd > max_dd_) {
            max_dd_ = dd;
        }
        current_dd_ = dd;
        last_portfolio_value_ = pv;
        last_cash_ = cash;

        pending_equity_.push_back(EquityPoint{ .timestamp       = ts,
                                               .portfolio_value = pv,
                                               .cash            = cash,
                                               .drawdown_pct    = dd });

        ++processed_bars_;
        maybe_flush_(/*force=*/false);
    }

    void notify_trade(const Trade& trade, double /*orig_size*/) override {
        pending_trades_.push_back(project_trade_(trade));
        if (trade.is_closed()) {
            closed_realized_pnl_ += trade.pnlcomm;
            ++closed_trade_count_;
            // Win/loss tally — exact 0.0 is neither (break-even).
            if (trade.pnlcomm > 0.0) {
                ++winning_count_;
            } else if (trade.pnlcomm < 0.0) {
                ++losing_count_;
            }
        }
    }

    void stop() override {
        emit_(/*is_final=*/true);
    }

private:
    void maybe_flush_(bool force) {
        const auto now = std::chrono::steady_clock::now();
        const bool first_force =
            config_.emit_first_bar_immediately && seq_ == 0 && !pending_bars_.empty();
        const bool by_count = pending_bars_.size() >= config_.max_bars_per_batch;
        const bool by_time  = (now - last_flush_at_) >= config_.max_interval;
        if (force || first_force || by_count || by_time) {
            emit_(/*is_final=*/false);
            last_flush_at_ = now;
        }
    }

    void emit_(bool is_final) {
        IncrementSnapshot snap{};
        snap.seq               = ++seq_;
        snap.new_bars          = std::move(pending_bars_);
        snap.new_trades        = std::move(pending_trades_);
        snap.new_equity_points = std::move(pending_equity_);
        snap.current_metrics   = current_metrics_();
        snap.processed_bars    = processed_bars_;
        snap.total_bars        = total_bars_;
        snap.is_final          = is_final;
        snap.termination       = is_final
                               ? std::optional<TerminationReason>{TerminationReason::Normal}
                               : std::nullopt;
        snap.dropped_since_last_flush = 0;  // backtest invariant

        pending_bars_.clear();
        pending_bars_.reserve(config_.max_bars_per_batch);
        pending_trades_.clear();
        pending_equity_.clear();
        pending_equity_.reserve(config_.max_bars_per_batch);

        //  §4.3: pass feeds so serializers can resolve
        // symbol-by-data_index. last_feeds_ is the most-recent ptr from
        // next(); on a zero-bar feed it remains null and the callback
        // receives an empty list.
        static const std::vector<DataFeed*> kEmpty{};
        const std::vector<DataFeed*>& feeds_ref =
            (last_feeds_ != nullptr) ? *last_feeds_ : kEmpty;
        on_flush_(snap, feeds_ref);  // may throw — propagates out of Cerebro::run() (§4.3)
    }

    [[nodiscard]] static Bar snapshot_current_bar_(const DataFeed& feed) noexcept {
        return Bar{
            .timestamp = feed.datetime()[0],
            .open      = feed.open()[0],
            .high      = feed.high()[0],
            .low       = feed.low()[0],
            .close     = feed.close()[0],
            .volume    = feed.volume()[0],
        };
    }

    [[nodiscard]] static TradeRecord project_trade_(const Trade& trade) noexcept {
        return TradeRecord{
            .trade_id    = static_cast<std::uint64_t>(trade.id),
            .data_index  = trade.data_index,
            .size        = trade.size,
            .entry_price = trade.entry_price,
            .exit_price  = trade.exit_price,
            .commission  = trade.commission,
            .pnl         = trade.pnl,
            .pnl_net     = trade.pnlcomm,
            .entry_bar   = trade.entry_bar,
            .exit_bar    = trade.exit_bar,
            .status      = static_cast<std::uint8_t>(trade.status),
            .entry_time  = trade.entry_time,
            .exit_time   = trade.exit_time,
        };
    }

    [[nodiscard]] MetricsSnapshot current_metrics_() const noexcept {
        MetricsSnapshot m{};
        m.realized_pnl     = closed_realized_pnl_;
        // unrealized = (current value − cash) − initial position cost; since
        // backtest starts flat, "value above cash that is not yet realized"
        // is the live open-position PnL. closed realized PnL is already in
        // cash, so subtracting it here would double-count.
        m.unrealized_pnl   = last_portfolio_value_ - last_cash_;
        m.total_return_pct = (initial_cash_ > 0.0)
                           ? ((last_portfolio_value_ / initial_cash_) - 1.0) * 100.0
                           : 0.0;
        m.current_dd_pct   = current_dd_;
        m.max_dd_pct       = max_dd_;
        m.trade_count      = closed_trade_count_;
        m.winning_count    = winning_count_;
        m.losing_count     = losing_count_;
        const std::uint64_t decided = winning_count_ + losing_count_;
        m.win_rate_pct     = (decided == 0)
                           ? 0.0
                           : (static_cast<double>(winning_count_) / static_cast<double>(decided)) * 100.0;
        return m;
    }

    Config                                  config_;
    FlushCallback                           on_flush_;

    std::uint64_t                           seq_                  = 0;
    std::size_t                             processed_bars_       = 0;
    std::optional<std::size_t>              total_bars_;
    std::vector<Bar>                        pending_bars_;
    std::vector<TradeRecord>                pending_trades_;
    std::vector<EquityPoint>                pending_equity_;
    std::chrono::steady_clock::time_point   last_flush_at_{};
    bool                                    first_bar_seen_       = false;
    const std::vector<DataFeed*>*           last_feeds_           = nullptr;

    double                                  initial_cash_         = 0.0;
    double                                  last_portfolio_value_ = 0.0;
    double                                  last_cash_            = 0.0;
    double                                  peak_                 = 0.0;
    double                                  current_dd_           = 0.0;
    double                                  max_dd_               = 0.0;
    double                                  closed_realized_pnl_  = 0.0;
    std::uint64_t                           closed_trade_count_   = 0;
    std::uint64_t                           winning_count_        = 0;
    std::uint64_t                           losing_count_         = 0;
};

}  // namespace stratforge
