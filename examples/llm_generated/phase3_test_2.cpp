// LLM-Generated Strategy: RSI Mean Reversion
// Prompt: "Buy when RSI(14) drops below 30, sell when RSI rises above 70."
// Phase: TICKET_004 Phase 3 — End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class RsiMeanReversion : public stratforge::Strategy {
public:
    void init() override {
        rsi_ = std::make_unique<stratforge::RSI>(data().close(), 14);
    }

    void next() override {
        if (rsi_->line().size() == 0) {
            return;
        }

        const double rsi_val = rsi_->line()[0];

        // Oversold: buy
        if (!position().size && rsi_val < 30.0) {
            (void)buy();
        }
        // Overbought: sell
        else if (position().size > 0 && rsi_val > 70.0) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::RSI> rsi_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 2: RSI Mean Reversion ===\n\n";

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
    cerebro.add_strategy<RsiMeanReversion>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
