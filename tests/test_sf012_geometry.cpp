#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/indicators/candle_structure_common.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

template <typename Indicator>
void run_ohlc(stratforge::Line<double>& open, stratforge::Line<double>& high,
              stratforge::Line<double>& low, stratforge::Line<double>& close,
              Indicator& indicator) {
    stratforge::test::run_indicator_ohlc(open, high, low, close, indicator);
}

} // namespace

TEST_CASE("CandleGeometry exposes the frozen formulas and accessor history",
          "[sf012][geometry]") {
    auto open = stratforge::test::make_line({10.0, 13.0});
    auto high = stratforge::test::make_line({13.0, 15.0});
    auto low = stratforge::test::make_line({8.0, 11.0});
    auto close = stratforge::test::make_line({12.0, 14.0});
    stratforge::CandleGeometry geometry(open, high, low, close);
    run_ohlc(open, high, low, close, geometry);

    CHECK(geometry.minimum_period() == 2uz);
    CHECK(geometry.accessor_minimum_period("body") == 1uz);
    CHECK(geometry.accessor_minimum_period("range") == 1uz);
    CHECK(geometry.accessor_minimum_period("signed_gap") == 2uz);
    CHECK(geometry.accessor_minimum_period("gap") == 2uz);
    CHECK(geometry.body().data() == std::vector<double>{2.0, 1.0});
    CHECK(geometry.range().data() == std::vector<double>{5.0, 4.0});
    CHECK(geometry.upper_wick().data() == std::vector<double>{1.0, 1.0});
    CHECK(geometry.lower_wick().data() == std::vector<double>{2.0, 2.0});
    CHECK(std::isnan(geometry.signed_gap().data()[0]));
    CHECK(geometry.signed_gap().data()[1] == 1.0);
    CHECK(std::isnan(geometry.gap().data()[0]));
    CHECK(geometry.gap().data()[1] == 1.0);
}

TEST_CASE("CandleGeometry rejects invalid OHLC and requires an immediate valid gap predecessor",
          "[sf012][geometry][invalid]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto open = stratforge::test::make_line({10.0, 12.0, 15.0, 14.0});
    auto high = stratforge::test::make_line({13.0, 11.0, 16.0, 16.0});
    auto low = stratforge::test::make_line({8.0, 10.0, 14.0, 13.0});
    auto close = stratforge::test::make_line({12.0, 10.5, nan, 15.0});
    stratforge::CandleGeometry geometry(open, high, low, close);
    run_ohlc(open, high, low, close, geometry);

    CHECK(std::isnan(geometry.body().data()[1]));
    CHECK(std::isnan(geometry.body().data()[2]));
    CHECK(geometry.body().data()[3] == 1.0);
    CHECK(std::isnan(geometry.gap().data()[1]));
    CHECK(std::isnan(geometry.gap().data()[2]));
    CHECK(std::isnan(geometry.gap().data()[3]));
}

TEST_CASE("NormalizedCandleGeometry shares the public ATR sample across every accessor",
          "[sf012][geometry][atr]") {
    auto open = stratforge::test::make_line({9.0, 11.0, 11.0, 14.0});
    auto high = stratforge::test::make_line({10.0, 13.0, 14.0, 16.0});
    auto low = stratforge::test::make_line({8.0, 9.0, 10.0, 12.0});
    auto close = stratforge::test::make_line({9.0, 12.0, 13.0, 15.0});
    stratforge::NormalizedCandleGeometry geometry(open, high, low, close, 2uz);
    run_ohlc(open, high, low, close, geometry);

    CHECK(geometry.minimum_period() == 3uz);
    CHECK(geometry.accessor_minimum_period("gap") == 3uz);
    CHECK(geometry.atr_period() == 2uz);
    CHECK(std::isnan(geometry.body().data()[0]));
    CHECK(std::isnan(geometry.body().data()[1]));
    // TRs are 4 and 4, so Wilder ATR is 4 at bars 2 and 3.
    CHECK_THAT(geometry.body().data()[2], WithinAbs(0.5, 1e-14));
    CHECK_THAT(geometry.range().data()[2], WithinAbs(1.0, 1e-14));
    CHECK_THAT(geometry.upper_wick().data()[2], WithinAbs(0.25, 1e-14));
    CHECK_THAT(geometry.lower_wick().data()[2], WithinAbs(0.25, 1e-14));
    CHECK_THAT(geometry.signed_gap().data()[2], WithinAbs(-0.25, 1e-14));
    CHECK_THAT(geometry.gap().data()[2], WithinAbs(0.25, 1e-14));
    CHECK_THAT(geometry.signed_gap().data()[3], WithinAbs(0.25, 1e-14));
}

