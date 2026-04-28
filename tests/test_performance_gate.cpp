// SPDX-License-Identifier: MIT
//
// tests/test_performance_gate.cpp — P50/P99 latency regression gates.
//
//  Phase B +  Issue 3.
// Release-only — Debug builds provide a single SUCCEED placeholder so that
// `./tests "[performance]"` exits cleanly (Catch2 v3 returns non-zero
// when ALL matched tests are SKIP'd).
//
// Thresholds loaded from `benchmarks/baselines.json` (single source of truth).
// CI gate rule: fail if P50 exceeds baseline * 1.10 (+10%) or
//               P99 exceeds baseline * 1.20 (+20%).
//
// Tag form: [performance][regression] (subset matches  spec).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#if !defined(NDEBUG)

// Debug builds: single passing placeholder prevents non-zero exit code.
TEST_CASE("Performance gates - Debug build placeholder", "[performance]") {
    SUCCEED("Performance gates compiled out in Debug - nothing to run");
}

#else  // Release build — real performance gates below

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>

#include "baseline_loader.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using stratforge::test::generate_sine_data;
using stratforge::test::make_line;
using stratforge::test::StaticFeed;

namespace {

// Relative regression multipliers (CLAUDE.md spec).
constexpr double kP50RegressionFactor = 1.10;  // +10%
constexpr double kP99RegressionFactor = 1.20;  // +20%

using clock_type = std::chrono::steady_clock;

[[nodiscard]] inline std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock_type::now().time_since_epoch())
            .count());
}

struct LatencyStats {
    std::uint64_t p50_ns = 0;
    std::uint64_t p99_ns = 0;
    std::uint64_t min_ns = 0;
    std::uint64_t max_ns = 0;
    std::size_t samples = 0;
};

