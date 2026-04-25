// LLM-Generated Strategy: ADX Trend Filter + EMA
// Prompt: "Only trade when ADX(14) > 25 indicating a strong trend. Buy when price is above EMA(20), sell when below."
// Phase: TICKET_004 Phase 3 -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/directionalmovement.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class AdxTrend : public stratforge::Strategy {
public:
    void init() override {
        adx_ = std::make_unique<stratforge::ADX>(
            data().high(), data().low(), data().close(), 14);
        ema_ = std::make_unique<stratforge::EMA>(data().close(), 20);
    }

    void next() override {
        if (adx_->line().size() == 0 || ema_->line().size() == 0) {
            return;
        }

        const double adx_val = adx_->line()[0];
        const double price = data().close()[0];
        const double ema_val = ema_->line()[0];

        // Only trade when ADX > 25 (strong trend)
        if (adx_val > 25.0) {
            if (!position().size && price > ema_val) {
                (void)buy();
            } else if (position().size > 0 && price < ema_val) {
                (void)close();
            }
        }
    }

private:
    std::unique_ptr<stratforge::ADX> adx_;
    std::unique_ptr<stratforge::EMA> ema_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 8: ADX Trend Filter ===\n\n";

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
    cerebro.add_strategy<AdxTrend>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
