// SPDX-License-Identifier: MIT
//
// examples/streaming_run.cpp
//
// -C: minimal end-to-end demo of `stratforge::IncrementBatcher`.
//
// Wires a deterministic SMA-crossover strategy into Cerebro and attaches
// an IncrementBatcher that prints one line per flush:
//
//   seq=<n> processed=<bars>/<total> bars=<count> trades=<count>
//     pnl=<realized> dd=<current_dd_pct>%   [FINAL]
//
// Out-of-process consumers (e.g. StratCraft desktop's stratforge-runner)
// substitute a JSON-emitting callback at this seam — see
// docs/observers/increment_batcher.md for the full contract.

#include <stratforge/data/csv_data.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/observers/increment_batcher.hpp>
#include <stratforge/observers/increment_types.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>

namespace {

class StreamingDemoStrategy final : public stratforge::Strategy {
public:
    void init() override {
        fast_ = std::make_unique<stratforge::SMA>(data().close(), 10);
        slow_ = std::make_unique<stratforge::SMA>(data().close(), 30);
    }

    void next() override {
        fast_->next();
        slow_->next();

        if (fast_->line().size() == 0 || slow_->line().size() == 0) {
            return;
        }
        const double f = fast_->line()[0];
        const double s = slow_->line()[0];
        if (!position().is_long() && f > s) {
            static_cast<void>(buy(1.0));
        } else if (position().is_long() && f < s) {
            static_cast<void>(close());
        }
    }

private:
    std::unique_ptr<stratforge::SMA> fast_;
    std::unique_ptr<stratforge::SMA> slow_;
};

[[nodiscard]] std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

}  // namespace

int main() {
    stratforge::Cerebro cerebro;

    auto feed = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
        .filename    = source_path("tools/golden_extract/datas/2006-day-001.txt"),
        .columns     = {},
        .date_format = "%Y-%m-%d",
        .separator   = ',',
        .has_headers = true,
        .fromdate    = std::nullopt,
        .todate      = std::nullopt,
    });

    if (!feed->load()) {
        std::cerr << "Failed to load fixture data\n";
        return EXIT_FAILURE;
    }
    std::cout << "Loaded " << feed->size() << " bars\n";

    cerebro.add_data(std::move(feed));
    cerebro.add_strategy<StreamingDemoStrategy>();
    cerebro.set_cash(10'000.0);

    // The integration seam: pass a callback that gets a fully-populated
    // IncrementSnapshot on every flush. In a real out-of-process runner,
    // this is where you'd serialize to JSON and write to stdout.
    cerebro.add_observer(std::make_unique<stratforge::IncrementBatcher>(
        stratforge::IncrementBatcher::Config{
            .max_bars_per_batch         = 50,
            .max_interval               = std::chrono::milliseconds{100},
            .emit_first_bar_immediately = true,
        },
        [](const stratforge::IncrementSnapshot& s, const std::vector<stratforge::DataFeed*>&) {
            // Print summary fields aligned with the consumer-contract
            // wire shape: totalPnl = realized + unrealized,
            // totalReturn (%) replaces the old per-POD realized_pnl print,
            // and per-snapshot win-rate is now first-class.
            const auto& m = s.current_metrics;
            const double total_pnl = m.realized_pnl + m.unrealized_pnl;
            std::cout << "seq=" << s.seq
                      << " processed=" << s.processed_bars;
            if (s.total_bars.has_value()) {
                std::cout << '/' << *s.total_bars;
            }
            std::cout << " bars="     << s.new_bars.size()
                      << " trades="   << s.new_trades.size()
                      << " totalPnl=" << total_pnl
                      << " totalReturn=" << m.total_return_pct << '%'
                      << " winRate="  << m.win_rate_pct << '%';
            if (!s.new_equity_points.empty()) {
                const auto& last_pt = s.new_equity_points.back();
                std::cout << " lastEquity=" << last_pt.portfolio_value
                          << " lastDd="     << last_pt.drawdown_pct << '%';
            }
            if (s.is_final) {
                std::cout << "   [FINAL]";
            }
            std::cout << '\n';
        }));

    cerebro.run();
    return EXIT_SUCCESS;
}
