// End-to-End Integration Test (TICKET_003_10_10 Phase 10.5)
// Comprehensive test exercising the full stratforge pipeline:
// CSV load → Cerebro → Strategy → Analyzers → Observers → Report extraction

#include <stratforge/analyzers/drawdown.hpp>
#include <stratforge/analyzers/returns.hpp>
#include <stratforge/analyzers/sharpe_ratio.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/broker/sizer.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/observers/buy_sell.hpp>
#include <stratforge/observers/value.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

using namespace stratforge;
using Catch::Matchers::WithinRel;

namespace {

std::string source_path(const std::string& relative) {
    auto path = std::filesystem::path(SF_SOURCE_DIR) / relative;
    return path.string();
}

// SMA(10) / SMA(30) Crossover Strategy with PercentSizer(95%)
class E2E_SMA_CrossoverStrategy : public Strategy {
public:
    void init() override {
        // Create two SMAs
        sma_fast_ = std::make_unique<SMA>(data().close(), 10);
        sma_slow_ = std::make_unique<SMA>(data().close(), 30);

        // Set position sizer to 95% of available cash
        setsizer(std::make_unique<PercentSizer>(95.0));
    }

    void next() override {
        // Advance indicators on every bar
        sma_fast_->next();
        sma_slow_->next();

        // Wait for slow SMA warmup (period=30)
        if (sma_slow_->line().size() < 30) {
            return;
        }

        const double fast = sma_fast_->line()[0];
        const double slow = sma_slow_->line()[0];

        // Entry: fast crosses above slow
        if (!position().size && fast > slow) {
            (void)buy(); // Use sizer to determine position size
            trades_entered_++;
        }
        // Exit: fast crosses below slow
        else if (position().size > 0 && fast < slow) {
            (void)close();
            trades_exited_++;
        }
    }

    [[nodiscard]] int trades_entered() const noexcept { return trades_entered_; }
    [[nodiscard]] int trades_exited() const noexcept { return trades_exited_; }

private:
    std::unique_ptr<SMA> sma_fast_;
    std::unique_ptr<SMA> sma_slow_;
    int trades_entered_ = 0;
    int trades_exited_ = 0;
};

} // namespace

