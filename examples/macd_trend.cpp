// MACD Trend Following Strategy Example
// Demonstrates: Signal crossover, SharpeRatio analyzer

#include <stratforge/analyzers/sharpe_ratio.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>

namespace {
std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}
} // namespace


#include <iostream>
#include <memory>

// Strategy: Buy when MACD crosses above signal, sell when crosses below
class MACD_TrendStrategy : public stratforge::Strategy {
public:
    void init() override {
        // MACD with standard parameters: 12, 26, 9
        macd_ = std::make_unique<stratforge::MACD>(data().close(), 12, 26, 9);

        std::cout << "Strategy initialized: MACD(12,26,9) trend following\n";
    }

    void next() override {
        macd_->next();

        if (macd_->macd().size() == 0 || macd_->signal().size() == 0) {
            return;
        }

        const double macd_line = macd_->macd()[0];
        const double signal_line = macd_->signal()[0];
        const double histogram = macd_->histogram()[0];

        // Entry: MACD crosses above signal (bullish)
        if (!position().size && macd_line > signal_line && histogram > 0) {
            const double size = 2.0;
            (void)buy(size);
            std::cout << "BUY signal at bar " << data().index()
                      << ": MACD=" << macd_line << " signal=" << signal_line
                      << " histogram=" << histogram << "\n";
        }
        // Exit: MACD crosses below signal (bearish)
        else if (position().size > 0 && macd_line < signal_line) {
            (void)close();
            std::cout << "SELL signal at bar " << data().index()
                      << ": MACD=" << macd_line << " signal=" << signal_line
                      << " histogram=" << histogram << "\n";
        }
    }

private:
    std::unique_ptr<stratforge::MACD> macd_;
};

int main() {
    std::cout << "=== MACD Trend Following Strategy Example ===\n\n";

    stratforge::Cerebro cerebro;

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
        std::cerr << "Failed to load data file\n";
        return 1;
    }

    std::cout << "Loaded " << feed->size() << " bars\n\n";

    cerebro.add_data(std::move(feed));
    cerebro.add_strategy<MACD_TrendStrategy>();

    // Add analyzers
    auto& trade_analyzer = cerebro.add_analyzer<stratforge::TradeAnalyzer>();
    auto& sharpe = cerebro.add_analyzer<stratforge::SharpeRatio>();

    // Broker setup
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    const double starting_cash = cerebro.broker().cash();
    std::cout << "Starting cash: $" << starting_cash << "\n\n";

    // Run backtest
    cerebro.run();

    // Results
    const double final_cash = cerebro.broker().cash();
    const double return_pct = ((final_cash - starting_cash) / starting_cash) * 100.0;

    std::cout << "\n=== Results ===\n";
    std::cout << "Starting cash: $" << starting_cash << "\n";
    std::cout << "Final cash: $" << final_cash << "\n";
    std::cout << "Return: " << return_pct << "%\n";

    const auto& trades = trade_analyzer.get_analysis();
    std::cout << "\nTrade Statistics:\n";
    std::cout << "  Total trades: " << trades.total.total << "\n";
    std::cout << "  Won: " << trades.won.total << " | Lost: " << trades.lost.total << "\n";
    if (trades.total.total > 0) {
        std::cout << "  Win rate: "
                  << (100.0 * trades.won.total / trades.total.total) << "%\n";
        std::cout << "  Total P&L: $" << trades.pnl.net.total << "\n";
        std::cout << "  Avg P&L per trade: $"
                  << (trades.pnl.net.total / trades.total.total) << "\n";
    }

    const auto& sharpe_analysis = sharpe.get_analysis();
    std::cout << "\nRisk-Adjusted Performance:\n";
    std::cout << "  Sharpe Ratio: " << sharpe_analysis.sharperatio << "\n";

    std::cout << "\n=== Example Complete ===\n";
    return 0;
}
