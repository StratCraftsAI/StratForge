// SMA Crossover Strategy Example
// Demonstrates: Buy/sell signals, TradeAnalyzer, basic reporting

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>

namespace {
std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}
} // namespace


#include <iostream>
#include <memory>

// Strategy: Buy when fast SMA crosses above slow SMA, sell when crosses below
class SMA_CrossoverStrategy : public stratforge::Strategy {
public:
    void init() override {
        // Create two simple moving averages
        sma_fast_ = std::make_unique<stratforge::SMA>(data().close(), 10);
        sma_slow_ = std::make_unique<stratforge::SMA>(data().close(), 30);

        std::cout << "Strategy initialized: SMA(" << 10 << ") / SMA(" << 30 << ") crossover\n";
    }

    void next() override {
        // Wait until both SMAs have valid values
        if (sma_fast_->line().size() == 0 || sma_slow_->line().size() == 0) {
            return;
        }

        const double fast = sma_fast_->line()[0];
        const double slow = sma_slow_->line()[0];

        // Entry signal: fast crosses above slow
        if (!position().size && fast > slow) {
            const double size = 100.0; // Fixed position size
            (void)buy(size);
            std::cout << "BUY signal at bar " << data().index()
                      << ": fast=" << fast << " slow=" << slow << "\n";
        }
        // Exit signal: fast crosses below slow
        else if (position().size > 0 && fast < slow) {
            (void)close();
            std::cout << "SELL signal at bar " << data().index()
                      << ": fast=" << fast << " slow=" << slow << "\n";
        }
    }

    void stop() override {
        std::cout << "Strategy stopped. Final position size: " << position().size << "\n";
    }

private:
    std::unique_ptr<stratforge::SMA> sma_fast_;
    std::unique_ptr<stratforge::SMA> sma_slow_;
};

int main() {
    std::cout << "=== SMA Crossover Strategy Example ===\n\n";

    // Create Cerebro engine
    stratforge::Cerebro cerebro;

    // Load CSV data
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
        std::cerr << "Failed to load data file\n";
        return 1;
    }

    std::cout << "Loaded " << feed->size() << " bars\n\n";

    // Add data feed
    cerebro.add_data(std::move(feed));

    // Add strategy
    cerebro.add_strategy<SMA_CrossoverStrategy>();

    // Add TradeAnalyzer
    auto& analyzer = cerebro.add_analyzer<stratforge::TradeAnalyzer>();

    // Set broker parameters
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001}); // 0.1% commission

    std::cout << "Starting portfolio value: $" << cerebro.broker().cash() << "\n\n";

    // Run backtest
    cerebro.run();

    // Print results
    std::cout << "\n=== Results ===\n";
    std::cout << "Final portfolio value: $" << cerebro.broker().cash() << "\n";

    const auto& analysis = analyzer.get_analysis();
    std::cout << "\nTrade Analysis:\n";
    std::cout << "  Total trades: " << analysis.total.total << "\n";
    std::cout << "  Won trades: " << analysis.won.total << "\n";
    std::cout << "  Lost trades: " << analysis.lost.total << "\n";

    if (analysis.total.total > 0) {
        std::cout << "  Win rate: "
                  << (100.0 * analysis.won.total / analysis.total.total) << "%\n";
    }

    if (analysis.pnl.net.total != 0.0) {
        std::cout << "  Total P&L: $" << analysis.pnl.net.total << "\n";
        std::cout << "  Average P&L per trade: $"
                  << (analysis.pnl.net.total / analysis.total.total) << "\n";
    }

    std::cout << "\n=== Example Complete ===\n";
    return 0;
}
