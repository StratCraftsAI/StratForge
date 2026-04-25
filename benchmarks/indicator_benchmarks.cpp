#include "benchmark_utils.hpp"

#include <stratforge/data/csv_data.hpp>
#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/directionalmovement.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/ichimoku.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/indicators/trend.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

using namespace stratforge::bench;

stratforge::CsvData load_feed(const std::string& relative_path) {
    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(relative_path),
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });

    if (!feed.load()) {
        throw std::runtime_error("Unable to load benchmark dataset: " + relative_path);
    }

    return feed;
}

template <typename Indicator>
void run_indicator(stratforge::CsvData& feed, Indicator& indicator) {
    for (std::size_t i = 0; i < feed.size(); ++i) {
        indicator.next();
        if (i + 1 < feed.size()) {
            feed.advance();
        }
    }
}

// --- Whole-run benchmark: load data ONCE, clone per iteration ---------

template <typename IndicatorFactory>
SampleSummary benchmark_indicator_run(const std::string& label,
                                      stratforge::CsvData& preloaded_feed,
                                      std::size_t iterations,
                                      IndicatorFactory&& make_indicator,
                                      JsonReport& report) {
    const auto bars = preloaded_feed.size();

    // Clone feed for each iteration -- no disk I/O in timing loop
    const auto summary = run_benchmark(iterations, [&]() {
        auto feed_ptr = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
        feed.load();
        auto indicator = make_indicator(feed);
        run_indicator(feed, indicator);
    });

    print_summary(label, summary, iterations, bars);

    report.add({.name = label + " (whole-run)",
                .summary = summary,
                .bars_per_iteration = bars});

    return summary;
}

// --- Per-bar micro-benchmark: time each next() individually -----------

template <typename IndicatorFactory>
SampleSummary benchmark_indicator_perbar(const std::string& label,
                                         stratforge::CsvData& preloaded_feed,
                                         IndicatorFactory&& make_indicator,
                                         JsonReport& report) {
    // Clone feed and create indicator
    auto feed_ptr = preloaded_feed.clone();
    auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
    feed.load();
    auto indicator = make_indicator(feed);

    const auto bars = feed.size();

    const auto summary = run_perbar_benchmark(
        bars,
        [&]() { /* setup already done */ },
        [&](std::size_t bar) {
            indicator.next();
            if (bar + 1 < bars) {
                feed.advance();
            }
        },
        /* warmup_bars= */ 50
    );

    print_perbar_summary("  " + label + " [per-bar]", summary);

    report.add({.name = label + " (per-bar)",
                .summary = summary,
                .bars_per_iteration = 1});

    return summary;
}

// --- Per-bar with allocation counting ---------------------------------

template <typename IndicatorFactory>
std::int64_t benchmark_indicator_allocs(const std::string& label,
                                        stratforge::CsvData& preloaded_feed,
                                        IndicatorFactory&& make_indicator,
                                        JsonReport& report) {
    auto feed_ptr = preloaded_feed.clone();
    auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
    feed.load();
    auto indicator = make_indicator(feed);

    // Skip warmup period (first 50 bars)
    const std::size_t warmup = std::min(std::size_t{50}, feed.size());
    for (std::size_t i = 0; i < warmup; ++i) {
        indicator.next();
        if (i + 1 < feed.size()) feed.advance();
    }

    // Count allocations in hot path (remaining bars)
    AllocationCounter ac(label.c_str());
    const std::size_t hot_bars = feed.size() - warmup;
    for (std::size_t i = warmup; i < feed.size(); ++i) {
        indicator.next();
        if (i + 1 < feed.size()) feed.advance();
    }
    const auto allocs = ac.count();

    std::cout << "  " << label << " [allocs]: "
              << allocs << " allocations in " << hot_bars << " hot-path bars";
    if (allocs == 0) {
        std::cout << " [ok] ZERO-ALLOC";
    } else {
        std::cout << " (" << (static_cast<double>(allocs) / static_cast<double>(hot_bars))
                  << " allocs/bar)";
    }
    std::cout << '\n';

    report.add({.name = label + " (allocs)",
                .summary = {},
                .bars_per_iteration = hot_bars,
                .alloc_count = allocs});

    return allocs;
}

// --- Full indicator benchmark suite -----------------------------------

