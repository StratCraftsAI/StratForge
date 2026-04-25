#include "benchmark_utils.hpp"

#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
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

// --- Indicator hot-path allocation audit --------------------------------

template <typename IndicatorFactory>
std::int64_t audit_indicator_allocs(const std::string& label,
                                    stratforge::CsvData& preloaded_feed,
                                    IndicatorFactory&& make_indicator) {
    auto feed_ptr = preloaded_feed.clone();
    auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
    feed.load();
    auto indicator = make_indicator(feed);

    // Run through warmup period (allocations expected for output line growth)
    const std::size_t warmup = std::min(std::size_t{60}, feed.size());
    for (std::size_t i = 0; i < warmup; ++i) {
        indicator.next();
        if (i + 1 < feed.size()) feed.advance();
    }

    // Now count allocations in hot path only (after indicator output line
    // has grown past its initial capacity, and indicator state is warm)
    AllocationCounter ac(label.c_str());
    const std::size_t hot_bars = feed.size() - warmup;
    for (std::size_t i = warmup; i < feed.size(); ++i) {
        indicator.next();
        if (i + 1 < feed.size()) feed.advance();
    }
    const auto allocs = ac.count();

    const char* status = (allocs == 0) ? "ZERO-ALLOC" : "ALLOCATING";
    std::cout << "  " << label << ": " << allocs << " allocs / " << hot_bars << " bars"
              << " [" << status << "]";
    if (allocs > 0) {
        std::cout << " (" << (static_cast<double>(allocs) / static_cast<double>(hot_bars))
                  << " allocs/bar)";
    }
    std::cout << '\n';

    return allocs;
}

// --- Strategy with indicator access for hot-path auditing ---------------

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

std::int64_t audit_strategy_allocs(stratforge::CsvData& preloaded_feed) {
    // Run full cerebro backtest and count total allocations
    AllocationCounter ac("strategy_full");

    stratforge::Cerebro cerebro;
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

    const auto total_allocs = ac.count();
    return total_allocs;
}

} // namespace

int main() {
    const std::string dataset_512 = "tools/golden_extract/datas/2005-2006-day-001.txt";

    std::cout << "=== StratForge Memory Allocation Benchmarks ===\n\n";

    auto feed_512 = load_feed(dataset_512);
    std::cout << "Dataset: " << feed_512.size() << " bars\n\n";

    JsonReport report;
    std::int64_t total_violations = 0;

    // --- Individual Indicator Allocation Audit ----------------------------
    std::cout << "--- Indicator Hot-Path Allocation Audit ---\n";
    std::cout << "(Warmup: first 60 bars excluded from counting)\n\n";

    auto count_and_report = [&](const std::string& name, std::int64_t allocs) {
        report.add({.name = name,
                    .summary = {},
                    .bars_per_iteration = feed_512.size() - 60,
                    .alloc_count = allocs});
        if (allocs > 0) ++total_violations;
    };

    count_and_report("SMA(30)",
        audit_indicator_allocs("SMA(30)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::SMA(f.close(), 30); }));

    count_and_report("EMA(30)",
        audit_indicator_allocs("EMA(30)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::EMA(f.close(), 30); }));

    count_and_report("StdDev(20)",
        audit_indicator_allocs("StdDev(20)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::StdDev(f.close(), 20); }));

    count_and_report("ATR(14)",
        audit_indicator_allocs("ATR(14)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::ATR(f.high(), f.low(), f.close(), 14); }));

    count_and_report("BollingerBands(20,2)",
        audit_indicator_allocs("BollingerBands(20,2)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::BollingerBands(f.close(), 20, 2.0); }));

    count_and_report("RSI(14)",
        audit_indicator_allocs("RSI(14)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::RSI(f.close(), 14); }));

    count_and_report("MACD(12,26,9)",
        audit_indicator_allocs("MACD(12,26,9)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::MACD(f.close(), 12, 26, 9); }));

    count_and_report("Stochastic(14,3,3)",
        audit_indicator_allocs("Stochastic(14,3,3)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::Stochastic(f.high(), f.low(), f.close(), 14, 3, 3); }));

    count_and_report("ADX(14)",
        audit_indicator_allocs("ADX(14)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::ADX(f.high(), f.low(), f.close(), 14); }));

    count_and_report("SuperTrend(10,3)",
        audit_indicator_allocs("SuperTrend(10,3)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::SuperTrend(f.high(), f.low(), f.close(), 10, 3.0); }));

    count_and_report("Ichimoku(9,26,52)",
        audit_indicator_allocs("Ichimoku(9,26,52)", feed_512,
            [](stratforge::CsvData& f) { return stratforge::Ichimoku(f.high(), f.low(), f.close(), 9, 26, 52, 26, 26); }));

    // --- Strategy Allocation Audit ------------------------------------------
    std::cout << "\n--- Strategy Allocation Audit ---\n";
    const auto strategy_allocs = audit_strategy_allocs(feed_512);
    std::cout << "  CompositeStrategy (full run): " << strategy_allocs
              << " total allocations (init + hot path)\n";

    report.add({.name = "CompositeStrategy (full-run allocs)",
                .summary = {},
                .bars_per_iteration = feed_512.size(),
                .alloc_count = strategy_allocs});

    // --- Summary -------------------------------------------------------------
    std::cout << "\n--- Summary ---\n";
    std::cout << "Indicators with hot-path allocations: " << total_violations << " / 11\n";
    if (total_violations == 0) {
        std::cout << "All indicators ZERO-ALLOC in hot path!\n";
    } else {
        std::cout << "WARNING: " << total_violations
                  << " indicator(s) allocate during hot-path computation.\n";
        std::cout << "Note: allocations from Line<double>::forward() (output line growth)\n"
                  << "      are expected until the output vector capacity is reached.\n"
                  << "      The 60-bar warmup should absorb most of these.\n"
                  << "      Consider pre-reserving output lines for zero-alloc hot path.\n";
    }

    // --- Write JSON report --------------------------------------------------
    report.write(source_path("build/bench_results/memory_benchmarks.json"),
                 "memory_benchmarks");

    std::cout << "\n=== Memory Benchmarks Complete ===\n";
    return 0;
}
