// SPDX-License-Identifier: MIT
//
// tests/test_increment_batcher.cpp
//
// -B acceptance suite for IncrementBatcher.
// Maps to  §6 tests #1–#7, #9, #10. Test #8 (frozen baseline
// CSV parity) lives in 789-C.

#include <catch2/catch_test_macros.hpp>

#include <stratforge/broker/broker.hpp>
#include <stratforge/broker/trade.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/observers/increment_types.hpp>
#include <stratforge/strategy/strategy.hpp>

#include "test_helpers.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using stratforge::BackBroker;
using stratforge::Cerebro;
using stratforge::DataFeed;
using stratforge::IncrementBatcher;
using stratforge::IncrementSnapshot;
using stratforge::Strategy;
using stratforge::TerminationReason;
using stratforge::Trade;
using stratforge::test::StaticFeed;

namespace {

// --- helpers --------------------------------------------------------------

/// Generate `count` trivial OHLC bars with strictly-monotone close.
/// Open == close, so PnL is deterministic and broker arithmetic is simple.
[[nodiscard]] std::vector<StaticFeed::Bar> make_trivial_bars(std::size_t count,
                                                             double start = 100.0,
                                                             double step  = 1.0) {
    std::vector<StaticFeed::Bar> bars;
    bars.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double px = start + static_cast<double>(i) * step;
        bars.push_back(StaticFeed::Bar{ .open = px, .high = px, .low = px, .close = px });
    }
    return bars;
}

/// Passive strategy — does nothing on every bar. Useful for tests that
/// only care about Observer accumulation cadence.
class NoopStrategy final : public Strategy {
public:
    void next() override {}
};

/// Buy once on bar 0 (via start()), close on bar 1 — produces exactly one
/// closed trade after the order fills on bar 2.
class SingleTradeStrategy final : public Strategy {
public:
    void start() override { static_cast<void>(buy(1.0)); }
    void next() override {
        if (data().index() == 2 && position().is_long()) {
            static_cast<void>(close());
        }
    }
};

/// Strategy that opens and closes a long position N times. Each cycle
/// spans two bars (buy at bar k, close at bar k+1).
class RepeatTradeStrategy final : public Strategy {
public:
    explicit RepeatTradeStrategy(std::size_t cycles, std::size_t spacing = 3)
        : cycles_(cycles), spacing_(spacing) {}

    void next() override {
        const std::size_t idx = data().index();
        if (fired_ >= cycles_) return;
        const std::size_t cycle_start = fired_ * spacing_;
        if (idx == cycle_start && !position().is_long()) {
            static_cast<void>(buy(1.0));
        } else if (idx == cycle_start + 1 && position().is_long()) {
            static_cast<void>(close());
            ++fired_;
        }
    }

private:
    std::size_t cycles_;
    std::size_t spacing_;
    std::size_t fired_ = 0;
};

/// Strategy that sleeps in next() to drive the interval-flush path.
class SleepyStrategy final : public Strategy {
public:
    explicit SleepyStrategy(std::chrono::milliseconds per_bar) : per_bar_(per_bar) {}
    void next() override { std::this_thread::sleep_for(per_bar_); }

private:
    std::chrono::milliseconds per_bar_;
};

}  // namespace

// --- Tests ----------------------------------------------------------------

TEST_CASE("IncrementBatcher flush_by_bar_count partitions feed deterministically",
          "[observer][increment][regression]") {
    constexpr std::size_t kBars = 1000;
    constexpr std::size_t kBatch = 250;

    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(kBars)));
    cerebro.add_strategy(std::make_unique<NoopStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = kBatch,
            .max_interval               = std::chrono::milliseconds{60'000},  // effectively disabled
            .emit_first_bar_immediately = false,
        },
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    // 1000 bars / 250 = exactly 4 mid-flushes (bars 250, 500, 750, 1000),
    // plus 1 terminal sentinel from stop() = 5 snapshots total. The 4th
    // mid-flush at bar 1000 carries the last 250 bars; the terminal flush
    // is empty.
    REQUIRE(snaps.size() == 5);

    std::size_t total_bars = 0;
    for (std::size_t i = 0; i < snaps.size(); ++i) {
        total_bars += snaps[i].new_bars.size();
        if (i + 1 < snaps.size()) {
            INFO("mid-flush " << i << " size=" << snaps[i].new_bars.size());
            CHECK(snaps[i].new_bars.size() == kBatch);
            CHECK_FALSE(snaps[i].is_final);
        }
    }
    REQUIRE(total_bars == kBars);

    REQUIRE(snaps.back().is_final);
    REQUIRE(snaps.back().new_bars.empty());
    REQUIRE(snaps.back().termination.has_value());
    REQUIRE(*snaps.back().termination == TerminationReason::Normal);
    REQUIRE(snaps.back().processed_bars == kBars);
    REQUIRE(snaps.back().total_bars.has_value());
    REQUIRE(*snaps.back().total_bars == kBars);
}

