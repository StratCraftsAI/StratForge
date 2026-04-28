#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/analyzers/drawdown.hpp>
#include <stratforge/analyzers/returns.hpp>
#include <stratforge/analyzers/sharpe_ratio.hpp>
#include <stratforge/analyzers/trade_analyzer.hpp>
#include <stratforge/engine/cerebro.hpp>
#include <stratforge/observers/buy_sell.hpp>
#include <stratforge/observers/value.hpp>

#include "golden_reference.hpp"
#include "test_helpers.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

std::string source_path(const std::string& relative) {
    return std::string(SF_SOURCE_DIR) + "/" + relative;
}

class AnalyticsStrategy final : public Strategy {
public:
    void nextstart() override {
        static_cast<void>(buy(1.0));
    }

    void next() override {
        if (data().index() == 2 && position().is_long()) {
            static_cast<void>(close());
        }
    }
};

void require_golden_value(double actual, const std::string& expected) {
    const double parsed = stratforge::test::parse_golden_double(expected);
    if (std::isnan(parsed)) {
        REQUIRE(std::isnan(actual));
    } else {
        REQUIRE_THAT(actual, WithinRel(parsed, 1e-12));
    }
}

} // namespace

TEST_CASE("Phase 6 analytics match backtrader golden reference", "[golden][analytics]") {
    const auto fixture = stratforge::test::load_analytics_golden_reference(
        source_path("tools/golden_extract/output/analyzers/analytics_reference.json"));

    Cerebro cerebro;
    cerebro.set_cash(fixture.initial_cash);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.5},
        {110.0, 111.0, 109.0, 110.5},
        {90.0, 91.0, 89.0, 90.5},
        {120.0, 121.0, 119.0, 120.5},
    }, "main", 1000.0, 0.0));
    cerebro.add_strategy<AnalyticsStrategy>();

    auto& trade_analyzer = cerebro.add_analyzer<TradeAnalyzer>();
    auto& sharpe = cerebro.add_analyzer<SharpeRatio>(0.0, 252);
    auto& drawdown = cerebro.add_analyzer<Drawdown>();
    auto& returns = cerebro.add_analyzer<Returns>(252.0);
    auto& buysell = cerebro.add_observer<BuySellObserver>();
    auto& value = cerebro.add_observer<ValueObserver>();

    cerebro.run();

    const auto& trade = trade_analyzer.get_analysis();
    REQUIRE(trade.total.total == fixture.trade_analyzer.total.total);
    REQUIRE(trade.total.open == fixture.trade_analyzer.total.open);
    REQUIRE(trade.total.closed == fixture.trade_analyzer.total.closed);
    REQUIRE(trade.streak.won.current == fixture.trade_analyzer.streak.won.current);
    REQUIRE(trade.streak.won.longest == fixture.trade_analyzer.streak.won.longest);
    REQUIRE(trade.streak.lost.current == fixture.trade_analyzer.streak.lost.current);
    REQUIRE(trade.streak.lost.longest == fixture.trade_analyzer.streak.lost.longest);
    REQUIRE(trade.won.total == fixture.trade_analyzer.won.total);
    REQUIRE(trade.lost.total == fixture.trade_analyzer.lost.total);
    REQUIRE(trade.len.total_stats.total == fixture.trade_analyzer.len.total);
    REQUIRE(trade.len.total_stats.max == fixture.trade_analyzer.len.max);
    REQUIRE(trade.len.total_stats.min == fixture.trade_analyzer.len.min);
    REQUIRE_THAT(trade.pnl.gross.total, WithinRel(fixture.trade_analyzer.pnl.gross.total, 1e-12));
    REQUIRE_THAT(trade.pnl.gross.average, WithinRel(fixture.trade_analyzer.pnl.gross.average, 1e-12));
    REQUIRE_THAT(trade.pnl.net.total, WithinRel(fixture.trade_analyzer.pnl.net.total, 1e-12));
    REQUIRE_THAT(trade.pnl.net.average, WithinRel(fixture.trade_analyzer.pnl.net.average, 1e-12));
    REQUIRE_THAT(trade.won.pnl.total, WithinRel(fixture.trade_analyzer.won.pnl.total, 1e-12));
    REQUIRE_THAT(trade.won.pnl.average, WithinRel(fixture.trade_analyzer.won.pnl.average, 1e-12));
    REQUIRE_THAT(trade.won.pnl.max, WithinRel(fixture.trade_analyzer.won.pnl.max, 1e-12));
    REQUIRE_THAT(trade.lost.pnl.total, WithinRel(fixture.trade_analyzer.lost.pnl.total, 1e-12));
    REQUIRE_THAT(trade.lost.pnl.average, WithinRel(fixture.trade_analyzer.lost.pnl.average, 1e-12));
    REQUIRE_THAT(trade.lost.pnl.max, WithinRel(fixture.trade_analyzer.lost.pnl.max, 1e-12));
    REQUIRE_THAT(trade.len.total_stats.average, WithinRel(fixture.trade_analyzer.len.average, 1e-12));

    REQUIRE_THAT(sharpe.get_analysis().sharperatio, WithinRel(fixture.sharpe_ratio, 1e-12));

    const auto& dd = drawdown.get_analysis();
    REQUIRE(dd.len == fixture.drawdown.len);
    REQUIRE(dd.max.len == fixture.drawdown.max.len);
    REQUIRE_THAT(dd.drawdown, WithinRel(fixture.drawdown.drawdown, 1e-12));
    REQUIRE_THAT(dd.moneydown, WithinRel(fixture.drawdown.moneydown, 1e-12));
    REQUIRE_THAT(dd.max.drawdown, WithinRel(fixture.drawdown.max.drawdown, 1e-12));
    REQUIRE_THAT(dd.max.moneydown, WithinRel(fixture.drawdown.max.moneydown, 1e-12));

    const auto& ret = returns.get_analysis();
    REQUIRE_THAT(ret.rtot, WithinRel(fixture.returns.rtot, 1e-12));
    REQUIRE_THAT(ret.ravg, WithinRel(fixture.returns.ravg, 1e-12));
    REQUIRE_THAT(ret.rnorm, WithinRel(fixture.returns.rnorm, 1e-12));
    REQUIRE_THAT(ret.rnorm100, WithinRel(fixture.returns.rnorm100, 1e-12));

    REQUIRE(buysell.buy().size() == fixture.buy.size());
    REQUIRE(buysell.sell().size() == fixture.sell.size());
    REQUIRE(value.value().size() == fixture.value.size());
    for (std::size_t i = 0; i < fixture.buy.size(); ++i) {
        require_golden_value(buysell.buy().data()[i], fixture.buy[i]);
        require_golden_value(buysell.sell().data()[i], fixture.sell[i]);
        require_golden_value(value.value().data()[i], fixture.value[i]);
    }
}
