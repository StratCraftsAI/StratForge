// LLM-Generated Strategy: MACD Signal Crossover
// Prompt: "Buy when MACD line crosses above signal line, sell when it crosses below."
// Phase: internal reference -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class MacdSignalCrossover : public stratforge::Strategy {
public:
    void init() override {
        macd_ = std::make_unique<stratforge::MACD>(data().close(), 12, 26, 9);
    }

    void next() override {
        if (macd_->macd().size() == 0 || macd_->signal().size() == 0) {
            return;
        }

        const double macd_val = macd_->macd()[0];
        const double signal_val = macd_->signal()[0];
        const double macd_prev = macd_->macd()[-1];
        const double signal_prev = macd_->signal()[-1];

        // MACD crosses above signal: buy
        if (!position().size && macd_val > signal_val && macd_prev <= signal_prev) {
            (void)buy();
        }
        // MACD crosses below signal: sell
        else if (position().size > 0 && macd_val < signal_val && macd_prev >= signal_prev) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::MACD> macd_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 3: MACD Signal Crossover ===\n\n";

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
    cerebro.add_strategy<MacdSignalCrossover>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