[[nodiscard]] LatencyStats summarize(std::vector<std::uint64_t>& samples) {
    LatencyStats stats{};
    if (samples.empty()) {
        return stats;
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t n = samples.size();
    stats.samples = n;
    stats.min_ns = samples.front();
    stats.max_ns = samples.back();
    stats.p50_ns = samples[n / 2];
    stats.p99_ns = samples[std::min(n - 1, (n * 99) / 100)];
    return stats;
}

constexpr std::size_t kWarmupIters = 1024;
constexpr std::size_t kMeasureIters = 10000;

const std::string kSourceDir = SF_SOURCE_DIR;

class SmaCrossoverPerfStrategy final : public stratforge::Strategy {
public:
    void init() override {
        fast_ = std::make_unique<stratforge::SMA>(data().close(), 10);
        slow_ = std::make_unique<stratforge::SMA>(data().close(), 30);
    }

    void next() override {
        if (fast_->line().size() == 0 || slow_->line().size() == 0) return;
        const double f = fast_->line()[0];
        const double s = slow_->line()[0];
        if (!position().size && f > s) {
            (void)buy(1.0);
        } else if (position().size > 0 && f < s) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::SMA> fast_;
    std::unique_ptr<stratforge::SMA> slow_;
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

}  // namespace

TEST_CASE("SMA(30) per-bar latency gate", "[performance][regression][indicator]") {
    const auto baselines = stratforge::test::load_baselines(kSourceDir);
    const auto bl = stratforge::test::find_baseline(baselines, "SMA(30) per-bar");
    REQUIRE(bl.has_value());

    constexpr std::size_t period = 30;
    constexpr std::size_t bar_count = kWarmupIters + kMeasureIters + period;

    auto data = generate_sine_data(bar_count);
    auto source = make_line(data);
    stratforge::SMA sma(source, period);

    // Warmup (populate I-cache, branch predictor)
    for (std::size_t i = 0; i < kWarmupIters; ++i) {
        sma.next();
        if (i + 1 < source.size()) source.advance();
    }

    std::vector<std::uint64_t> samples;
    samples.reserve(kMeasureIters);
    for (std::size_t i = 0; i < kMeasureIters; ++i) {
        const auto t0 = now_ns();
        sma.next();
        const auto t1 = now_ns();
        samples.push_back(t1 - t0);
        if (kWarmupIters + i + 1 < source.size()) source.advance();
    }

    const auto stats = summarize(samples);
    const auto p50_limit = static_cast<std::uint64_t>(
        static_cast<double>(bl->p50_max_ns) * kP50RegressionFactor);
    const auto p99_limit = static_cast<std::uint64_t>(
        static_cast<double>(bl->p99_max_ns) * kP99RegressionFactor);

    INFO("SMA(30) p50=" << stats.p50_ns << "ns (limit=" << p50_limit
         << "ns, baseline=" << bl->p50_max_ns << "ns +10%)"
         << " p99=" << stats.p99_ns << "ns (limit=" << p99_limit
         << "ns, baseline=" << bl->p99_max_ns << "ns +20%)"
         << " min=" << stats.min_ns << "ns max=" << stats.max_ns
         << "ns samples=" << stats.samples);
    CHECK(stats.p50_ns < p50_limit);
    CHECK(stats.p99_ns < p99_limit);
}

TEST_CASE("BollingerBands per-bar latency gate", "[performance][regression][indicator]") {
    const auto baselines = stratforge::test::load_baselines(kSourceDir);
    const auto bl = stratforge::test::find_baseline(baselines, "BollingerBands(20,2) per-bar");
    REQUIRE(bl.has_value());

    constexpr std::size_t period = 20;
    constexpr std::size_t bar_count = kWarmupIters + kMeasureIters + period;

    auto data = generate_sine_data(bar_count);
    auto source = make_line(data);
    stratforge::BollingerBands bb(source, period, 2.0);

    for (std::size_t i = 0; i < kWarmupIters; ++i) {
        bb.next();
        if (i + 1 < source.size()) source.advance();
    }

    std::vector<std::uint64_t> samples;
    samples.reserve(kMeasureIters);
    for (std::size_t i = 0; i < kMeasureIters; ++i) {
        const auto t0 = now_ns();
        bb.next();
        const auto t1 = now_ns();
        samples.push_back(t1 - t0);
        if (kWarmupIters + i + 1 < source.size()) source.advance();
    }

    const auto stats = summarize(samples);
    const auto p50_limit = static_cast<std::uint64_t>(
        static_cast<double>(bl->p50_max_ns) * kP50RegressionFactor);
    const auto p99_limit = static_cast<std::uint64_t>(
        static_cast<double>(bl->p99_max_ns) * kP99RegressionFactor);

    INFO("BollingerBands(20,2) p50=" << stats.p50_ns << "ns (limit=" << p50_limit
         << "ns, baseline=" << bl->p50_max_ns << "ns +10%)"
         << " p99=" << stats.p99_ns << "ns (limit=" << p99_limit
         << "ns, baseline=" << bl->p99_max_ns << "ns +20%)"
         << " min=" << stats.min_ns << "ns max=" << stats.max_ns << "ns");
    CHECK(stats.p50_ns < p50_limit);
    CHECK(stats.p99_ns < p99_limit);
}

TEST_CASE("Cerebro full backtest latency gate", "[performance][regression][engine]") {
    const auto baselines = stratforge::test::load_baselines(kSourceDir);
    const auto bl = stratforge::test::find_baseline(baselines, "Cerebro 10K-bar full backtest");
    REQUIRE(bl.has_value());

    constexpr std::size_t bar_count = 10000;

    // Warmup run (populate caches, JIT branch predictor)
    {
        stratforge::Cerebro warm;
        warm.set_cash(100000.0);
        warm.add_data(std::make_unique<StaticFeed>(make_bars(bar_count)));
        warm.add_strategy<SmaCrossoverPerfStrategy>();
        warm.run();
    }

    stratforge::Cerebro cerebro;
    cerebro.set_cash(100000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(make_bars(bar_count)));
    cerebro.add_strategy<SmaCrossoverPerfStrategy>();

    const auto t0 = now_ns();
    cerebro.run();
    const auto t1 = now_ns();
    const std::uint64_t elapsed_ns = t1 - t0;
    const std::uint64_t per_bar_ns = elapsed_ns / bar_count;

    const auto total_limit = static_cast<std::uint64_t>(
        static_cast<double>(bl->total_max_ns) * kP50RegressionFactor);
    const auto per_bar_limit = static_cast<std::uint64_t>(
        static_cast<double>(bl->per_bar_max_ns) * kP50RegressionFactor);

    INFO("Cerebro 10K-bar full backtest: total=" << elapsed_ns
         << "ns (limit=" << total_limit << "ns, baseline=" << bl->total_max_ns << "ns +10%)"
         << " per_bar=" << per_bar_ns
         << "ns (limit=" << per_bar_limit << "ns, baseline=" << bl->per_bar_max_ns << "ns +10%)");
    CHECK(elapsed_ns < total_limit);
    CHECK(per_bar_ns < per_bar_limit);
}

TEST_CASE("Strategy next() per-bar latency gate", "[performance][regression][strategy]") {
    const auto baselines = stratforge::test::load_baselines(kSourceDir);
    const auto bl = stratforge::test::find_baseline(baselines, "Strategy SMA-crossover per-bar (end-to-end)");
    REQUIRE(bl.has_value());

    constexpr std::size_t bar_count = 5000;

    // Warmup
    {
        stratforge::Cerebro warm;
        warm.set_cash(100000.0);
        warm.add_data(std::make_unique<StaticFeed>(make_bars(bar_count)));
        warm.add_strategy<SmaCrossoverPerfStrategy>();
        warm.run();
    }

    stratforge::Cerebro cerebro;
    cerebro.set_cash(100000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(make_bars(bar_count)));
    cerebro.add_strategy<SmaCrossoverPerfStrategy>();

    const auto t0 = now_ns();
    cerebro.run();
    const auto t1 = now_ns();
    const std::uint64_t per_bar_ns = (t1 - t0) / bar_count;

    const auto per_bar_limit = static_cast<std::uint64_t>(
        static_cast<double>(bl->per_bar_max_ns) * kP50RegressionFactor);

    INFO("SMA-crossover strategy per-bar: " << per_bar_ns
         << "ns (limit=" << per_bar_limit << "ns, baseline=" << bl->per_bar_max_ns << "ns +10%)");
    CHECK(per_bar_ns < per_bar_limit);
}

#endif  // NDEBUG
