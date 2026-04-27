// LLM-Generated Strategy: TRIX Momentum
// Prompt: "Buy when TRIX(15) crosses above zero, sell when it crosses below zero."
// Phase: internal reference -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/trix.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class TrixMomentum : public stratforge::Strategy {
public:
    void init() override {
        trix_ = std::make_unique<stratforge::TRIX>(data().close(), 15);
    }

    void next() override {
        if (trix_->line().size() == 0) {
            return;
        }

        const double trix_val = trix_->line()[0];
        const double trix_prev = trix_->line()[-1];

        // TRIX crosses above zero: buy
        if (!position().size && trix_val > 0.0 && trix_prev <= 0.0) {
            (void)buy();
        }
        // TRIX crosses below zero: sell
        else if (position().size > 0 && trix_val < 0.0 && trix_prev >= 0.0) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::TRIX> trix_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 9: TRIX Momentum ===\n\n";

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
    cerebro.add_strategy<TrixMomentum>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
