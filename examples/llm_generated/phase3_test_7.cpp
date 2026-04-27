// LLM-Generated Strategy: Triple SMA
// Prompt: "Buy when SMA(5) > SMA(20) > SMA(50), sell when SMA(5) < SMA(20)."
// Phase: internal reference -- End-to-End LLM Generation Test
// Date: 2026-04-21

#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

class TripleSma : public stratforge::Strategy {
public:
    void init() override {
        sma_fast_ = std::make_unique<stratforge::SMA>(data().close(), 5);
        sma_mid_ = std::make_unique<stratforge::SMA>(data().close(), 20);
        sma_slow_ = std::make_unique<stratforge::SMA>(data().close(), 50);
    }

    void next() override {
        if (sma_fast_->line().size() == 0 || sma_mid_->line().size() == 0
            || sma_slow_->line().size() == 0) {
            return;
        }

        const double fast = sma_fast_->line()[0];
        const double mid = sma_mid_->line()[0];
        const double slow = sma_slow_->line()[0];

        // All three aligned: buy
        if (!position().size && fast > mid && mid > slow) {
            (void)buy();
        }
        // Fast drops below mid: sell
        else if (position().size > 0 && fast < mid) {
            (void)close();
        }
    }

private:
    std::unique_ptr<stratforge::SMA> sma_fast_;
    std::unique_ptr<stratforge::SMA> sma_mid_;
    std::unique_ptr<stratforge::SMA> sma_slow_;
};

} // namespace

int main() {
    std::cout << "=== Phase 3 Test 7: Triple SMA ===\n\n";

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
    cerebro.add_strategy<TripleSma>();
    cerebro.set_cash(10000.0);
    cerebro.set_commission(stratforge::CommissionInfo{.commission = 0.001});

    cerebro.run();

    std::cout << "Final cash: $" << cerebro.broker().cash() << "\n";
    std::cout << "=== PASS ===\n";
    return 0;
}