template <typename IndicatorFactory>
void full_indicator_benchmark(const std::string& label,
                              stratforge::CsvData& feed_512,
                              stratforge::CsvData* feed_100k,
                              std::size_t iterations,
                              IndicatorFactory make_indicator,
                              JsonReport& report) {
    // 1. Whole-run (512 bars)
    benchmark_indicator_run(label + " [512]", feed_512, iterations, make_indicator, report);

    // 2. Per-bar micro-benchmark (512 bars)
    benchmark_indicator_perbar(label + " [512]", feed_512, make_indicator, report);

    // 3. Allocation count (512 bars)
    benchmark_indicator_allocs(label + " [512]", feed_512, make_indicator, report);

    // 4. 100K-bar throughput (if dataset available)
    if (feed_100k) {
        benchmark_indicator_run(label + " [100K]", *feed_100k, std::min(iterations, std::size_t{10}),
                                make_indicator, report);
        benchmark_indicator_perbar(label + " [100K]", *feed_100k, make_indicator, report);
    }

    std::cout << '\n';
}

} // namespace

int main() {
    constexpr std::size_t iterations = 50;
    const std::string dataset_512 = "tools/golden_extract/datas/2005-2006-day-001.txt";
    const std::string dataset_100k = "build/bench_data/synthetic_100k.csv";

    std::cout << "=== StratForge Indicator Benchmarks ===\n";
    std::cout << "Iterations: " << iterations << " (with 3 warmup)\n\n";

    // Load datasets ONCE
    auto feed_512 = load_feed(dataset_512);
    std::cout << "Loaded 512-bar dataset: " << feed_512.size() << " bars\n";

    std::unique_ptr<stratforge::CsvData> feed_100k_ptr;
    const auto path_100k = source_path(dataset_100k);
    if (std::filesystem::exists(path_100k)) {
        feed_100k_ptr = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
            .filename = path_100k,
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });
        if (feed_100k_ptr->load()) {
            std::cout << "Loaded 100K-bar dataset: " << feed_100k_ptr->size() << " bars\n";
        } else {
            std::cerr << "Warning: 100K dataset failed to load, skipping large-scale tests\n";
            feed_100k_ptr.reset();
        }
    } else {
        std::cout << "Note: 100K dataset not found at " << path_100k << '\n';
        std::cout << "  Run gen_bench_data first for large-scale throughput tests\n";
    }

    std::cout << '\n';

    JsonReport report;

    // --- Cold/Warm comparison -----------------------------------------------
    std::cout << "--- Cold/Warm Comparison ---\n";
    run_cold_warm_comparison("SMA(30) full-run", 10, [&]() {
        auto fp = feed_512.clone();
        auto& f = static_cast<stratforge::CsvData&>(*fp);
        f.load();
        auto sma = stratforge::SMA(f.close(), 30);
        run_indicator(f, sma);
    });
    std::cout << '\n';

    // --- Moving Averages ---------------------------------------------------
    std::cout << "--- Moving Averages ---\n";
    full_indicator_benchmark("SMA(30)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::SMA(f.close(), 30); }, report);

    full_indicator_benchmark("EMA(30)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::EMA(f.close(), 30); }, report);

    // --- Volatility --------------------------------------------------------
    std::cout << "--- Volatility ---\n";
    full_indicator_benchmark("StdDev(20)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::StdDev(f.close(), 20); }, report);

    full_indicator_benchmark("ATR(14)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::ATR(f.high(), f.low(), f.close(), 14); }, report);

    full_indicator_benchmark("BollingerBands(20,2)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::BollingerBands(f.close(), 20, 2.0); }, report);

    // --- Momentum/Oscillators -----------------------------------------------
    std::cout << "--- Momentum/Oscillators ---\n";
    full_indicator_benchmark("RSI(14)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::RSI(f.close(), 14); }, report);

    full_indicator_benchmark("MACD(12,26,9)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::MACD(f.close(), 12, 26, 9); }, report);

    full_indicator_benchmark("Stochastic(14,3,3)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::Stochastic(f.high(), f.low(), f.close(), 14, 3, 3); }, report);

    // --- Trend --------------------------------------------------------------
    std::cout << "--- Trend ---\n";
    full_indicator_benchmark("ADX(14)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::ADX(f.high(), f.low(), f.close(), 14); }, report);

    full_indicator_benchmark("SuperTrend(10,3)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::SuperTrend(f.high(), f.low(), f.close(), 10, 3.0); }, report);

    // --- Complex Multi-line -------------------------------------------------
    std::cout << "--- Complex Multi-line ---\n";
    full_indicator_benchmark("Ichimoku(9,26,52)", feed_512, feed_100k_ptr.get(), iterations,
        [](stratforge::CsvData& f) { return stratforge::Ichimoku(f.high(), f.low(), f.close(), 9, 26, 52, 26, 26); }, report);

    // --- Write JSON report --------------------------------------------------
    report.write(source_path("build/bench_results/indicator_benchmarks.json"),
                 "indicator_benchmarks");

    std::cout << "=== Indicator Benchmarks Complete ===\n";
    return 0;
}
