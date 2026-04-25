// Bollinger Bands Strategy Example
// Demonstrates: Band breakout, stop-loss orders, order notifications

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>

namespace {
std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}
} // namespace


#include <iostream>
#include <memory>

// Strategy: Buy on lower band touch, sell on upper band touch
class BollingerBandsStrategy : public stratforge::Strategy {
public:
    void init() override {
        // Bollinger Bands: 20-period SMA with 2 standard deviations
        bbands_ = std::make_unique<stratforge::BollingerBands>(data().close(), 20, 2.0);

        std::cout << "Strategy initialized: BollingerBands(20, 2.0)\n";
        std::cout << "  Entry: Price touches lower band\n";
        std::cout << "  Exit: Price touches upper band or stop-loss\n";
    }

    void next() override {
        if (bbands_->top().size() == 0 || bbands_->bottom().size() == 0) {
            return;
        }

        const double price = data().close()[0];
        const double upper_band = bbands_->top()[0];
        const double lower_band = bbands_->bottom()[0];
        const double middle_band = bbands_->mid()[0];

        // Entry: Price touches or breaks below lower band (oversold)
        if (!position().size && price <= lower_band) {
            const double size = 100.0;
            const double stop_price = price * 0.95; // 5% stop-loss

            (void)buy(size);
            stop_order_id_ = sell(size, stop_price, stratforge::OrderType::Stop);

            std::cout << "BUY at bar " << data().index()
                      << ": price=" << price << " lower_band=" << lower_band
                      << " stop=" << stop_price << "\n";
        }
        // Exit: Price touches upper band (overbought)
        else if (position().size > 0 && price >= upper_band) {
            // Cancel stop-loss order
            if (stop_order_id_ != 0) {
                (void)cancel(stop_order_id_);
                stop_order_id_ = 0;
            }

            (void)close();
            std::cout << "SELL (target) at bar " << data().index()
                      << ": price=" << price << " upper_band=" << upper_band << "\n";
        }

        // Suppress unused variable warning
        (void)middle_band;
    }

    void notify_order(const stratforge::Order& order) override {
        if (order.status == stratforge::OrderStatus::Completed) {
            if (order.side == stratforge::OrderSide::Buy) {
                std::cout << "  -> BUY order completed: size=" << order.executed_size
                          << " price=" << order.executed_price << "\n";
            } else {
                std::cout << "  -> SELL order completed: size=" << order.executed_size
                          << " price=" << order.executed_price << "\n";
            }
        } else if (order.status == stratforge::OrderStatus::Canceled) {
            std::cout << "  -> Order canceled: id=" << order.id << "\n";
        }
    }

private:
    std::unique_ptr<stratforge::BollingerBands> bbands_;
    std::size_t stop_order_id_ = 0;
};

int main() {
    std::cout << "=== Bollinger Bands Strategy Example ===\n\n";

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
    cerebro.add_strategy<BollingerBandsStrategy>();

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
