// SPDX-License-Identifier: MIT
//
// tests/test_signal_evaluator.cpp — SignalEvaluator P2 smoke + P4 warmup tests.
//
// : covers §4.4 (no-broker safety), reset semantics, empty span,
// §4.2 walk-forward slice concatenation, §4.3 warmup boundary.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/bar.hpp>
#include <stratforge/evaluation/signal_evaluator.hpp>
#include <stratforge/strategy/signal_entry_strategy.hpp>

#include <chrono>
#include <cstddef>
#include <span>
#include <vector>

namespace {

using stratforge::Bar;
using stratforge::DateTime;
using stratforge::EntrySignal;
using stratforge::SignalEntryStrategy;
using stratforge::evaluation::SignalEvaluator;

inline DateTime epoch_plus_days(int days) {
    return DateTime{} + std::chrono::hours(24 * days);
}

inline std::vector<Bar> make_ramp_bars(std::size_t n, double base = 100.0) {
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

/// Always emits a long signal — useful for "no-broker safety" + reuse tests.
class AlwaysLongStrategy final : public SignalEntryStrategy {
public:
    void initialize_indicators() override { ++init_count; }
    [[nodiscard]] EntrySignal check_open_conditions() override {
        return {.long_signal = true};
    }
    [[nodiscard]] bool check_close_conditions() override { return false; }

    int init_count = 0;
};

/// Calls buy()/sell()/close() inside the hot path. With a null broker these
/// must short-circuit to 0 (Strategy.hpp:82/93/99 guards) — no crash.
class OrderCallingStrategy final : public SignalEntryStrategy {
public:
    void initialize_indicators() override {}
    [[nodiscard]] EntrySignal check_open_conditions() override {
        (void)buy();
        (void)sell();
        (void)close();
        return {.long_signal = data().close()[0] > 100.5};
    }
    [[nodiscard]] bool check_close_conditions() override { return false; }
};

/// Threshold-based strategy that exercises a warmup period > 1.
class WarmupThresholdStrategy final : public SignalEntryStrategy {
public:
    explicit WarmupThresholdStrategy(std::size_t warmup) : warmup_(warmup) {}

    void initialize_indicators() override {
        set_minimum_period(warmup_);
    }
    [[nodiscard]] EntrySignal check_open_conditions() override {
        return {.long_signal = data().close()[0] > 102.0};
    }
    [[nodiscard]] bool check_close_conditions() override { return false; }

private:
    std::size_t warmup_;
};

}  // namespace

TEST_CASE("SignalEvaluator evaluates always-long strategy across all bars",
          "[evaluation][smoke][regression]") {
    const auto bars = make_ramp_bars(10);
    AlwaysLongStrategy strat;
    SignalEvaluator eval(strat);

    const auto signals = eval.evaluate(std::span<const Bar>{bars});

    REQUIRE(signals.size() == bars.size());
    for (std::size_t i = 0; i < signals.size(); ++i) {
        INFO("bar index: " << i);
        CHECK(signals[i].long_signal);
        CHECK_FALSE(signals[i].short_signal);
    }
}

TEST_CASE("SignalEvaluator no-broker safety: buy/sell calls do not crash",
          "[evaluation][smoke][regression]") {
    const auto bars = make_ramp_bars(6);
    OrderCallingStrategy strat;
    SignalEvaluator eval(strat);

    const auto signals = eval.evaluate(std::span<const Bar>{bars});

    REQUIRE(signals.size() == bars.size());
    // close ramp: bar i close = 100 + i. Threshold 100.5: bar 0 (100) false,
    // bars 1..5 (101..105) all true.
    CHECK_FALSE(signals[0].long_signal);
    CHECK(signals[1].long_signal);
    CHECK(signals[2].long_signal);
    CHECK(signals[3].long_signal);
    CHECK(signals[4].long_signal);
    CHECK(signals[5].long_signal);
}

TEST_CASE("SignalEvaluator can be reused across multiple evaluate() calls",
          "[evaluation][smoke][regression]") {
    const auto bars = make_ramp_bars(8);
    AlwaysLongStrategy strat;
    SignalEvaluator eval(strat);

    const auto first  = eval.evaluate(std::span<const Bar>{bars});
    const auto second = eval.evaluate(std::span<const Bar>{bars});

    REQUIRE(first.size() == bars.size());
    REQUIRE(second.size() == bars.size());
    CHECK(first == second);
    // init() ran once per evaluate() call (reset semantics).
    CHECK(strat.init_count == 2);
}

TEST_CASE("SignalEvaluator output is empty for empty bar span",
          "[evaluation][smoke][regression]") {
    AlwaysLongStrategy strat;
    SignalEvaluator eval(strat);

    const auto signals = eval.evaluate(std::span<const Bar>{});
    CHECK(signals.empty());
}

TEST_CASE("SignalEvaluator slice concatenation equals full evaluate",
          "[evaluation][regression]") {
    const auto bars = make_ramp_bars(20);
    const std::size_t split_n = 7;
    const std::size_t split_m = 15;

    WarmupThresholdStrategy strat_full(3);
    WarmupThresholdStrategy strat_a(3);
    WarmupThresholdStrategy strat_b(3);
    SignalEvaluator eval_full(strat_full);
    SignalEvaluator eval_a(strat_a);
    SignalEvaluator eval_b(strat_b);

    const auto full = eval_full.evaluate(std::span<const Bar>{bars});
    const auto part_a = eval_a.evaluate_slice(std::span<const Bar>{bars}, 0, split_n);
    const auto part_b = eval_b.evaluate_slice(std::span<const Bar>{bars}, split_n, split_m);

    REQUIRE(full.size() == bars.size());
    REQUIRE(part_a.size() == split_n);
    REQUIRE(part_b.size() == split_m - split_n);

    std::vector<EntrySignal> concat;
    concat.reserve(split_m);
    concat.insert(concat.end(), part_a.begin(), part_a.end());
    concat.insert(concat.end(), part_b.begin(), part_b.end());

    for (std::size_t i = 0; i < split_m; ++i) {
        INFO("bar index: " << i);
        CHECK(concat[i] == full[i]);
    }
}

TEST_CASE("SignalEvaluator forces warmup neutral for min_period > 1",
          "[evaluation][regression]") {
    // close ramp: bar i close = 100+i. With threshold 102.0, the bar where
    // close first crosses 102 is bar i=3 (close=103). With warmup=5, bars 0..3
    // must be forced neutral and bar 4 is the first real signal recording.
    const auto bars = make_ramp_bars(8);
    WarmupThresholdStrategy strat(5);
    SignalEvaluator eval(strat);

    const auto signals = eval.evaluate(std::span<const Bar>{bars});

    REQUIRE(signals.size() == bars.size());
    for (std::size_t i = 0; i < 4; ++i) {
        INFO("forced-neutral bar index: " << i);
        CHECK(signals[i] == EntrySignal{});
    }
    // bar 4: close = 104 > 102, real signal is long.
    CHECK(signals[4].long_signal);
    CHECK(signals[5].long_signal);
    CHECK(signals[6].long_signal);
    CHECK(signals[7].long_signal);
}

TEST_CASE("SignalEvaluator default warmup (min_period=1) records bar 0 immediately",
          "[evaluation][regression]") {
    const auto bars = make_ramp_bars(3);
    AlwaysLongStrategy strat;
    SignalEvaluator eval(strat);

    const auto signals = eval.evaluate(std::span<const Bar>{bars});

    REQUIRE(signals.size() == bars.size());
    CHECK(strat.minimum_period() == 1);
    CHECK(signals[0].long_signal);   // not forced neutral
}
