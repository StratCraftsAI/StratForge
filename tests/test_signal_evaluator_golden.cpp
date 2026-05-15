// SPDX-License-Identifier: MIT
//
// tests/test_signal_evaluator_golden.cpp —  P3 Golden Master.
//
// Contract gate for §4.1: SignalEvaluator's per-bar EntrySignal output must
// match the signal series produced by Cerebro on the same strategy + bars.
//
// We compare *signal-emission* bars (when check_open_conditions returned
// any() == true), NOT broker fill bars. Cerebro fills market orders on the
// next bar, but signal generation happens on the current bar — the contract
// SignalEvaluator promises is "what the strategy decided at bar i", not
// "what the broker executed at bar i".

#include <catch2/catch_test_macros.hpp>

#include <stratforge/bar.hpp>
#include <stratforge/data/in_memory_feed.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/evaluation/signal_evaluator.hpp>
#include <stratforge/strategy/signal_entry_strategy.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace {

using stratforge::Bar;
using stratforge::Cerebro;
using stratforge::DateTime;
using stratforge::EntrySignal;
using stratforge::SignalEntryStrategy;
using stratforge::evaluation::SignalEvaluator;

inline DateTime epoch_plus_days(int days) {
    return DateTime{} + std::chrono::hours(24 * days);
}

/// Deterministic OHLCV ramp. close = 100 + i; high = close + 1; low = close - 1.
inline std::vector<Bar> make_synthetic_bars(std::size_t n, double base = 100.0) {
    std::vector<Bar> bars;
    bars.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double c = base + static_cast<double>(i);
        bars.push_back(Bar{
            epoch_plus_days(static_cast<int>(i)),
            c, c + 1.0, c - 1.0, c, 1000.0,
        });
    }
    return bars;
}

/// Strategy mixin that records (bar_index, EntrySignal) for every bar where
/// check_open_conditions runs (i.e. post-warmup). Bar index is derived from
/// an internal counter incremented in update_indicators(), which fires once
/// per bar in both Cerebro::next() and SignalEvaluator::evaluate_slice().
///
/// Subclasses implement signal_for(bar_index) — pure function of bar index so
/// Cerebro vs. SignalEvaluator decisions can't diverge for reasons other than
/// the harness itself.
class RecordingStrategy : public SignalEntryStrategy {
public:
    std::vector<std::pair<std::size_t, EntrySignal>> recorded;
    std::size_t bar_cursor = 0;
    bool        cursor_primed = false;

    void initialize_indicators() override {
        recorded.clear();
        bar_cursor    = 0;
        cursor_primed = false;
        configure();   // subclass hook (e.g. set_minimum_period)
    }

    void update_indicators() override {
        if (!cursor_primed) {
            // First update_indicators of the run corresponds to bar (min_period - 1).
            bar_cursor    = minimum_period() == 0 ? 0 : minimum_period() - 1;
            cursor_primed = true;
        } else {
            ++bar_cursor;
        }
    }

    [[nodiscard]] EntrySignal check_open_conditions() override {
        const EntrySignal sig = signal_for(bar_cursor);
        if (sig.any()) {
            recorded.emplace_back(bar_cursor, sig);
        }
        return sig;
    }

    [[nodiscard]] bool check_close_conditions() override { return false; }

protected:
    virtual void         configure() {}                                  // optional override
    virtual EntrySignal  signal_for(std::size_t bar_index) const = 0;
};

class AlwaysLongRecording final : public RecordingStrategy {
protected:
    EntrySignal signal_for(std::size_t) const override {
        return {.long_signal = true};
    }
};

class AlwaysShortRecording final : public RecordingStrategy {
protected:
    EntrySignal signal_for(std::size_t) const override {
        return {.short_signal = true};
    }
};

/// Real warmup > 1 — first signal recording is at bar (warmup - 1), and signals
/// alternate long/none so the comparison is non-degenerate.
class WarmupAlternatingRecording final : public RecordingStrategy {
public:
    explicit WarmupAlternatingRecording(std::size_t warmup) : warmup_(warmup) {}

protected:
    void configure() override { set_minimum_period(warmup_); }
    EntrySignal signal_for(std::size_t bar_index) const override {
        return {.long_signal = (bar_index % 2 == 0)};
    }

private:
    std::size_t warmup_;
};

/// Drive `strat` through Cerebro on `bars` and return the recorded signals.
template <typename StrategyT, typename... Args>
std::vector<std::pair<std::size_t, EntrySignal>>
collect_via_cerebro(std::span<const Bar> bars, Args&&... ctor_args) {
    Cerebro cerebro;
    cerebro.set_cash(100000.0);
    cerebro.add_data(std::make_unique<stratforge::InMemoryFeed>(bars));
    auto& strat = cerebro.add_strategy<StrategyT>(std::forward<Args>(ctor_args)...);
    cerebro.run();
    return strat.recorded;
}

/// Drive `strat` through SignalEvaluator on `bars` and return the same vector
/// shape as collect_via_cerebro for direct comparison.
template <typename StrategyT, typename... Args>
std::vector<std::pair<std::size_t, EntrySignal>>
collect_via_evaluator(std::span<const Bar> bars, Args&&... ctor_args) {
    StrategyT strat(std::forward<Args>(ctor_args)...);
    SignalEvaluator eval(strat);
    (void)eval.evaluate(bars);
    return strat.recorded;
}

}  // namespace

TEST_CASE("SignalEvaluator matches Cerebro signal bars: AlwaysLongRecording",
          "[evaluation][golden][regression]") {
    const auto bars = make_synthetic_bars(50);

    const auto cerebro_record = collect_via_cerebro<AlwaysLongRecording>(
        std::span<const Bar>{bars});
    const auto evaluator_record = collect_via_evaluator<AlwaysLongRecording>(
        std::span<const Bar>{bars});

    REQUIRE(cerebro_record.size() == bars.size());
    REQUIRE(evaluator_record == cerebro_record);
}

TEST_CASE("SignalEvaluator matches Cerebro signal bars: AlwaysShortRecording",
          "[evaluation][golden][regression]") {
    const auto bars = make_synthetic_bars(50);

    const auto cerebro_record = collect_via_cerebro<AlwaysShortRecording>(
        std::span<const Bar>{bars});
    const auto evaluator_record = collect_via_evaluator<AlwaysShortRecording>(
        std::span<const Bar>{bars});

    REQUIRE(cerebro_record.size() == bars.size());
    REQUIRE(evaluator_record == cerebro_record);
}

TEST_CASE("SignalEvaluator matches Cerebro signal bars: warmup > 1",
          "[evaluation][golden][regression]") {
    // 50 bars, warmup=5: first signal-eligible bar is bar 4 (== min_period-1).
    // Subsequent bars alternate long/none by bar_index parity.
    const auto bars = make_synthetic_bars(50);
    constexpr std::size_t warmup = 5;

    const auto cerebro_record = collect_via_cerebro<WarmupAlternatingRecording>(
        std::span<const Bar>{bars}, warmup);
    const auto evaluator_record = collect_via_evaluator<WarmupAlternatingRecording>(
        std::span<const Bar>{bars}, warmup);

    // Bars 4..49: parity-even bar indices emit long. That's bars {4,6,...,48} = 23 records.
    REQUIRE(cerebro_record.size() == 23);
    REQUIRE(evaluator_record == cerebro_record);

    // First record is bar 4 — proves the off-by-one warmup boundary (§9 Revision #2).
    REQUIRE(cerebro_record.front().first == 4);
}
