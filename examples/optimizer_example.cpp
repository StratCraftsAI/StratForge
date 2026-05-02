// Optimizer Example -- SMA Crossover Parameter Grid Search
// Demonstrates: Optimizer, ParamRange, ResultExtractor, parallel execution

#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/engine/optimizer.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {
std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}
} // namespace

/// SMA crossover strategy with optimizable fast/slow periods.
class SmaCrossover : public stratforge::Strategy {
public:
    void init() override {
        auto p = params();
        auto fast = p.get<std::size_t>("fast_period");
        auto slow = p.get<std::size_t>("slow_period");

        sma_fast_ = std::make_unique<stratforge::SMA>(data().close(), fast);
        sma_slow_ = std::make_unique<stratforge::SMA>(data().close(), slow);
    }

    void next() override {
        // Compute indicators
        sma_fast_->next();
        sma_slow_->next();

        if (sma_fast_->line().size() == 0 || sma_slow_->line().size() == 0) return;

        const double fast = sma_fast_->line()[0];
        const double slow = sma_slow_->line()[0];

        if (!position().size && fast > slow) {
            (void)buy(2.0);
        } else if (position().size > 0 && fast < slow) {
            (void)close();
        }
    }

    [[nodiscard]] stratforge::ParamMap default_params() const override {
        return {
            {"fast_period", std::int64_t{10}},
            {"slow_period", std::int64_t{30}},
        };
    }

private:
    std::unique_ptr<stratforge::SMA> sma_fast_;
    std::unique_ptr<stratforge::SMA> sma_slow_;
};

int main() {
    std::cout << "=== SMA Crossover Optimizer Example ===\n\n";

    // Load data
    auto feed = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
        .filename = source_path("tools/golden_extract/datas/2006-day-001.txt"),
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });
    if (!feed->load()) {
        std::cerr << "Failed to load data\n";
        return 1;
    }
    std::cout << "Loaded " << feed->size() << " bars\n\n";

    // Build optimizer
    stratforge::Optimizer optimizer(stratforge::Optimizer::Config{
        .cash = 10000.0,
        .commission = stratforge::CommissionInfo{.commission = 0.001},
    });

    // Define parameter ranges
    std::vector<stratforge::ParamRange> ranges = {
        {"fast_period", {std::int64_t{5}, std::int64_t{10}, std::int64_t{15}, std::int64_t{20}}},
        {"slow_period", {std::int64_t{30}, std::int64_t{40}, std::int64_t{50}, std::int64_t{60}}},
    };

    // Strategy factory
    auto factory = [](const stratforge::ParamMap&) -> std::unique_ptr<stratforge::Strategy> {
        return std::make_unique<SmaCrossover>();
    };

    // Result extractor -- collects final portfolio value
    auto extractor = [](const stratforge::Cerebro& cerebro,
                        const stratforge::ParamMap&) -> stratforge::OptResult {
        stratforge::OptResult result;
        result.final_value = cerebro.broker().cash();
        result.total_trades = cerebro.broker().closed_trades().size();
        return result;
    };

    // Data feed pointers for optimizer
    std::vector<stratforge::DataFeed*> feeds = {feed.get()};

    // Run optimization (parallel grid search)
    std::cout << "Running " << 4 * 4 << " parameter combinations...\n\n";
    auto results = optimizer.run(feeds, factory, ranges, extractor);

    // Sort by final value descending
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.final_value > b.final_value; });

    // Print results
    std::cout << "Results (sorted by final value):\n";
    std::cout << "  Fast  Slow  Final Value    Trades\n";
    std::cout << "  ----  ----  -----------    ------\n";

    for (const auto& r : results) {
        auto fast = std::get<std::int64_t>(r.params.at("fast_period"));
        auto slow = std::get<std::int64_t>(r.params.at("slow_period"));
        std::cout << "  " << fast << "     " << slow
                  << "    $" << r.final_value
                  << "       " << r.total_trades << "\n";
    }

    if (!results.empty()) {
        const auto& best = results.front();
        auto best_fast = std::get<std::int64_t>(best.params.at("fast_period"));
        auto best_slow = std::get<std::int64_t>(best.params.at("slow_period"));
        std::cout << "\nBest: fast=" << best_fast << " slow=" << best_slow
                  << " -> $" << best.final_value << "\n";
    }

    std::cout << "\n=== Example Complete ===\n";
    return 0;
}