TEST_CASE("NormalizedCandleGeometry rejects zero period and zero ATR",
          "[sf012][geometry][boundary]") {
    auto open = stratforge::test::make_line({10.0, 10.0});
    auto high = stratforge::test::make_line({10.0, 10.0});
    auto low = stratforge::test::make_line({10.0, 10.0});
    auto close = stratforge::test::make_line({10.0, 10.0});
    CHECK_THROWS_AS(stratforge::NormalizedCandleGeometry(open, high, low, close, 0uz),
                    std::invalid_argument);

    stratforge::NormalizedCandleGeometry geometry(open, high, low, close, 1uz);
    run_ohlc(open, high, low, close, geometry);
    CHECK(std::isnan(geometry.body().data()[1]));
    CHECK(std::isnan(geometry.range().data()[1]));
    CHECK(std::isnan(geometry.gap().data()[1]));
}

TEST_CASE("Normalized geometry does not clamp a finite near-zero ATR",
          "[sf012][geometry][boundary]") {
    auto open = stratforge::test::make_line({0.0, 1e-300});
    auto high = stratforge::test::make_line({0.0, 2e-300});
    auto low = stratforge::test::make_line({0.0, 0.0});
    auto close = stratforge::test::make_line({0.0, 2e-300});
    stratforge::NormalizedCandleGeometry geometry(open, high, low, close, 1uz);
    run_ohlc(open, high, low, close, geometry);

    CHECK_THAT(geometry.body().data()[1], WithinAbs(0.5, 1e-14));
    CHECK_THAT(geometry.range().data()[1], WithinAbs(1.0, 1e-14));
    CHECK_THAT(geometry.signed_gap().data()[1], WithinAbs(0.5, 1e-14));
}

TEST_CASE("Normalized geometry masks invalid OHLC and recovers same-bar fields",
          "[sf012][geometry][invalid]") {
    auto open = stratforge::test::make_line({10.0, 14.0, 12.0});
    auto high = stratforge::test::make_line({11.0, 13.0, 14.0});
    auto low = stratforge::test::make_line({9.0, 11.0, 10.0});
    auto close = stratforge::test::make_line({10.0, 12.0, 13.0});
    stratforge::NormalizedCandleGeometry geometry(open, high, low, close, 1uz);
    run_ohlc(open, high, low, close, geometry);

    CHECK(std::isnan(geometry.body().data()[1]));
    CHECK(std::isfinite(geometry.body().data()[2]));
    CHECK(std::isfinite(geometry.range().data()[2]));
    CHECK(std::isnan(geometry.gap().data()[2]));
}

TEST_CASE("Geometry is translation invariant and normalized geometry is scale invariant",
          "[sf012][geometry][property]") {
    const auto evaluate = [](double scale, double shift) {
        auto open = stratforge::test::make_line({10.0 * scale + shift, 13.0 * scale + shift});
        auto high = stratforge::test::make_line({13.0 * scale + shift, 15.0 * scale + shift});
        auto low = stratforge::test::make_line({8.0 * scale + shift, 11.0 * scale + shift});
        auto close = stratforge::test::make_line({12.0 * scale + shift, 14.0 * scale + shift});
        stratforge::NormalizedCandleGeometry geometry(open, high, low, close, 1uz);
        run_ohlc(open, high, low, close, geometry);
        return std::vector<double>{geometry.body().data()[1], geometry.range().data()[1],
                                   geometry.upper_wick().data()[1],
                                   geometry.lower_wick().data()[1], geometry.gap().data()[1]};
    };
    const auto baseline = evaluate(1.0, 0.0);
    for (const auto& [scale, shift] : {std::pair{0.01, -7.0}, std::pair{17.0, 100.0}}) {
        const auto transformed = evaluate(scale, shift);
        for (std::size_t i = 0; i < baseline.size(); ++i) {
            CHECK_THAT(transformed[i], WithinAbs(baseline[i], 1e-12));
        }
    }
}

