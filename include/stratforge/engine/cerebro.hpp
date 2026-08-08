#pragma once

#include <stratforge/analyzers/analyzer.hpp>
#include <stratforge/broker/broker.hpp>
#include <stratforge/data/data_feed.hpp>
#include <stratforge/data/interval.hpp>
#include <stratforge/observers/observer.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace stratforge {

/// Cerebro - Central orchestrator for backtesting.
/// Manages data feeds, strategies, broker, analyzers, and observers.
class Cerebro {
public:
    struct RunOptions {
        bool runonce = false;
        bool preload = true;
    };

    Cerebro() = default;

    /// Add a data feed (takes ownership)
    void add_data(std::unique_ptr<DataFeed> feed, std::string name = {}) {
        if (!name.empty()) {
            feed->set_name(std::move(name));
        }
        data_feeds_.push_back(std::move(feed));
    }

    /// Add a strategy (takes ownership)
    void add_strategy(std::unique_ptr<Strategy> strategy) {
        strategies_.push_back(std::move(strategy));
    }

    /// Add a strategy by type with constructor args
    template <typename S, typename... Args>
    S& add_strategy(Args&&... args) {
        auto ptr = std::make_unique<S>(std::forward<Args>(args)...);
        auto& ref = *ptr;
        strategies_.push_back(std::move(ptr));
        return ref;
    }

    /// Add a strategy with parameter overrides
    template <typename S, typename... Args>
    S& add_strategy_with_params(ParamMap params, Args&&... args) {
        auto ptr = std::make_unique<S>(std::forward<Args>(args)...);
        ptr->set_params(std::move(params));
        auto& ref = *ptr;
        strategies_.push_back(std::move(ptr));
        return ref;
    }

    /// Add an analyzer (takes ownership)
    void add_analyzer(std::unique_ptr<Analyzer> analyzer) {
        analyzers_.push_back(std::move(analyzer));
    }

    /// Add an analyzer by type with constructor args
    template <typename A, typename... Args>
    A& add_analyzer(Args&&... args) {
        auto ptr = std::make_unique<A>(std::forward<Args>(args)...);
        auto& ref = *ptr;
        analyzers_.push_back(std::move(ptr));
        return ref;
    }

    /// Add an observer (takes ownership)
    void add_observer(std::unique_ptr<Observer> observer) {
        observers_.push_back(std::move(observer));
    }

    /// Add an observer by type
    template <typename O, typename... Args>
    O& add_observer(Args&&... args) {
        auto ptr = std::make_unique<O>(std::forward<Args>(args)...);
        auto& ref = *ptr;
        observers_.push_back(std::move(ptr));
        return ref;
    }

    /// Set initial cash
    void set_cash(double cash) { broker_.set_cash(cash); }

    /// Set commission scheme
    void set_commission(CommissionInfo info) { broker_.set_commission(info); }

    /// Set percentage-based slippage on the broker
    void set_slippage_perc(double perc, bool slip_open = true, bool slip_limit = true,
                           bool slip_match = true, bool slip_out = false) {
        broker_.set_slippage_perc(perc, slip_open, slip_limit, slip_match, slip_out);
    }

    /// Set fixed-point slippage on the broker
    void set_slippage_fixed(double fixed, bool slip_open = true, bool slip_limit = true,
                            bool slip_match = true, bool slip_out = false) {
        broker_.set_slippage_fixed(fixed, slip_open, slip_limit, slip_match, slip_out);
    }

    /// Get the broker
    [[nodiscard]] const BackBroker& broker() const noexcept { return broker_; }
    [[nodiscard]] BackBroker& broker() noexcept { return broker_; }

    ///  P5: timestamp (epoch ms) of the first next() bar — the bar
    /// where all feeds satisfied their warmup for the first strategy. Zero if
    /// run() has not been called or no strategy ever entered next().
    [[nodiscard]] int64_t warmup_end_timestamp_ms() const noexcept { return warmup_end_timestamp_ms_; }

    ///  P5: per-feed bar counts at the end of the run.
    /// Index 0 = master (execution) feed. Empty before run().
    [[nodiscard]] const std::vector<std::size_t>& final_bars_delivered() const noexcept { return final_bars_delivered_; }

    /// Run the backtest
    void run() {
        run({});
    }

