// SPDX-License-Identifier: MIT
//
// tests/test_performance_increment_batcher.cpp
//
// -B §7 performance gate for IncrementBatcher.
//
// Release-only — Debug builds compile to a SUCCEED placeholder so
// `./stratforge_tests "[performance]"` exits cleanly (Catch2 v3 returns
// non-zero if all matched tests are SKIP'd).
//
// Methodology: drive a Cerebro backtest of N bars with the batcher
// attached to a noop callback, then re-run the same backtest without
// the batcher. The per-bar accumulation budget is the difference
// (with − without) divided by N. Single-shot amortized measurement
// matches the existing "Cerebro 10K-bar full backtest" baseline pattern
// in test_performance_gate.cpp.
//
// Two gates:
//   * `IncrementBatcher per-bar accumulation` — per-bar delta budget
//   * `IncrementBatcher flush dispatch`       — per-flush overhead

#include <catch2/catch_test_macros.hpp>

#if !defined(NDEBUG)

TEST_CASE("IncrementBatcher performance gates - Debug build placeholder",
          "[performance]") {
    SUCCEED("Performance gates compiled out in Debug — nothing to run");
}

#else  // Release build

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/strategy/strategy.hpp>

#include "baseline_loader.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using stratforge::Cerebro;
using stratforge::IncrementBatcher;
using stratforge::IncrementSnapshot;
using stratforge::Strategy;
using stratforge::test::generate_sine_data;
using stratforge::test::StaticFeed;

namespace {

const std::string kSourceDir = SF_SOURCE_DIR;

class PerfNoopStrategy final : public Strategy {
public:
    void next() override {}
};

[[nodiscard]] std::vector<StaticFeed::Bar> make_bars(std::size_t n) {
    std::vector<StaticFeed::Bar> bars;
    bars.reserve(n);
    auto closes = generate_sine_data(n, 100.0, 5.0, 0.05);
    for (std::size_t i = 0; i < n; ++i) {
        const double c = closes[i];
        bars.push_back({c - 0.5, c + 0.5, c - 1.0, c});
    }
    return bars;
}

[[nodiscard]] std::uint64_t run_backtest_ns(std::size_t bar_count,
                                            bool with_batcher,
                                            std::size_t max_bars_per_batch,
                                            std::size_t* out_flush_count = nullptr) {
    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_bars(bar_count)));
    cerebro.add_strategy(std::make_unique<PerfNoopStrategy>());
    std::size_t flushes = 0;
    if (with_batcher) {
        cerebro.add_observer(std::make_unique<IncrementBatcher>(
            IncrementBatcher::Config{
                .max_bars_per_batch         = max_bars_per_batch,
                .max_interval               = std::chrono::milliseconds{60'000},
                .emit_first_bar_immediately = false,
            },
            [&flushes](const IncrementSnapshot&, const std::vector<stratforge::DataFeed*>&) { ++flushes; }));
    }

    const auto t0 = std::chrono::steady_clock::now();
    cerebro.run();
    const auto t1 = std::chrono::steady_clock::now();

    if (out_flush_count) *out_flush_count = flushes;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

}  // namespace

TEST_CASE("IncrementBatcher per-bar accumulation budget",
          "[performance][regression][observer]") {
    const auto baselines = stratforge::test::load_baselines(kSourceDir);
    const auto bl = stratforge::test::find_baseline(
        baselines, "IncrementBatcher per-bar accumulation");
    REQUIRE(bl.has_value());

    constexpr std::size_t kBars  = 10'000;
    constexpr std::size_t kBatch = 10'000;  // single mid-flush + terminal

    // Warmup run discards I-cache / branch-predictor cold starts.
    (void)run_backtest_ns(kBars, /*with_batcher=*/true,  kBatch);
    (void)run_backtest_ns(kBars, /*with_batcher=*/false, kBatch);

    const std::uint64_t with_ns    = run_backtest_ns(kBars, true,  kBatch);
    const std::uint64_t without_ns = run_backtest_ns(kBars, false, kBatch);

    REQUIRE(with_ns > without_ns);
    const std::uint64_t delta_ns       = with_ns - without_ns;
    const std::uint64_t per_bar_delta  = delta_ns / kBars;

    INFO("bars=" << kBars
         << " with_ns=" << with_ns
         << " without_ns=" << without_ns
         << " delta_ns=" << delta_ns
         << " per_bar_delta_ns=" << per_bar_delta
         << " budget_p99=" << bl->p99_max_ns);
    CHECK(per_bar_delta <= bl->p99_max_ns);
}

TEST_CASE("IncrementBatcher flush dispatch budget",
          "[performance][regression][observer]") {
    const auto baselines = stratforge::test::load_baselines(kSourceDir);
    const auto bl = stratforge::test::find_baseline(
        baselines, "IncrementBatcher flush dispatch");
    REQUIRE(bl.has_value());

    constexpr std::size_t kBars  = 10'000;
    constexpr std::size_t kBatch = 100;  // ~100 mid-flushes + terminal

    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(make_bars(kBars)));
    cerebro.add_strategy(std::make_unique<PerfNoopStrategy>());

    std::size_t flushes = 0;
    cerebro.add_observer(std::make_unique<IncrementBatcher>(
        IncrementBatcher::Config{
            .max_bars_per_batch         = kBatch,
            .max_interval               = std::chrono::milliseconds{60'000},
            .emit_first_bar_immediately = false,
        },
        [&flushes](const IncrementSnapshot&, const std::vector<stratforge::DataFeed*>&) {
            // Body kept trivially cheap — we are budgeting the batcher's
            // per-flush overhead, not whatever the caller does with the
            // snapshot.
            ++flushes;
        }));

    // Warmup run discards I-cache / branch-predictor cold starts.
    (void)run_backtest_ns(kBars, /*with_batcher=*/true, kBatch);

    const auto t0 = std::chrono::steady_clock::now();
    cerebro.run();
    const auto t1 = std::chrono::steady_clock::now();
    const std::uint64_t with_batcher_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    // Subtract the per-bar accumulation cost (already-gated 200 ns
    // budget) to isolate flush dispatch. Use the without-batcher run as
    // the per-bar floor; the residual that scales with flush count is
    // dispatch.
    const std::uint64_t without_ns = run_backtest_ns(kBars, false, kBatch);
    REQUIRE(flushes > 0);
    REQUIRE(with_batcher_ns > without_ns);

    // Total batcher overhead = per-bar accumulation × kBars + per-flush × flushes.
    // We can't perfectly disentangle, so we attribute the FULL overhead to
    // flushes — a strict upper bound. If even that upper bound fits the
    // budget, the real per-flush cost necessarily does too.
    const std::uint64_t batcher_overhead_ns = with_batcher_ns - without_ns;
    const std::uint64_t per_flush_upper_bound_ns = batcher_overhead_ns / flushes;

    INFO("bars=" << kBars
         << " flushes=" << flushes
         << " with_batcher_ns=" << with_batcher_ns
         << " without_ns=" << without_ns
         << " batcher_overhead_ns=" << batcher_overhead_ns
         << " per_flush_upper_bound_ns=" << per_flush_upper_bound_ns
         << " budget_p99=" << bl->p99_max_ns);
    CHECK(per_flush_upper_bound_ns <= bl->p99_max_ns);
}

#endif  // NDEBUG
