#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/analytics/extended_metrics.hpp>

#include <limits>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;

TEST_CASE("Extended metrics match reference formulas", "[analytics][extended]") {
    const std::vector<double> equity_curve{1000.0, 1010.0, 990.0, 980.0, 1005.0, 970.0};
    const std::vector<double> trade_pnls{20.0, -10.0, 15.0, -5.0};

    const auto metrics = ExtendedMetricsCalculator::compute(
        equity_curve,
        trade_pnls,
        {
            .initial_capital = 1000.0,
            .risk_free_rate = 0.0,
            .trading_days_per_year = 6,
            .value_at_risk_quantile = 0.05,
        });

    REQUIRE(metrics.total_trades == 4);
    REQUIRE(metrics.winning_trades == 2);
    REQUIRE(metrics.losing_trades == 2);
    REQUIRE_THAT(metrics.win_rate, WithinRel(50.0, 1e-12));
    REQUIRE_THAT(metrics.average_win, WithinRel(17.5, 1e-12));
    REQUIRE_THAT(metrics.average_loss, WithinRel(7.5, 1e-12));
    REQUIRE_THAT(metrics.profit_factor, WithinRel(2.3333333333333335, 1e-12));
    REQUIRE_THAT(metrics.expectancy, WithinRel(5.0, 1e-12));

    REQUIRE_THAT(metrics.annualized_return, WithinRel(-3.6, 1e-12));
    REQUIRE_THAT(metrics.cagr, WithinRel(-3.0, 1e-12));
    REQUIRE_THAT(metrics.volatility, WithinRel(5.23871887578576, 1e-12));
    REQUIRE_THAT(metrics.max_drawdown, WithinRel(3.9603960396039604, 1e-12));
    REQUIRE_THAT(metrics.sortino_ratio, WithinRel(-1.4072734839851386, 1e-12));
    REQUIRE_THAT(metrics.calmar_ratio, WithinRel(-0.7575, 1e-12));
    REQUIRE_THAT(metrics.value_at_risk95, WithinRel(3.18210925570169, 1e-12));
}

TEST_CASE("Extended metrics handle empty and win-only trade sets", "[analytics][extended]") {
    SECTION("Empty inputs return zeros") {
        const auto metrics = ExtendedMetricsCalculator::compute(
            std::vector<double>{},
            std::vector<double>{});

        REQUIRE(metrics.total_trades == 0);
        REQUIRE(metrics.winning_trades == 0);
        REQUIRE(metrics.losing_trades == 0);
        REQUIRE(metrics.profit_factor == 0.0);
        REQUIRE(metrics.expectancy == 0.0);
        REQUIRE(metrics.value_at_risk95 == 0.0);
    }

    SECTION("No losses yield infinite profit factor and finite expectancy") {
        const auto metrics = ExtendedMetricsCalculator::compute(
            std::vector<double>{1000.0, 1010.0, 1020.0},
            std::vector<double>{5.0, 15.0},
            {
                .initial_capital = 1000.0,
                .trading_days_per_year = 3,
            });

        REQUIRE(metrics.total_trades == 2);
        REQUIRE(metrics.losing_trades == 0);
        REQUIRE(metrics.winning_trades == 2);
        REQUIRE(metrics.profit_factor == std::numeric_limits<double>::infinity());
        REQUIRE_THAT(metrics.expectancy, WithinRel(10.0, 1e-12));
    }
}