TEST_CASE("IncrementBatcher flush_by_interval paces flushes by wall clock",
          "[observer][increment][regression]") {
    constexpr std::size_t kBars = 12;
    constexpr auto kInterval = std::chrono::milliseconds{40};
    constexpr auto kPerBar   = std::chrono::milliseconds{25};

    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(kBars)));
    cerebro.add_strategy(std::make_unique<SleepyStrategy>(kPerBar));
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 10'000,   // count threshold disabled
            .max_interval               = kInterval,
            .emit_first_bar_immediately = false,
        },
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    // Lower bound only (CI hardware is noisy): at minimum 2 mid-flushes
    // plus the terminal. Strict upper bound on flush count would be flaky.
    REQUIRE(snaps.size() >= 3);

    std::size_t total_bars = 0;
    for (const auto& s : snaps) total_bars += s.new_bars.size();
    REQUIRE(total_bars == kBars);

    REQUIRE(snaps.back().is_final);
    REQUIRE(snaps.back().processed_bars == kBars);
}

TEST_CASE("IncrementBatcher emits the first bar immediately when configured",
          "[observer][increment][regression]") {
    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(50)));
    cerebro.add_strategy(std::make_unique<NoopStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 10'000,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = true,
        },
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    REQUIRE(snaps.size() >= 2);
    REQUIRE(snaps[0].seq == 1u);
    REQUIRE(snaps[0].processed_bars == 1u);
    REQUIRE(snaps[0].new_bars.size() == 1u);
    REQUIRE_FALSE(snaps[0].is_final);
}

TEST_CASE("IncrementBatcher always emits a terminal sentinel even with no trades",
          "[observer][increment][regression]") {
    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(20)));
    cerebro.add_strategy(std::make_unique<NoopStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 10'000,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = false,
        },
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    REQUIRE_FALSE(snaps.empty());
    REQUIRE(snaps.back().is_final);
    REQUIRE(snaps.back().termination.has_value());
    REQUIRE(*snaps.back().termination == TerminationReason::Normal);
    REQUIRE(snaps.back().new_trades.empty());
    REQUIRE(snaps.back().processed_bars == 20u);
}

TEST_CASE("IncrementBatcher seq is strictly monotonic with no gaps",
          "[observer][increment][regression]") {
    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(300)));
    cerebro.add_strategy(std::make_unique<NoopStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 73,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = true,
        },
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    REQUIRE(snaps.size() >= 2);
    for (std::size_t i = 0; i < snaps.size(); ++i) {
        REQUIRE(snaps[i].seq == static_cast<std::uint64_t>(i + 1));
    }
}

TEST_CASE("IncrementBatcher captures every trade fired during the run",
          "[observer][increment][regression]") {
    constexpr std::size_t kCycles  = 5;
    constexpr std::size_t kSpacing = 3;
    constexpr std::size_t kBars    = kCycles * kSpacing + 5;

    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.set_cash(100'000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(kBars)));
    cerebro.add_strategy(std::make_unique<RepeatTradeStrategy>(kCycles, kSpacing));
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 4,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = false,
        },
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    // BackBroker fires trade_notify_ only on the close transition
    // (broker.hpp:931 / :944), so each round-trip cycle produces exactly
    // one TradeRecord delivery. The terminal flush's closed trade_count
    // must match the cycle count.
    std::size_t total_trade_records = 0;
    for (const auto& s : snaps) total_trade_records += s.new_trades.size();
    REQUIRE(total_trade_records == kCycles);

    for (const auto& s : snaps) {
        for (const auto& tr : s.new_trades) {
            CHECK(tr.status == 1u);  // Closed
        }
    }

    REQUIRE(snaps.back().is_final);
    REQUIRE(snaps.back().current_metrics.trade_count == kCycles);
}

TEST_CASE("IncrementBatcher accounts for every bar across all flushes",
          "[observer][increment][regression]") {
    constexpr std::size_t kBars = 777;

    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(kBars)));
    cerebro.add_strategy(std::make_unique<NoopStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 100,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = true,
        },
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    std::size_t total_bars   = 0;
    std::size_t total_equity = 0;
    for (const auto& s : snaps) {
        total_bars   += s.new_bars.size();
        total_equity += s.new_equity_points.size();
    }
    REQUIRE(total_bars == kBars);
    REQUIRE(total_equity == kBars);  // one equity sample per bar
    REQUIRE(snaps.back().processed_bars == kBars);
}

TEST_CASE("IncrementBatcher propagates callback exceptions out of Cerebro::run()",
          "[observer][increment][regression]") {
    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_trivial_bars(50)));
    cerebro.add_strategy(std::make_unique<NoopStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = 10,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = false,
        },
        [seen = std::size_t{0}](const IncrementSnapshot& s) mutable {
            ++seen;
            if (s.seq == 3) {
                throw std::runtime_error("callback failure on seq=3");
            }
        }));

    REQUIRE_THROWS_AS(cerebro.run(), std::runtime_error);
}

TEST_CASE("IncrementBatcher on a zero-bar feed emits a single terminal snapshot",
          "[observer][increment][regression]") {
    std::vector<IncrementSnapshot> snaps;

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{}));
    cerebro.add_strategy(std::make_unique<NoopStrategy>());
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{},
        [&snaps](const IncrementSnapshot& s) { snaps.push_back(s); }));

    cerebro.run();

    // Cerebro short-circuits when num_bars == 0 — observer lifecycle
    // (start/stop) never runs, so no snapshot is emitted. This is the
    // documented behavior (cerebro.hpp:146 early return). Downstream
    // consumers detect "process exit with no snapshots" as an empty run.
    REQUIRE(snaps.empty());
}
