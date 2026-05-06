#include "benchmark_utils.hpp"

#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/sma.hpp>
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

// Multi-indicator strategy for meaningful cold-start measurement
class MultiIndicatorStrategy : public stratforge::Strategy {
public:
    void init() override {
        sma_ = std::make_unique<stratforge::SMA>(data().close(), 20);
        ema_ = std::make_unique<stratforge::EMA>(data().close(), 12);
        rsi_ = std::make_unique<stratforge::RSI>(data().close(), 14);
        macd_ = std::make_unique<stratforge::MACD>(data().close(), 12, 26, 9);
        boll_ = std::make_unique<stratforge::BollingerBands>(data().close(), 20, 2.0);
    }

    void next() override {
        if (sma_->line().size() == 0) return;
        const double sma = sma_->line()[0];
        const double ema = ema_->line()[0];
        const double rsi = rsi_->line()[0];

        if (!position().size) {
            if (ema > sma && rsi < 70.0) {
                (void)buy(100.0);
            }
        } else {
            if (ema < sma || rsi > 80.0) {
                (void)close();
            }
        }
    }

private:
    std::unique_ptr<stratforge::SMA> sma_;
    std::unique_ptr<stratforge::EMA> ema_;
    std::unique_ptr<stratforge::RSI> rsi_;
    std::unique_ptr<stratforge::MACD> macd_;
    std::unique_ptr<stratforge::BollingerBands> boll_;
};

// --- Measure cold-start: first execution after fresh process/cache state ---

struct ColdWarmResult {
    double cold_ns;
    double warm_avg_ns;
    double warm_p50_ns;
    double warm_p99_ns;
    double ratio;
};

ColdWarmResult measure_cold_warm(stratforge::CsvData& preloaded_feed,
                                 std::size_t warm_iterations) {
    auto run_once = [&]() {
        stratforge::Cerebro cerebro;
        auto feed_ptr = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
        feed.load();

        cerebro.add_strategy<MultiIndicatorStrategy>();
        cerebro.add_data(std::make_unique<stratforge::CsvData>(std::move(feed)));
        cerebro.set_cash(10000.0);
        cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});
        cerebro.run(stratforge::Cerebro::RunOptions{.runonce = false, .preload = true});

        volatile double cash = cerebro.broker().cash();
        (void)cash;
    };

    // Cold run
    const auto cold_start = std::chrono::steady_clock::now();
    run_once();
    const auto cold_end = std::chrono::steady_clock::now();
    const double cold_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(cold_end - cold_start).count());

    // Warm runs
    const auto warm_summary = run_benchmark(warm_iterations, run_once, 0);

    return ColdWarmResult{
        .cold_ns = cold_ns,
        .warm_avg_ns = warm_summary.avg_ns,
        .warm_p50_ns = warm_summary.p50_ns,
        .warm_p99_ns = warm_summary.p99_ns,
        .ratio = cold_ns / warm_summary.avg_ns,
    };
}

// --- Measure cold-start for CSV loading ---

ColdWarmResult measure_csv_cold_warm(const std::string& filepath,
                                     std::size_t warm_iterations) {
    auto load_once = [&]() {
        stratforge::CsvData feed(stratforge::CsvData::Params{
            .filename = filepath,
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });
        volatile bool ok = feed.load();
        (void)ok;
    };

    const auto cold_start = std::chrono::steady_clock::now();
    load_once();
    const auto cold_end = std::chrono::steady_clock::now();
    const double cold_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(cold_end - cold_start).count());

    const auto warm_summary = run_benchmark(warm_iterations, load_once, 0);

    return ColdWarmResult{
        .cold_ns = cold_ns,
        .warm_avg_ns = warm_summary.avg_ns,
        .warm_p50_ns = warm_summary.p50_ns,
        .warm_p99_ns = warm_summary.p99_ns,
        .ratio = cold_ns / warm_summary.avg_ns,
    };
}

// --- Measure cold-start for indicator computation ---

