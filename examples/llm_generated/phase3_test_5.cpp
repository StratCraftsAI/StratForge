// LLM-Generated Strategy: EMA + ATR Trailing Stop
// Prompt: "Buy when price is above EMA(20), use 2x ATR(14) as trailing stop distance."
// Phase: internal reference -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class EmaAtrTrailing : public stratforge::Strategy {
public:
    void init() override {
        ema_ = std::make_unique<stratforge::EMA>(data().close(), 20);
        atr_ = std::make_unique<stratforge::ATR>(data().high(), data().low(), data().close(), 14);
    }

    void next() override {
        if (ema_->line().size() == 0 || atr_->line().size() == 0) {
            return;
        }

        const double price = data().close()[0];
        const double ema_val = ema_->line()[0];
        const double atr_val = atr_->line()[0];

        if (!position().size) {
            // Entry: price above EMA
            if (price > ema_val) {
                (void)buy();
                trailing_stop_ = price - 2.0 * atr_val;
            }
        } else {
            // Update trailing stop
            const double new_stop = price - 2.0 * atr_val;
            trailing_stop_ = std::max(trailing_stop_, new_stop);

            // Exit: price falls below trailing stop
            if (price < trailing_stop_) {
                (void)close();
                trailing_stop_ = 0.0;
            }
        }
    }

private:
    std::unique_ptr<stratforge::EMA> ema_;
    std::unique_ptr<stratforge::ATR> atr_;
    double trailing_stop_ = 0.0;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 5: EMA + ATR Trailing Stop ===\n\n";

    stratforge::Cerebro cerebro;

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

    cerebro.add_data(std::move(feed));
    cerebro.add_strategy<EmaAtrTrailing>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