TEST_CASE("End-to-end integration: CSV → Cerebro → Strategy → Analyzers → Observers", "[e2e][integration]") {
    SECTION("Full pipeline with SMA crossover strategy") {
        Cerebro cerebro;

        // Load CSV data
        auto feed = std::make_unique<CsvData>(CsvData::Params{
            .filename = source_path("tools/golden_extract/datas/2005-2006-day-001.txt"),
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });

        REQUIRE(feed->load());
        const auto num_bars = feed->size();
        REQUIRE(num_bars == 512); // 2005-2006 daily data

        cerebro.add_data(std::move(feed));

        // Add strategy
        auto& strategy = cerebro.add_strategy<E2E_SMA_CrossoverStrategy>();

        // Add analyzers
        auto& trade_analyzer = cerebro.add_analyzer<TradeAnalyzer>();
        auto& sharpe = cerebro.add_analyzer<SharpeRatio>();
        auto& drawdown = cerebro.add_analyzer<Drawdown>();
        auto& returns = cerebro.add_analyzer<Returns>();

        // Add observers (mainly for visualization, no need to verify data here)
        cerebro.add_observer<BuySellObserver>();
        cerebro.add_observer<ValueObserver>();

        // Configure broker
        cerebro.set_cash(10000.0);
        cerebro.set_commission(CommissionInfo{.commission = 0.001}); // 0.1%

        const double starting_cash = cerebro.broker().cash();
        REQUIRE_THAT(starting_cash, WithinRel(10000.0, 1e-10));

        // Run backtest
        cerebro.run(Cerebro::RunOptions{
            .runonce = false,
            .preload = true,
        });

        const double final_cash = cerebro.broker().cash();

        // Verify strategy executed
        REQUIRE(strategy.trades_entered() > 0);
        // trades_exited can be 0 if last position is still open
        REQUIRE(strategy.trades_exited() < strategy.trades_entered() + 1);

        // Verify TradeAnalyzer results
        const auto& trades = trade_analyzer.get_analysis();
        // Closed trades = won + lost; total = closed + open
        REQUIRE(trades.won.total + trades.lost.total == trades.total.closed);
        REQUIRE(trades.total.closed + trades.total.open == trades.total.total);

        // If trades occurred, verify P&L tracking
        if (trades.total.total > 0) {
            const double total_pnl = trades.pnl.net.total;
            REQUIRE(std::isfinite(total_pnl));
        }

        // Verify SharpeRatio calculation
        const auto& sharpe_analysis = sharpe.get_analysis();
        REQUIRE(std::isfinite(sharpe_analysis.sharperatio));

        // Verify Drawdown tracking
        const auto& dd = drawdown.get_analysis();
        REQUIRE(std::isfinite(dd.max.drawdown));
        REQUIRE(dd.max.drawdown >= 0.0); // Drawdown is non-negative percentage

        // Verify Returns tracking
        const auto& rets = returns.get_analysis();
        REQUIRE(std::isfinite(rets.rtot));

        // Verify final portfolio value is positive
        REQUIRE(final_cash > 0.0);

        // Log results for manual inspection
        std::cout << "\n=== E2E Integration Test Results ===\n";
        std::cout << "Starting cash: $" << starting_cash << "\n";
        std::cout << "Final cash: $" << final_cash << "\n";
        std::cout << "Return: " << ((final_cash - starting_cash) / starting_cash * 100.0) << "%\n";
        std::cout << "\nTrades:\n";
        std::cout << "  Total: " << trades.total.total << "\n";
        std::cout << "  Won: " << trades.won.total << " | Lost: " << trades.lost.total << "\n";
        if (trades.total.total > 0) {
            std::cout << "  Win rate: " << (100.0 * trades.won.total / trades.total.total) << "%\n";
            std::cout << "  Total P&L: $" << trades.pnl.net.total << "\n";
        }
        std::cout << "\nRisk Metrics:\n";
        std::cout << "  Sharpe Ratio: " << sharpe_analysis.sharperatio << "\n";
        std::cout << "  Max Drawdown: " << dd.max.drawdown << "%\n";
        std::cout << "  Total Return: " << rets.rtot << "%\n";
        std::cout << "=== E2E Test: PASS ===\n";
    }

    SECTION("Pipeline handles zero trades gracefully") {
        // Strategy that never trades
        class NoTradeStrategy : public Strategy {
        public:
            void next() override {
                // Never place any orders
            }
        };

        Cerebro cerebro;

        auto feed = std::make_unique<CsvData>(CsvData::Params{
            .filename = source_path("tools/golden_extract/datas/2006-day-001.txt"),
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });

        REQUIRE(feed->load());
        cerebro.add_data(std::move(feed));
        cerebro.add_strategy<NoTradeStrategy>();

        auto& trade_analyzer = cerebro.add_analyzer<TradeAnalyzer>();
        auto& sharpe = cerebro.add_analyzer<SharpeRatio>();
        auto& drawdown = cerebro.add_analyzer<Drawdown>();

        cerebro.set_cash(10000.0);
        cerebro.run();

        // Verify analyzers handle zero trades
        const auto& trades = trade_analyzer.get_analysis();
        REQUIRE(trades.total.total == 0);
        REQUIRE(trades.won.total == 0);
        REQUIRE(trades.lost.total == 0);

        const auto& sharpe_analysis = sharpe.get_analysis();
        REQUIRE(sharpe_analysis.sharperatio == 0.0); // Zero trades → zero Sharpe

        const auto& dd = drawdown.get_analysis();
        REQUIRE(dd.max.drawdown == 0.0); // No trades → no drawdown

        // Cash should be unchanged
        REQUIRE_THAT(cerebro.broker().cash(), WithinRel(10000.0, 1e-10));
    }
}
