// Pairs Trading Strategy Example
// Demonstrates: Multi-data, spread calculation, market-neutral strategy

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>

namespace {
std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}
} // namespace

#include <cmath>
#include <iostream>
#include <memory>

// Strategy: Trade the spread between two correlated assets
// This example loads the same data file for both assets and applies a synthetic
// multiplier (1.02x) to asset2 to simulate a correlated-but-different instrument.
// In production, you would load two genuinely different correlated assets.
class PairsTradingStrategy : public stratforge::Strategy {
public:
    void init() override {
        asset1_sma_ = std::make_unique<stratforge::SMA>(data(0).close(), 20);
        asset2_sma_ = std::make_unique<stratforge::SMA>(data(1).close(), 20);

        std::cout << "Strategy initialized: Pairs Trading\n";
        std::cout << "  Asset 1: " << data_name(0) << "\n";
        std::cout << "  Asset 2: " << data_name(1) << " (synthetic 1.02x multiplier)\n";
        std::cout << "  Spread threshold: " << spread_threshold_ << " std deviations\n";
    }

    void next() override {
        asset1_sma_->next();
        asset2_sma_->next();

        if (asset1_sma_->line().size() == 0 || asset2_sma_->line().size() == 0) {
            return;
        }

        const double price1 = data(0).close()[0];
        // Synthetic multiplier to simulate a correlated-but-different asset
        const double price2 = data(1).close()[0] * 1.02;
        const double spread = price1 - price2;

        // Calculate spread mean and std dev over recent history
        spread_history_.push_back(spread);
        if (spread_history_.size() > 20) {
            spread_history_.erase(spread_history_.begin());
        }

        if (spread_history_.size() < 20) {
            return;
        }

        // Calculate statistics
        double mean = 0.0;
        for (double s : spread_history_) {
            mean += s;
        }
        mean /= static_cast<double>(spread_history_.size());

        double variance = 0.0;
        for (double s : spread_history_) {
            variance += (s - mean) * (s - mean);
        }
        const double stddev = std::sqrt(variance / static_cast<double>(spread_history_.size()));

        if (stddev < 1e-10) return;

        const double z_score = (spread - mean) / stddev;

        // Entry: Spread diverges beyond threshold
        if (!position(0).size && !position(1).size) {
            // Spread too high: short asset1, long asset2
            if (z_score > spread_threshold_) {
                (void)sell(1.0, 0.0, stratforge::OrderType::Market, 0); // Short asset1
                (void)buy(1.0, 0.0, stratforge::OrderType::Market, 1);  // Long asset2
                std::cout << "OPEN pair trade at bar " << data(0).index()
                          << ": spread=" << spread << " z=" << z_score
                          << " (short asset1, long asset2)\n";
            }
            // Spread too low: long asset1, short asset2
            else if (z_score < -spread_threshold_) {
                (void)buy(1.0, 0.0, stratforge::OrderType::Market, 0);  // Long asset1
                (void)sell(1.0, 0.0, stratforge::OrderType::Market, 1); // Short asset2
                std::cout << "OPEN pair trade at bar " << data(0).index()
                          << ": spread=" << spread << " z=" << z_score
                          << " (long asset1, short asset2)\n";
            }
        }
        // Exit: Spread reverts to mean
        else if ((position(0).size || position(1).size) && std::abs(z_score) < 0.5) {
            (void)close(0);
            (void)close(1);
            std::cout << "CLOSE pair trade at bar " << data(0).index()
                      << ": spread=" << spread << " z=" << z_score << "\n";
        }
    }

private:
    std::unique_ptr<stratforge::SMA> asset1_sma_;
    std::unique_ptr<stratforge::SMA> asset2_sma_;
    std::vector<double> spread_history_;
    double spread_threshold_ = 2.0; // Z-score threshold
};

int main() {
    std::cout << "=== Pairs Trading Strategy Example ===\n\n";

    stratforge::Cerebro cerebro;

    // Load first asset
    auto feed1 = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
        .filename = source_path("tools/golden_extract/datas/2006-day-001.txt"),
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });

    // Load second asset (same data file; strategy applies synthetic multiplier)
    // In production, use different correlated assets (e.g., GLD vs SLV)
    auto feed2 = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
        .filename = source_path("tools/golden_extract/datas/2006-day-001.txt"),
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });

    if (!feed1->load() || !feed2->load()) {
        std::cerr << "Failed to load data files\n";
        return 1;
    }

    std::cout << "Loaded asset1: " << feed1->size() << " bars\n";
    std::cout << "Loaded asset2: " << feed2->size() << " bars\n\n";

    cerebro.add_data(std::move(feed1), "asset1");
    cerebro.add_data(std::move(feed2), "asset2");

    cerebro.add_strategy<PairsTradingStrategy>();
    auto& analyzer = cerebro.add_analyzer<stratforge::TradeAnalyzer>();

    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    std::cout << "Starting cash: $" << cerebro.broker().cash() << "\n\n";
    std::cout << "Note: This demo applies a 1.02x multiplier to asset2 prices.\n";
    std::cout << "      In production, load two genuinely different correlated assets.\n\n";

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
