// Multi-Timeframe Strategy Example
// Demonstrates: Resampler, multi-data access, timeframe alignment

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/resampler.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>

namespace {
std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}
} // namespace


#include <iostream>
#include <memory>

// Strategy: Trade on daily data, use weekly EMA for trend filter
class MultiTimeframeStrategy : public stratforge::Strategy {
public:
    void init() override {
        // Daily EMA (fast)
        daily_ema_ = std::make_unique<stratforge::EMA>(data(0).close(), 20);

        // Weekly EMA (slow trend filter)
        weekly_ema_ = std::make_unique<stratforge::EMA>(data(1).close(), 10);

        std::cout << "Strategy initialized: Multi-timeframe\n";
        std::cout << "  Daily data: " << data_name(0) << "\n";
        std::cout << "  Weekly data: " << data_name(1) << "\n";
        std::cout << "  Daily EMA(20) + Weekly EMA(10) trend filter\n";
    }

    void next() override {
        // Check if indicators have valid values
        if (daily_ema_->line().size() == 0 || weekly_ema_->line().size() == 0) {
            return;
        }

        const double daily_price = data(0).close()[0];
        const double daily_ema = daily_ema_->line()[0];
        const double weekly_ema = weekly_ema_->line()[0];

        // Trend filter: only trade when weekly trend is up
        const bool weekly_uptrend = daily_price > weekly_ema;

        // Entry: Daily price crosses above daily EMA, and weekly trend is up
        if (!position().size && daily_price > daily_ema && weekly_uptrend) {
            (void)buy(50.0);
            std::cout << "BUY at bar " << data(0).index()
                      << ": price=" << daily_price
                      << " daily_ema=" << daily_ema
                      << " weekly_ema=" << weekly_ema << "\n";
        }
        // Exit: Daily price crosses below daily EMA
        else if (position().size > 0 && daily_price < daily_ema) {
            (void)close();
            std::cout << "SELL at bar " << data(0).index()
                      << ": price=" << daily_price
                      << " daily_ema=" << daily_ema << "\n";
        }
    }

private:
    std::unique_ptr<stratforge::EMA> daily_ema_;
    std::unique_ptr<stratforge::EMA> weekly_ema_;
};

int main() {
    std::cout << "=== Multi-Timeframe Strategy Example ===\n\n";

    stratforge::Cerebro cerebro;

    // Load daily data
    auto daily_feed = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
        .filename = source_path("tools/golden_extract/datas/2006-day-001.txt"),
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });

    if (!daily_feed->load()) {
        std::cerr << "Failed to load daily data\n";
        return 1;
    }

    std::cout << "Loaded daily data: " << daily_feed->size() << " bars\n";

    // Create weekly resampled data from daily
    auto weekly_feed = std::make_unique<stratforge::Resampler>(
        *daily_feed,
        stratforge::TimeFrame::Weeks,
        1
    );

    std::cout << "Created weekly resampled data\n\n";

    // Add both feeds to Cerebro
    cerebro.add_data(std::move(daily_feed), "daily");
    cerebro.add_data(std::move(weekly_feed), "weekly");

    cerebro.add_strategy<MultiTimeframeStrategy>();
    auto& analyzer = cerebro.add_analyzer<stratforge::TradeAnalyzer>();

    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    std::cout << "Starting cash: $" << cerebro.broker().cash() << "\n\n";

    cerebro.run();

    std::cout << "\n=== Results ===\n";
    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";

    const auto& analysis = analyzer.get_analysis();
    std::cout << "\nTrade Analysis:\n";
    std::cout << "  Total trades: " << analysis.total.total << "\n";
    std::cout << "  Won: " << analysis.won.total << " | Lost: " << analysis.lost.total << "\n";
    if (analysis.total.total > 0) {
        std::cout << "  Win rate: "
                  << (100.0 * analysis.won.total / analysis.total.total) << "%\n";
        std::cout << "  Total P&L: $" << analysis.pnl.net.total << "\n";
    }

    std::cout << "\n=== Example Complete ===\n";
    return 0;
}