TEST_CASE("SF012 rolling primitive freezes strict ties and period boundaries",
          "[sf012][percentile][primitive]") {
    CHECK_THROWS_AS(stratforge::candle_structure::RollingRankExtrema(0uz),
                    std::invalid_argument);

    stratforge::candle_structure::RollingRankExtrema window(4uz);
    for (double value : {4.0, 2.0, 2.0}) window.push(value);
    CHECK_FALSE(window.ready());
    CHECK(std::isnan(window.percent_rank()));
    window.push(3.0);
    CHECK(window.ready());
    CHECK_THAT(window.percent_rank(), WithinAbs(0.5, 1e-14));
    CHECK_FALSE(window.strict_minimum());
    CHECK_FALSE(window.strict_maximum());

    stratforge::candle_structure::RollingRankExtrema ties(2uz);
    ties.push(7.0);
    ties.push(7.0);
    CHECK(ties.percent_rank() == 0.0);
    CHECK_FALSE(ties.strict_minimum());
    CHECK_FALSE(ties.strict_maximum());

    stratforge::candle_structure::RollingRankExtrema singleton(1uz);
    singleton.push(5.0);
    CHECK(singleton.strict_minimum());
    CHECK(singleton.strict_maximum());
}

TEST_CASE("CandlePercentile ranks all frozen measurements with strict-below ties",
          "[sf012][percentile]") {
    auto open = stratforge::test::make_line({10.0, 10.0, 10.0, 12.0});
    auto high = stratforge::test::make_line({13.0, 12.0, 14.0, 15.0});
    auto low = stratforge::test::make_line({9.0, 9.0, 8.0, 10.0});
    auto close = stratforge::test::make_line({12.0, 11.0, 13.0, 14.0});
    stratforge::CandlePercentile percentile(
        open, high, low, close, stratforge::CandleMeasurement::range, 3uz);
    run_ohlc(open, high, low, close, percentile);

    CHECK(percentile.minimum_period() == 3uz);
    CHECK(percentile.period() == 3uz);
    CHECK(percentile.measurement() == stratforge::CandleMeasurement::range);
    CHECK(std::isnan(percentile.line().data()[0]));
    CHECK(std::isnan(percentile.line().data()[1]));
    CHECK_THAT(percentile.line().data()[2], WithinAbs(2.0 / 3.0, 1e-14));
    CHECK_THAT(percentile.line().data()[3], WithinAbs(1.0 / 3.0, 1e-14));
}

TEST_CASE("CandlePercentile gap readiness and invalid recovery require consecutive samples",
          "[sf012][percentile][invalid]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto open = stratforge::test::make_line({10.0, 12.0, 14.0, 15.0, 17.0, 20.0, 24.0});
    auto high = stratforge::test::make_line({11.0, 13.0, 13.0, 16.0, 18.0, 21.0, 25.0});
    auto low = stratforge::test::make_line({9.0, 11.0, 12.0, 14.0, 16.0, 19.0, 23.0});
    auto close = stratforge::test::make_line({10.0, 12.0, nan, 15.0, 17.0, 20.0, 24.0});
    stratforge::CandlePercentile percentile(
        open, high, low, close, stratforge::CandleMeasurement::gap, 2uz);
    run_ohlc(open, high, low, close, percentile);

    CHECK(percentile.minimum_period() == 3uz);
    for (std::size_t i = 0; i < 5uz; ++i) CHECK(std::isnan(percentile.line().data()[i]));
    CHECK_THAT(percentile.line().data()[5], WithinAbs(0.5, 1e-14));
    CHECK_THAT(percentile.line().data()[6], WithinAbs(0.5, 1e-14));
}

TEST_CASE("CandlePercentile rejects zero period", "[sf012][percentile][boundary]") {
    auto line = stratforge::test::make_line({1.0});
    CHECK_THROWS_AS(stratforge::CandlePercentile(
                        line, line, line, line, stratforge::CandleMeasurement::body, 0uz),
                    std::invalid_argument);
}
