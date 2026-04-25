#include "benchmark_utils.hpp"

#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>

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
        throw std::runtime_error("Unable to load dataset: " + relative_path);
    }

    return feed;
}

// Simple SMA crossover strategy with 5 indicators
class CompositeStrategy : public stratforge::Strategy {
public:
    void init() override {
        sma_fast_ = std::make_unique<stratforge::SMA>(data().close(), 10);
        sma_slow_ = std::make_unique<stratforge::SMA>(data().close(), 30);
        ema_ = std::make_unique<stratforge::EMA>(data().close(), 20);
        rsi_ = std::make_unique<stratforge::RSI>(data().close(), 14);
        macd_ = std::make_unique<stratforge::MACD>(data().close(), 12, 26, 9);
    }

    void next() override {
        if (sma_fast_->line().size() == 0 || sma_slow_->line().size() == 0 ||
            ema_->line().size() == 0 || rsi_->line().size() == 0 ||
            macd_->macd().size() == 0) {
            return;
        }

        const double sma_fast = sma_fast_->line()[0];
        const double sma_slow = sma_slow_->line()[0];
        const double ema = ema_->line()[0];
        const double rsi = rsi_->line()[0];
        const double macd = macd_->macd()[0];

        if (!position().size) {
            if (sma_fast > sma_slow && rsi < 70.0) {
                (void)buy(100.0);
            }
        } else if (position().size > 0) {
            if (sma_fast < sma_slow || rsi > 80.0) {
                (void)close();
            }
        }

        (void)ema;
        (void)macd;
    }

private:
    std::unique_ptr<stratforge::SMA> sma_fast_;
    std::unique_ptr<stratforge::SMA> sma_slow_;
    std::unique_ptr<stratforge::EMA> ema_;
    std::unique_ptr<stratforge::RSI> rsi_;
    std::unique_ptr<stratforge::MACD> macd_;
};

// --- Helper: run a full cerebro backtest (no I/O in hot path) --------

void run_cerebro_backtest(stratforge::CsvData& preloaded_feed) {
    stratforge::Cerebro cerebro;

    // Clone feed -- no disk I/O
    auto feed_ptr = preloaded_feed.clone();
    auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
    feed.load();

    cerebro.add_strategy<CompositeStrategy>();
    cerebro.add_data(std::make_unique<stratforge::CsvData>(std::move(feed)));
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run(stratforge::Cerebro::RunOptions{
        .runonce = false,
        .preload = true,
    });

    volatile double final_cash = cerebro.broker().cash();
    (void)final_cash;
}

// --- Whole-run strategy benchmark ----------------------------------------

void benchmark_strategy_execution(stratforge::CsvData& preloaded_feed,
                                  std::size_t iterations,
                                  const std::string& dataset_label,
                                  JsonReport& report) {
    const auto bars = preloaded_feed.size();

    const auto summary = run_benchmark(iterations, [&]() {
        run_cerebro_backtest(preloaded_feed);
    });

    print_summary("Composite Strategy (5 indicators + broker) [" + dataset_label + "]",
                  summary, iterations, bars);

    // Per-bar latency (derived from whole-run)
    const double ns_per_bar = summary.avg_ns / static_cast<double>(bars);
    std::cout << "  --> Per-bar latency: avg=" << ns_per_bar << " ns"
              << ", p50=" << (summary.p50_ns / static_cast<double>(bars)) << " ns"
              << ", p99=" << (summary.p99_ns / static_cast<double>(bars)) << " ns\n";

    constexpr double target_ns_per_bar = 100.0;
    if (ns_per_bar < target_ns_per_bar) {
        std::cout << "  MEETS target (<" << target_ns_per_bar << " ns)\n";
    } else {
        std::cout << "  EXCEEDS target (>" << target_ns_per_bar << " ns)\n";
    }

    report.add({.name = "CompositeStrategy [" + dataset_label + "] (whole-run)",
                .summary = summary,
                .bars_per_iteration = bars});
}

// --- Full backtest wall-clock -------------------------------------------

