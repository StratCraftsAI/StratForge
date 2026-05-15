// SPDX-License-Identifier: MIT
//
// include/stratforge/evaluation/signal_evaluator.hpp
//
//  P2: pure-function evaluation harness for SignalEntryStrategy.
//
// Drives initialize_indicators() once, then for each bar in the input span:
//   - calls update_indicators() + check_open_conditions(), records the signal
//   - advances the in-memory feed cursor by one
//
// Does NOT instantiate broker / sizer / portfolio / order book / analyzers,
// nor any IO-backed data feed. Bars are consumed from an InMemoryFeed the
// evaluator owns. See docs/tickets/.md for the full design contract.

#pragma once

#include <stratforge/bar.hpp>
#include <stratforge/data/in_memory_feed.hpp>
#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/signal_entry_strategy.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace stratforge::evaluation {

/// Pure-function evaluation harness for SignalEntryStrategy subclasses.
///
/// Output indexing: result[i] is the EntrySignal at bar i — bit-for-bit the
/// same signal Cerebro's per-bar loop sees when next() runs at bar i.
///
/// Warmup: indices [0, min_period - 1) are forced to EntrySignal{}. The first
/// bar where the strategy's real signal is recorded is min_period - 1, which
/// matches Cerebro's nextstart() boundary (bar + 1 == min_period). When
/// min_period == 1 (the default), no entries are forced neutral.
///
/// Reset: evaluate*() re-binds and preloads the feed and re-runs init() at
/// every entry — callers may reuse the evaluator across slices.
class SignalEvaluator {
public:
    explicit SignalEvaluator(SignalEntryStrategy& strategy)
        : strategy_(&strategy),
          feed_(std::make_unique<InMemoryFeed>(std::span<const Bar>{})) {}

    SignalEvaluator(const SignalEvaluator&)            = delete;
    SignalEvaluator& operator=(const SignalEvaluator&) = delete;
    SignalEvaluator(SignalEvaluator&&) noexcept            = default;
    SignalEvaluator& operator=(SignalEvaluator&&) noexcept = default;
    ~SignalEvaluator()                                     = default;

    /// Evaluate the strategy across the entire span. Output length == bars.size().
    [[nodiscard]] std::vector<EntrySignal>
    evaluate(std::span<const Bar> bars) {
        return evaluate_slice(bars, 0, bars.size());
    }

    /// Evaluate `bars` but only record output for indices [start, end). Indicators
    /// always see the full prefix from bar 0 so warmup state is correct on the
    /// recorded window. Output length == end - start.
    [[nodiscard]] std::vector<EntrySignal>
    evaluate_slice(std::span<const Bar> bars,
                   std::size_t start,
                   std::size_t end) {
        feed_->reset(bars);
        feed_->preload();
        strategy_->set_broker(nullptr);
        strategy_->set_data_feeds({feed_.get()});
        strategy_->ensure_params_initialized();
        // SignalEntryStrategy::init() is `final` and private; the public Strategy
        // base declares init() public/virtual, so we drive lifecycle init through
        // the base ref. Functionally identical: SignalEntryStrategy::init final
        // forwards to initialize_indicators().
        static_cast<Strategy*>(strategy_)->init();

        const std::size_t n          = bars.size();
        const std::size_t min_period = strategy_->minimum_period();

        std::vector<EntrySignal> out;
        out.reserve(end > start ? end - start : 0);

        for (std::size_t i = 0; i < n; ++i) {
            EntrySignal sig{};
            if (i + 1 >= min_period) {
                strategy_->update_indicators();
                sig = strategy_->check_open_conditions();
            }
            if (i >= start && i < end) {
                out.push_back(sig);
            }
            if (i + 1 < n) {
                feed_->advance();
            }
        }
        return out;
    }

private:
    SignalEntryStrategy*          strategy_;
    std::unique_ptr<InMemoryFeed> feed_;
};

}  // namespace stratforge::evaluation
