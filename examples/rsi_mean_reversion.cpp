// RSI Mean Reversion Strategy Example
// Demonstrates: Threshold buy/sell, position sizing, DrawDown analyzer

#include <stratforge/analyzers/drawdown.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/broker/sizer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>

namespace {
std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}
} // namespace

#include <iostream>
#include <memory>

// Strategy: Buy when RSI < 30 (oversold), sell when RSI > 70 (overbought)
class RSI_MeanReversionStrategy : public stratforge::Strategy {
public:
    stratforge::ParamMap default_params() const override {
        return {
            {"rsi_period", stratforge::ParamValue{std::int64_t{14}}},
            {"oversold", stratforge::ParamValue{30.0}},
            {"overbought", stratforge::ParamValue{70.0}},
        };
    }

    void init() override {
        const auto period = static_cast<int>(std::get<std::int64_t>(params().at("rsi_period")));
        oversold_ = std::get<double>(params().at("oversold"));
        overbought_ = std::get<double>(params().at("overbought"));

        rsi_ = std::make_unique<stratforge::RSI>(data().close(), period);

        // Set percentage-based position sizer (95% of available cash)
        setsizer(std::make_unique<stratforge::PercentSizer>(95.0));

        std::cout << "Strategy initialized: RSI(" << period << ") mean reversion\n";
        std::cout << "  Oversold threshold: " << oversold_ << "\n";
        std::cout << "  Overbought threshold: " << overbought_ << "\n";
    }

    void next() override {
        rsi_->next();

        if (rsi_->line().size() == 0) {
            return;
        }

        const double rsi_value = rsi_->line()[0];

        // Entry: Buy when oversold
        if (!position().size && rsi_value < oversold_) {
            (void)buy(); // Use sizer to determine position size
            std::cout << "BUY (oversold) at bar " << data().index()
                      << ": RSI=" << rsi_value << " price=" << data().close()[0] << "\n";
        }
        // Exit: Sell when overbought
        else if (position().size > 0 && rsi_value > overbought_) {
            (void)close();
            std::cout << "SELL (overbought) at bar " << data().index()
                      << ": RSI=" << rsi_value << " price=" << data().close()[0] << "\n";
        }
    }

private:
    std::unique_ptr<stratforge::RSI> rsi_;
    double oversold_ = 30.0;
    double overbought_ = 70.0;
};

int main() {
    std::cout << "=== RSI Mean Reversion Strategy Example ===\n\n";

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

    // Add strategy with custom parameters
    cerebro.add_strategy_with_params<RSI_MeanReversionStrategy>(
        stratforge::ParamMap{
            {"rsi_period", stratforge::ParamValue{std::int64_t{14}}},
            {"oversold", stratforge::ParamValue{35.0}},    // Tuned for sample dataset
            {"overbought", stratforge::ParamValue{65.0}},  // Tuned for sample dataset
        }
    );

    // Add analyzers
    auto& trade_analyzer = cerebro.add_analyzer<stratforge::TradeAnalyzer>();
    auto& drawdown = cerebro.add_analyzer<stratforge::DrawDown>();

    // Set broker
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    std::cout << "Starting cash: $" << cerebro.broker().cash() << "\n\n";

    // Run
    cerebro.run();

    // Results
    std::cout << "\n=== Results ===\n";
    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";

    const auto& trades = trade_analyzer.get_analysis();
    std::cout << "\nTrade Statistics:\n";
    std::cout << "  Total trades: " << trades.total.total << "\n";
    std::cout << "  Won: " << trades.won.total << " | Lost: " << trades.lost.total << "\n";
    if (trades.total.total > 0) {
        std::cout << "  Win rate: "
                  << (100.0 * trades.won.total / trades.total.total) << "%\n";
        std::cout << "  Total P&L: $" << trades.pnl.net.total << "\n";
    }

    const auto& dd = drawdown.get_analysis();
    std::cout << "\nDrawdown:\n";
    std::cout << "  Max drawdown: " << dd.max.drawdown << "%\n";
    std::cout << "  Max drawdown $: $" << dd.max.moneydown << "\n";

    std::cout << "\n=== Example Complete ===\n";
    return 0;
}
