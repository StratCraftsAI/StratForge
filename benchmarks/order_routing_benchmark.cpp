#include "benchmark_utils.hpp"

#include <stratforge/broker/broker.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
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

// --- Strategy that issues orders every bar (stress-test broker pipeline) ---

class OrderFloodStrategy : public stratforge::Strategy {
public:
    void init() override {}

    void next() override {
        // Alternate buy/sell every bar to maximize order creation + fill throughput
        if (bar_count_ % 2 == 0) {
            (void)buy(10.0);
        } else {
            if (position().size > 0) {
                (void)close();
            }
        }
        ++bar_count_;
    }

private:
    std::size_t bar_count_ = 0;
};

// --- Strategy that uses limit orders (more complex fill logic) ---

class LimitOrderStrategy : public stratforge::Strategy {
public:
    void init() override {}

    void next() override {
        const double price = data().close()[0];
        if (bar_count_ % 4 == 0) {
            // Buy limit below current price
            (void)buy(10.0, price * 0.99);
        } else if (bar_count_ % 4 == 2) {
            if (position().size > 0) {
                // Sell limit above current price
                (void)sell(10.0, price * 1.01);
            }
        }
        ++bar_count_;
    }

private:
    std::size_t bar_count_ = 0;
};

// --- Benchmark: order flood (market orders every bar) ---

void benchmark_order_flood(stratforge::CsvData& preloaded_feed,
                           std::size_t iterations,
                           JsonReport& report) {
    const auto bars = preloaded_feed.size();

    auto bench_fn = [&]() {
        stratforge::Cerebro cerebro;
        auto feed_ptr = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
        feed.load();

        cerebro.add_strategy<OrderFloodStrategy>();
        cerebro.add_data(std::make_unique<stratforge::CsvData>(std::move(feed)));
        cerebro.set_cash(1'000'000.0);
        cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});
        cerebro.run(stratforge::Cerebro::RunOptions{.runonce = false, .preload = true});

        volatile double cash = cerebro.broker().cash();
        (void)cash;
    };

    const auto summary = run_benchmark(iterations, bench_fn);
    const double ns_per_bar = summary.avg_ns / static_cast<double>(bars);

    std::cout << "Order Flood (market order every bar) [" << bars << " bars]:\n"
              << "  total: avg=" << summary.avg_ns / 1'000'000.0 << " ms"
              << "  p50=" << summary.p50_ns / 1'000'000.0 << " ms"
              << "  p99=" << summary.p99_ns / 1'000'000.0 << " ms\n"
              << "  per-bar: " << ns_per_bar << " ns (includes order create + fill)\n";

    report.add({.name = "order_flood_market_" + std::to_string(bars),
                .summary = summary,
                .bars_per_iteration = bars});
}

// --- Benchmark: limit order strategy ---

void benchmark_limit_orders(stratforge::CsvData& preloaded_feed,
                            std::size_t iterations,
                            JsonReport& report) {
    const auto bars = preloaded_feed.size();

    auto bench_fn = [&]() {
        stratforge::Cerebro cerebro;
        auto feed_ptr = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
        feed.load();

        cerebro.add_strategy<LimitOrderStrategy>();
        cerebro.add_data(std::make_unique<stratforge::CsvData>(std::move(feed)));
        cerebro.set_cash(1'000'000.0);
        cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});
        cerebro.run(stratforge::Cerebro::RunOptions{.runonce = false, .preload = true});

        volatile double cash = cerebro.broker().cash();
        (void)cash;
    };

    const auto summary = run_benchmark(iterations, bench_fn);
    const double ns_per_bar = summary.avg_ns / static_cast<double>(bars);

    std::cout << "Limit Orders (every 4 bars) [" << bars << " bars]:\n"
              << "  total: avg=" << summary.avg_ns / 1'000'000.0 << " ms"
              << "  p50=" << summary.p50_ns / 1'000'000.0 << " ms"
              << "  p99=" << summary.p99_ns / 1'000'000.0 << " ms\n"
              << "  per-bar: " << ns_per_bar << " ns\n";

    report.add({.name = "order_limit_" + std::to_string(bars),
                .summary = summary,
                .bars_per_iteration = bars});
}

// --- Benchmark: order creation + fill allocation audit ---

void benchmark_order_allocs(stratforge::CsvData& preloaded_feed,
                            JsonReport& report) {
    const auto bars = preloaded_feed.size();

    AllocationCounter ac("order_flood");
    {
        stratforge::Cerebro cerebro;
        auto feed_ptr = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
        feed.load();

        cerebro.add_strategy<OrderFloodStrategy>();
        cerebro.add_data(std::make_unique<stratforge::CsvData>(std::move(feed)));
        cerebro.set_cash(1'000'000.0);
        cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});
        cerebro.run(stratforge::Cerebro::RunOptions{.runonce = false, .preload = true});
    }
    const auto allocs = ac.count();
    const double allocs_per_bar = static_cast<double>(allocs) / static_cast<double>(bars);

    std::cout << "Order Flood Allocations [" << bars << " bars]:\n"
              << "  total=" << allocs
              << "  per-bar=" << allocs_per_bar << "\n";

    report.add({.name = "order_flood_allocs_" + std::to_string(bars),
                .summary = {},
                .bars_per_iteration = bars,
                .alloc_count = allocs});
}

// --- Cold/warm comparison for order processing ---

void benchmark_order_cold_warm(stratforge::CsvData& preloaded_feed) {
    run_cold_warm_comparison("Order Flood (512)", 20, [&]() {
        stratforge::Cerebro cerebro;
        auto feed_ptr = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
        feed.load();

        cerebro.add_strategy<OrderFloodStrategy>();
        cerebro.add_data(std::make_unique<stratforge::CsvData>(std::move(feed)));
        cerebro.set_cash(1'000'000.0);
        cerebro.run(stratforge::Cerebro::RunOptions{.runonce = false, .preload = true});
    });
}

} // namespace

int main() {
    std::cout << "=== stratforge Order Routing Benchmarks ===\n";
    setup_benchmark_env();
    std::cout << '\n';

    const std::string dataset = "tools/golden_extract/datas/2005-2006-day-001.txt";
    auto feed = load_feed(dataset);
    std::cout << "Dataset: " << feed.size() << " bars\n\n";

    JsonReport report;

    // Cold/warm
    std::cout << "--- Cold/Warm Comparison ---\n";
    benchmark_order_cold_warm(feed);
    std::cout << '\n';

    // Market order flood
    std::cout << "--- Market Order Flood ---\n";
    benchmark_order_flood(feed, 50, report);
    std::cout << '\n';

    // Limit orders
    std::cout << "--- Limit Order Strategy ---\n";
    benchmark_limit_orders(feed, 50, report);
    std::cout << '\n';

    // Allocation audit
    std::cout << "--- Allocation Audit ---\n";
    benchmark_order_allocs(feed, report);
    std::cout << '\n';

    report.write(source_path("build/bench_results/order_routing_benchmark.json"),
                 "order_routing_benchmark");

    std::cout << "=== Order Routing Benchmarks Complete ===\n";
    return 0;
}