ColdWarmResult measure_indicator_cold_warm(stratforge::CsvData& preloaded_feed,
                                           std::size_t warm_iterations) {
    auto run_indicators = [&]() {
        auto feed_ptr = preloaded_feed.clone();
        auto& feed = static_cast<stratforge::CsvData&>(*feed_ptr);
        feed.load();

        stratforge::SMA sma(feed.close(), 20);
        stratforge::EMA ema(feed.close(), 12);
        stratforge::RSI rsi(feed.close(), 14);
        stratforge::MACD macd(feed.close(), 12, 26, 9);
        stratforge::BollingerBands boll(feed.close(), 20, 2.0);

        for (std::size_t i = 0; i < feed.size(); ++i) {
            sma.next();
            ema.next();
            rsi.next();
            macd.next();
            boll.next();
            if (i + 1 < feed.size()) feed.advance();
        }

        volatile double v = sma.line()[0];
        (void)v;
    };

    const auto cold_start = std::chrono::steady_clock::now();
    run_indicators();
    const auto cold_end = std::chrono::steady_clock::now();
    const double cold_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(cold_end - cold_start).count());

    const auto warm_summary = run_benchmark(warm_iterations, run_indicators, 0);

    return ColdWarmResult{
        .cold_ns = cold_ns,
        .warm_avg_ns = warm_summary.avg_ns,
        .warm_p50_ns = warm_summary.p50_ns,
        .warm_p99_ns = warm_summary.p99_ns,
        .ratio = cold_ns / warm_summary.avg_ns,
    };
}

void print_cold_warm(const std::string& label, const ColdWarmResult& result,
                     JsonReport& report) {
    std::cout << label << ":\n"
              << "  cold:  " << result.cold_ns / 1'000'000.0 << " ms\n"
              << "  warm:  avg=" << result.warm_avg_ns / 1'000'000.0 << " ms"
              << "  p50=" << result.warm_p50_ns / 1'000'000.0 << " ms"
              << "  p99=" << result.warm_p99_ns / 1'000'000.0 << " ms\n"
              << "  ratio: " << std::fixed << std::setprecision(2) << result.ratio << "x\n";

    // Store warm metrics in JSON (cold is a single sample, less meaningful for regression)
    SampleSummary summary;
    summary.avg_ns = result.warm_avg_ns;
    summary.p50_ns = result.warm_p50_ns;
    summary.p99_ns = result.warm_p99_ns;
    summary.min_ns = result.warm_avg_ns; // approximate
    summary.max_ns = result.warm_p99_ns;
    summary.sample_count = 1;

    report.add({.name = label + " (warm)",
                .summary = summary,
                .bars_per_iteration = 0});
}

} // namespace

int main() {
    std::cout << "=== stratforge Cold-Start Benchmarks ===\n";
    setup_benchmark_env();
    std::cout << '\n';

    const std::string dataset = "tools/golden_extract/datas/2005-2006-day-001.txt";
    auto feed = load_feed(dataset);
    std::cout << "Dataset: " << feed.size() << " bars\n\n";

    JsonReport report;
    constexpr std::size_t warm_iters = 50;

    // CSV loading cold/warm
    std::cout << "--- CSV Load Cold/Warm ---\n";
    auto csv_result = measure_csv_cold_warm(
        source_path(dataset), warm_iters);
    print_cold_warm("CSV Load (512 bars)", csv_result, report);
    std::cout << '\n';

    // Indicator computation cold/warm
    std::cout << "--- Indicator Pipeline Cold/Warm ---\n";
    auto ind_result = measure_indicator_cold_warm(feed, warm_iters);
    print_cold_warm("5-Indicator Pipeline (512 bars)", ind_result, report);
    std::cout << '\n';

    // Full backtest cold/warm
    std::cout << "--- Full Backtest Cold/Warm ---\n";
    auto bt_result = measure_cold_warm(feed, warm_iters);
    print_cold_warm("Full Backtest (5 indicators + broker, 512 bars)", bt_result, report);
    std::cout << '\n';

    // Summary table
    std::cout << "--- Summary ---\n";
    std::cout << std::setw(40) << std::left << "Component"
              << std::setw(12) << "Cold (ms)"
              << std::setw(12) << "Warm (ms)"
              << std::setw(10) << "Ratio\n";
    std::cout << std::string(74, '-') << '\n';

    auto row = [](const std::string& name, const ColdWarmResult& r) {
        std::cout << std::setw(40) << std::left << name
                  << std::setw(12) << std::fixed << std::setprecision(3)
                  << r.cold_ns / 1'000'000.0
                  << std::setw(12) << r.warm_avg_ns / 1'000'000.0
                  << std::setw(10) << std::setprecision(2) << r.ratio << "x\n";
    };

    row("CSV Load", csv_result);
    row("Indicator Pipeline", ind_result);
    row("Full Backtest", bt_result);
    std::cout << '\n';

    report.write(source_path("build/bench_results/cold_start_benchmark.json"),
                 "cold_start_benchmark");

    std::cout << "=== Cold-Start Benchmarks Complete ===\n";
    return 0;
}
