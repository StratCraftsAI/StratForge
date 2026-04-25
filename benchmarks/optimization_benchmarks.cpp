#include "benchmark_utils.hpp"

#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/engine/optimizer.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace stratforge::bench;

// Parameterized SMA crossover strategy for optimization
class OptimizableStrategy : public stratforge::Strategy {
public:
    stratforge::ParamMap default_params() const override {
        return {
            {"fast_period", stratforge::ParamValue{std::int64_t{10}}},
            {"slow_period", stratforge::ParamValue{std::int64_t{30}}},
        };
    }

    void init() override {
        const auto fast_period = static_cast<int>(std::get<std::int64_t>(params().at("fast_period")));
        const auto slow_period = static_cast<int>(std::get<std::int64_t>(params().at("slow_period")));

        sma_fast_ = std::make_unique<stratforge::SMA>(data().close(), fast_period);
        sma_slow_ = std::make_unique<stratforge::SMA>(data().close(), slow_period);
    }

    void next() override {
        if (sma_fast_->line().size() == 0 || sma_slow_->line().size() == 0) {
            return;
        }

        const double sma_fast = sma_fast_->line()[0];
        const double sma_slow = sma_slow_->line()[0];

        if (!position().size) {
            if (sma_fast > sma_slow) {
                (void)buy(100.0);
            }
        } else if (position().size > 0) {
            if (sma_fast < sma_slow) {
                (void)close();
            }
        }
    }

private:
    std::unique_ptr<stratforge::SMA> sma_fast_;
    std::unique_ptr<stratforge::SMA> sma_slow_;
};

void benchmark_optimization_grid(stratforge::CsvData& preloaded_feed,
                                 std::size_t iterations,
                                 std::size_t grid_size,
                                 JsonReport& report) {
    std::cout << "Grid size: " << grid_size << " parameter combinations\n";

    const auto summary = run_benchmark(iterations, [&]() {
        // Clone feed -- no disk I/O
        auto base_feed = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*base_feed);
        feed.load();

        // Build parameter grid
        const int grid_dim = static_cast<int>(std::sqrt(static_cast<double>(grid_size)));

        std::vector<stratforge::ParamValue> fast_values;
        for (int fast = 5; fast < 5 + grid_dim; ++fast) {
            fast_values.push_back(stratforge::ParamValue{std::int64_t{fast}});
        }

        std::vector<stratforge::ParamValue> slow_values;
        for (int slow = 20; slow < 20 + grid_dim; ++slow) {
            slow_values.push_back(stratforge::ParamValue{std::int64_t{slow}});
        }

        std::vector<stratforge::ParamRange> ranges;
        ranges.push_back({"fast_period", std::move(fast_values)});
        ranges.push_back({"slow_period", std::move(slow_values)});

        stratforge::Optimizer::Config config;
        config.cash = 10000.0;
        config.commission = stratforge::CommissionInfo{.commission = 0.001};
        config.max_threads = 4;

        stratforge::Optimizer optimizer(config);

        auto factory = [](const stratforge::ParamMap& params) -> std::unique_ptr<stratforge::Strategy> {
            auto strat = std::make_unique<OptimizableStrategy>();
            strat->set_params(params);
            return strat;
        };

        auto extractor = [](const stratforge::Cerebro& cerebro, const stratforge::ParamMap& params) -> stratforge::OptResult {
            stratforge::OptResult result;
            result.params = params;
            result.final_value = cerebro.broker().cash();
            return result;
        };

        std::vector<stratforge::DataFeed*> feeds = {base_feed.get()};
        auto results = optimizer.run(feeds, factory, ranges, extractor);

        volatile std::size_t result_count = results.size();
        (void)result_count;
    });

    const double ms_total = summary.avg_ns / 1'000'000.0;
    const double ms_per_combo = ms_total / static_cast<double>(grid_size);
    const double combos_per_sec = 1000.0 / ms_per_combo;

    std::cout << "Optimization benchmark: "
              << "iterations=" << iterations
              << " grid_size=" << grid_size
              << " avg_ms=" << ms_total
              << " p50_ms=" << (summary.p50_ns / 1'000'000.0)
              << " p99_ms=" << (summary.p99_ns / 1'000'000.0)
              << " ms_per_combo=" << ms_per_combo
              << " combos/sec=" << combos_per_sec
              << '\n';

    report.add({.name = "Optimization (" + std::to_string(grid_size) + " combos)",
                .summary = summary,
                .bars_per_iteration = preloaded_feed.size()});
}

} // namespace

int main() {
    constexpr std::size_t iterations = 5;
    const std::string dataset = "tools/golden_extract/datas/2005-2006-day-001.txt";

    std::cout << "=== StratForge Optimization Benchmarks ===\n";
    std::cout << "Iterations: " << iterations << " (with 3 warmup)\n\n";

    // Load dataset ONCE
    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(dataset),
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });

    if (!feed.load()) {
        std::cerr << "Error: Unable to load dataset: " << dataset << '\n';
        return 1;
    }

    std::cout << "Dataset: " << feed.size() << " bars\n\n";

    JsonReport report;

    std::cout << "--- Parameter Grid Optimization (2x2=4 combos) ---\n";
    benchmark_optimization_grid(feed, iterations, 4, report);

    std::cout << "\n--- Parameter Grid Optimization (3x3=9 combos) ---\n";
    benchmark_optimization_grid(feed, iterations, 9, report);

    std::cout << "\n--- Parameter Grid Optimization (5x5=25 combos) ---\n";
    benchmark_optimization_grid(feed, iterations, 25, report);

    // Write JSON report
    report.write(source_path("build/bench_results/optimization_benchmarks.json"),
                 "optimization_benchmarks");

    std::cout << "\n=== Optimization Benchmarks Complete ===\n";
    std::cout << "Note: Parallel speedup depends on hardware (CPU cores) and workload distribution.\n";

    return 0;
}