void benchmark_full_backtest(stratforge::CsvData& preloaded_feed,
                             std::size_t iterations,
                             const std::string& dataset_label,
                             JsonReport& report) {
    const auto bars = preloaded_feed.size();

    const auto summary = run_benchmark(iterations, [&]() {
        run_cerebro_backtest(preloaded_feed);
    });

    std::cout << "Full Backtest [" << dataset_label << "]: "
              << "iterations=" << iterations
              << " bars=" << bars
              << " avg_ms=" << (summary.avg_ns / 1'000'000.0)
              << " p50_ms=" << (summary.p50_ns / 1'000'000.0)
              << " p99_ms=" << (summary.p99_ns / 1'000'000.0)
              << " min_ms=" << (summary.min_ns / 1'000'000.0)
              << " max_ms=" << (summary.max_ns / 1'000'000.0)
              << '\n';

    report.add({.name = "FullBacktest [" + dataset_label + "]",
                .summary = summary,
                .bars_per_iteration = bars});
}

// --- Strategy allocation audit -------------------------------------------

void benchmark_strategy_allocs(stratforge::CsvData& preloaded_feed,
                               const std::string& dataset_label,
                               JsonReport& report) {
    // We need to count allocations during the hot-path (after init).
    // Run cerebro normally but count allocs during the execution phase.
    // Since Cerebro wraps everything, we count for the whole run and note that
    // init-phase allocations are expected.

    AllocationCounter ac("strategy_hot_path");
    run_cerebro_backtest(preloaded_feed);
    const auto allocs = ac.count();

    std::cout << "Strategy allocs [" << dataset_label << "]: "
              << allocs << " total allocations (includes init + execution)\n";

    report.add({.name = "CompositeStrategy [" + dataset_label + "] (allocs)",
                .summary = {},
                .bars_per_iteration = preloaded_feed.size(),
                .alloc_count = allocs});
}

} // namespace

int main() {
    constexpr std::size_t iterations = 50;
    const std::string dataset_512 = "tools/golden_extract/datas/2005-2006-day-001.txt";
    const std::string dataset_100k = "build/bench_data/synthetic_100k.csv";

    std::cout << "=== StratForge Strategy Benchmarks ===\n";
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
            feed_100k_ptr.reset();
        }
    } else {
        std::cout << "Note: 100K dataset not found, run gen_bench_data first\n";
    }

    std::cout << '\n';

    JsonReport report;

    // --- Cold/Warm comparison -----------------------------------------------
    std::cout << "--- Cold/Warm Comparison ---\n";
    run_cold_warm_comparison("Full Backtest (512)", 10, [&]() {
        run_cerebro_backtest(feed_512);
    });
    std::cout << '\n';

    // --- 512-bar benchmarks -------------------------------------------------
    std::cout << "--- Per-Bar Composite Strategy [512] ---\n";
    benchmark_strategy_execution(feed_512, iterations, "512", report);

    std::cout << "\n--- Full Backtest Wall-Clock [512] ---\n";
    benchmark_full_backtest(feed_512, iterations, "512", report);

    std::cout << "\n--- Allocation Audit [512] ---\n";
    benchmark_strategy_allocs(feed_512, "512", report);

    // --- 100K-bar benchmarks ------------------------------------------------
    if (feed_100k_ptr) {
        std::cout << "\n--- Per-Bar Composite Strategy [100K] ---\n";
        benchmark_strategy_execution(*feed_100k_ptr, std::min(iterations, std::size_t{10}),
                                     "100K", report);

        std::cout << "\n--- Full Backtest Wall-Clock [100K] ---\n";
        benchmark_full_backtest(*feed_100k_ptr, std::min(iterations, std::size_t{10}),
                                "100K", report);
    }

    // --- Write JSON report --------------------------------------------------
    report.write(source_path("build/bench_results/strategy_benchmarks.json"),
                 "strategy_benchmarks");

    std::cout << "\n=== Strategy Benchmarks Complete ===\n";
    return 0;
}
