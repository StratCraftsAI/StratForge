// LLM-Generated Strategy: Stochastic + RSI
// Prompt: "Buy when both Stochastic %K < 20 and RSI < 35, sell when Stochastic %K > 80 and RSI > 65."
// Phase: internal reference -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class StochasticRsi : public stratforge::Strategy {
public:
    void init() override {
        stoch_ = std::make_unique<stratforge::Stochastic>(
            data().high(), data().low(), data().close(), 14, 3, 3);
        rsi_ = std::make_unique<stratforge::RSI>(data().close(), 14);
    }

    void next() override {
        if (stoch_->percK().size() == 0 || rsi_->line().size() == 0) {
            return;
        }

        const double stoch_k = stoch_->percK()[0];
        const double rsi_val = rsi_->line()[0];

        // Both oversold: buy
        if (!position().size && stoch_k < 20.0 && rsi_val < 35.0) {
            (void)buy();
        }
        // Both overbought: sell
        else if (position().size > 0 && stoch_k > 80.0 && rsi_val > 65.0) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::Stochastic> stoch_;
    std::unique_ptr<stratforge::RSI> rsi_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 6: Stochastic + RSI ===\n\n";

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
    cerebro.add_strategy<StochasticRsi>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
