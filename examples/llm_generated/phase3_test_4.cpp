// LLM-Generated Strategy: Bollinger Bands Breakout
// Prompt: "Buy when price breaks above upper Bollinger Band, sell when it falls below lower band."
// Phase: internal reference -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class BollingerBreakout : public stratforge::Strategy {
public:
    void init() override {
        bb_ = std::make_unique<stratforge::BollingerBands>(data().close(), 20, 2.0);
    }

    void next() override {
        if (bb_->top().size() == 0 || bb_->bottom().size() == 0) {
            return;
        }

        const double price = data().close()[0];
        const double upper = bb_->top()[0];
        const double lower = bb_->bottom()[0];

        // Price breaks above upper band: buy
        if (!position().size && price > upper) {
            (void)buy();
        }
        // Price falls below lower band: sell
        else if (position().size > 0 && price < lower) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::BollingerBands> bb_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 4: Bollinger Bands Breakout ===\n\n";

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
    cerebro.add_strategy<BollingerBreakout>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