    /// Run the backtest with explicit engine options.
    ///
    ///  P1: Multi-clock engine loop.
    /// - Feed 0 is the master (execution) feed; it drives the main loop.
    /// - Context feeds (1..N) advance via timestamp-gated alignment: a context
    ///   bar becomes visible only after its period has fully closed, measured
    ///   against the master bar's close time (no-lookahead contract).
    /// - Single-feed strategies are the degenerate case and produce identical
    ///   behaviour to the pre-P1 lockstep loop.
    void run(RunOptions options) {
        if (data_feeds_.empty()) return;
        if (!options.preload) {
            throw std::invalid_argument(
                "Cerebro currently requires preload=true; streaming execution is not implemented");
        }

        const std::size_t num_feeds = data_feeds_.size();

        // Build raw pointer vector for internal use
        std::vector<DataFeed*> feed_ptrs;
        feed_ptrs.reserve(num_feeds);
        for (auto& feed : data_feeds_) {
            feed_ptrs.push_back(feed.get());
        }

        // Phase 5 contract: runonce currently shares the same bar-by-bar engine
        // path as the validated preload=true execution model.
        static_cast<void>(options.runonce);

        // Preload all data feeds
        for (auto& feed : data_feeds_) {
            feed->preload();
        }

        // : feed 0 is master; num_bars = master feed size (not min).
        const std::size_t num_bars = data_feeds_[0]->size();
        if (num_bars == 0) return;

        // Validate plan order: no context feed may be strictly finer than
        // feed 0 (the execution/master feed). Equal timeframes are allowed
        // (multi-instrument same-frequency plans). Single-feed plans skip
        // validation (back-compat: legacy feeds may have the default TFC).
        if (num_feeds > 1) {
            const auto exec_tf = data_feeds_[0]->timeframe();
            for (std::size_t k = 1; k < num_feeds; ++k) {
                const auto ctx_tf = data_feeds_[k]->timeframe();
                if (finer_than(ctx_tf, exec_tf)) {
                    throw std::invalid_argument(
                        "Cerebro: context feed " + std::to_string(k) +
                        " is finer than feed 0 (execution); "
                        "the execution feed must be the finest or equal. "
                        "Got feed 0 = " + std::string(to_string(exec_tf)) +
                        ", feed " + std::to_string(k) + " = " +
                        std::string(to_string(ctx_tf)));
                }
            }
        }

        // Per-feed cursor tracking for gated context advance.
        // context_cursor_[k] = the index of the NEXT bar to deliver for feed k.
        // For feed 0 (master), the main loop advances directly.
        std::vector<std::size_t> context_cursor(num_feeds, 0);
        std::vector<std::size_t> bars_delivered(num_feeds, 0);
        std::vector<bool> feed_advanced_flags(num_feeds, false);

        // Feed 0 is always "delivered" via the master loop. Initialize its
        // cursor to 0 (first bar delivered on bar 0).
        // Context feeds start at cursor 0 with 0 bars delivered.

        // Check warmup feasibility: if any feed cannot satisfy its warmup
        // within its loaded data, fail fast.
        auto check_warmup_feasibility = [&]() {
            for (auto& strat : strategies_) {
                for (std::size_t k = 0; k < num_feeds; ++k) {
                    const std::size_t required = strat->minimum_period(k);
                    const std::size_t available = data_feeds_[k]->size();
                    if (required > available) {
                        const auto tf_str = to_string(data_feeds_[k]->timeframe());
                        throw std::invalid_argument(
                            "Cerebro: feed " + std::to_string(k) +
                            " (interval " + std::string(
                                tf_str.empty() ? "unknown" : tf_str) +
                            ") has " + std::to_string(available) +
                            " bars but warmup requires " +
                            std::to_string(required));
                    }
                }
            }
        };

        // Set up broker notifications -> strategy callbacks
        broker_.set_order_notify([this](const Order& order) {
            for (auto& strat : strategies_) {
                strat->notify_order(order);
            }
            for (auto& analyzer : analyzers_) {
                analyzer->notify_order(order);
            }
            for (auto& observer : observers_) {
                observer->notify_order(order);
            }
        });
        broker_.set_trade_notify([this](const Trade& trade, double orig_size) {
            for (auto& strat : strategies_) {
                strat->notify_trade(trade, orig_size);
            }
            for (auto& analyzer : analyzers_) {
                analyzer->notify_trade(trade, orig_size);
            }
            for (auto& observer : observers_) {
                observer->notify_trade(trade, orig_size);
            }
        });

        // Initialize strategies
        for (auto& strat : strategies_) {
            strat->set_broker(&broker_);
            strat->set_data_feeds(feed_ptrs);
            strat->ensure_params_initialized();
            strat->init();
            strat->start();
        }

        // Check warmup feasibility after strategies have set their min periods
        // (init() is where strategies typically call set_minimum_period).
        check_warmup_feasibility();

        // Start analyzers
        for (auto& analyzer : analyzers_) {
            analyzer->start();
        }

        // Start observers
        for (auto& observer : observers_) {
            observer->start();
        }

        // Track whether each strategy has entered the next() phase
        // (for multi-feed warmup: all feeds must satisfy their min_period).
        std::vector<bool> strat_entered_next(strategies_.size(), false);

        // Compute exec_close for the master feed. For fixed-span TFs this is
        // open + duration; for calendar TFs (weeks/months) it is period_end.
        const auto exec_tf = data_feeds_[0]->timeframe();
        const bool exec_tf_is_calendar =
            (exec_tf.timeframe == TimeFrame::Weeks ||
             exec_tf.timeframe == TimeFrame::Months);

        auto compute_exec_close = [&](std::size_t master_bar) -> DateTime {
            const DateTime bar_open = data_feeds_[0]->datetime()[0];
            if (num_feeds == 1) {
                // Single-feed: exec_close is never used for gating; return
                // a dummy to avoid calling period_end on an unset timeframe.
                return bar_open;
            }
            if (exec_tf_is_calendar) {
                return period_end(bar_open, exec_tf);
            }
            return bar_open + duration_of(exec_tf);
        };

        // Main loop: bar by bar on the master feed
        for (std::size_t bar = 0; bar < num_bars; ++bar) {
            broker_.set_bar_index(bar);

            // Reset per-bar flags
            std::fill(feed_advanced_flags.begin(), feed_advanced_flags.end(), false);

            // Feed 0 (master) always advances — record it
            feed_advanced_flags[0] = true;
            bars_delivered[0] = bar + 1;

            // Gated context-feed advance (feeds 1..N)
            if (num_feeds > 1) {
                const DateTime exec_close = compute_exec_close(bar);

                for (std::size_t k = 1; k < num_feeds; ++k) {
                    auto* ctx_feed = data_feeds_[k].get();
                    const auto ctx_tf = ctx_feed->timeframe();
                    const auto& ctx_dt = ctx_feed->datetime().data();
                    const std::size_t ctx_size = ctx_feed->size();

                    // Advance while the next context bar's period has fully
                    // closed by exec_close.
                    while (context_cursor[k] < ctx_size) {
                        // The bar at context_cursor[k] has period_end:
                        DateTime bar_period_end = period_end(
                            ctx_dt[context_cursor[k]], ctx_tf);

                        if (bar_period_end <= exec_close) {
                            // This context bar is now visible
                            if (bars_delivered[k] > 0) {
                                // Not the first bar: advance the feed cursor
                                ctx_feed->advance();
                            }
                            ++bars_delivered[k];
                            feed_advanced_flags[k] = true;
                            ++context_cursor[k];
                        } else {
                            break;
                        }
                    }
                }
            }

            // Propagate per-bar state to strategies
            for (auto& strat : strategies_) {
                strat->set_feed_advanced(feed_advanced_flags);
                strat->set_bars_delivered(bars_delivered);
            }

            // Process pending orders against current bar
            broker_.process_orders(feed_ptrs);

            // Call strategy lifecycle methods (multi-feed warmup)
            for (std::size_t si = 0; si < strategies_.size(); ++si) {
                auto& strat = strategies_[si];

                if (strat_entered_next[si]) {
                    strat->next();
                } else {
                    // Check if all feeds satisfy their warmup
                    bool all_ready = true;
                    for (std::size_t k = 0; k < num_feeds; ++k) {
                        if (bars_delivered[k] < strat->minimum_period(k)) {
                            all_ready = false;
                            break;
                        }
                    }

                    if (all_ready) {
                        strat_entered_next[si] = true;
                        //  P5: capture the timestamp of the first
                        // next() bar (warmup->next transition). Only record
                        // for the first strategy that enters next (si == 0 or
                        // warmup_end_timestamp_ms_ still unset).
                        if (warmup_end_timestamp_ms_ == 0) {
                            const auto& dt = data_feeds_[0]->datetime().data();
                            if (bar < dt.size()) {
                                warmup_end_timestamp_ms_ =
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                        dt[bar].time_since_epoch()).count();
                            }
                        }
                        strat->nextstart();
                    } else {
                        strat->prenext();
                    }
                }
            }

            // Update analyzers
            for (auto& analyzer : analyzers_) {
                analyzer->next(broker_, feed_ptrs);
            }

            // Update observers
            for (auto& observer : observers_) {
                observer->next(broker_, feed_ptrs);
            }

            // Advance master feed to next bar
            if (bar + 1 < num_bars) {
                data_feeds_[0]->advance();
            }
        }

        //  P5: persist per-feed bar counts for post-run access.
        final_bars_delivered_ = bars_delivered;

        // Stop strategies
        for (auto& strat : strategies_) {
            strat->stop();
        }

        // Stop analyzers
        for (auto& analyzer : analyzers_) {
            analyzer->stop();
        }

        // Stop observers
        for (auto& observer : observers_) {
            observer->stop();
        }
    }

private:
    std::vector<std::unique_ptr<DataFeed>> data_feeds_;
    std::vector<std::unique_ptr<Strategy>> strategies_;
    std::vector<std::unique_ptr<Analyzer>> analyzers_;
    std::vector<std::unique_ptr<Observer>> observers_;
    BackBroker broker_;

    ///  P5: timestamp (epoch ms) of the first next() bar.
    int64_t warmup_end_timestamp_ms_ = 0;
    ///  P5: per-feed bar counts at end of run().
    std::vector<std::size_t> final_bars_delivered_;
};

} // namespace stratforge
