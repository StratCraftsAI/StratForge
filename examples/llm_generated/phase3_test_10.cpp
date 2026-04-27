// LLM-Generated Strategy: Williams %R Overbought/Oversold
// Prompt: "Buy when Williams %R(14) falls below -80, sell when it rises above -20."
// Phase: internal reference -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/williams.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class WilliamsRReversion : public stratforge::Strategy {
public:
    void init() override {
        wr_ = std::make_unique<stratforge::WilliamsR>(
            data().high(), data().low(), data().close(), 14);
    }

    void next() override {
        if (wr_->line().size() == 0) {
            return;
        }

        const double wr_val = wr_->line()[0];

        // Oversold: buy
        if (!position().size && wr_val < -80.0) {
            (void)buy();
        }
        // Overbought: sell
        else if (position().size > 0 && wr_val > -20.0) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::WilliamsR> wr_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 10: Williams %R ===\n\n";

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
    cerebro.add_strategy<WilliamsRReversion>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
