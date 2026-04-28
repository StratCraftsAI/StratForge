#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/average.hpp>
#include <stratforge/indicators/crossover.hpp>
#include <stratforge/indicators/downday.hpp>
#include <stratforge/indicators/downmove.hpp>
#include <stratforge/indicators/findindex.hpp>
#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/logicaln.hpp>
#include <stratforge/indicators/lowest.hpp>
#include <stratforge/indicators/pctrank.hpp>
#include <stratforge/indicators/periodn.hpp>
#include <stratforge/indicators/sumn.hpp>
#include <stratforge/indicators/truehigh.hpp>
#include <stratforge/indicators/truelow.hpp>
#include <stratforge/indicators/upday.hpp>
#include <stratforge/indicators/upmove.hpp>
#include <stratforge/indicators/weightedaverage.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;
using stratforge::test::make_line;
using stratforge::test::run_indicator;

namespace {

class PeriodNProbe : public stratforge::PeriodN<PeriodNProbe> {
public:
    explicit PeriodNProbe(const stratforge::Line<double>& source, std::size_t period)
        : stratforge::PeriodN<PeriodNProbe>(source, period) {}

    void next_impl() {}
};

} // namespace

TEST_CASE("Highest emits NaN during warmup and then rolling maxima", "[indicator][highest]") {
    auto source = make_line({1.0, 3.0, 2.0, 5.0, 4.0});

    stratforge::Highest highest(source, 3);
    run_indicator(source, highest);

    REQUIRE(highest.line().size() == 5);
    REQUIRE(std::isnan(highest.line().data()[0]));
    REQUIRE(std::isnan(highest.line().data()[1]));
    REQUIRE(highest.line().data()[2] == 3.0);
    REQUIRE(highest.line().data()[3] == 5.0);
    REQUIRE(highest.line().data()[4] == 5.0);
}

TEST_CASE("Lowest emits NaN during warmup and then rolling minima", "[indicator][lowest]") {
    auto source = make_line({1.0, 3.0, 2.0, 5.0, 4.0});

    stratforge::Lowest lowest(source, 3);
    run_indicator(source, lowest);

    REQUIRE(lowest.line().size() == 5);
    REQUIRE(std::isnan(lowest.line().data()[0]));
    REQUIRE(std::isnan(lowest.line().data()[1]));
    REQUIRE(lowest.line().data()[2] == 1.0);
    REQUIRE(lowest.line().data()[3] == 2.0);
    REQUIRE(lowest.line().data()[4] == 2.0);
}

TEST_CASE("PeriodN reports the configured minimum period", "[indicator][periodn]") {
    auto source = make_line({1.0, 2.0, 3.0});

    PeriodNProbe periodn(source, 0);

    REQUIRE(periodn.minimum_period() == 1);
    REQUIRE(periodn.period() == 1);
}

TEST_CASE("SumN and Average emit rolling arithmetic aggregates", "[indicator][sumn][average]") {
    auto source_sum = make_line({1.0, 3.0, 2.0, 5.0, 4.0});
    auto source_avg = make_line({1.0, 3.0, 2.0, 5.0, 4.0});

    stratforge::SumN sumn(source_sum, 3);
    stratforge::Average average(source_avg, 3);

    run_indicator(source_sum, sumn);
    run_indicator(source_avg, average);

    REQUIRE(std::isnan(sumn.line().data()[0]));
    REQUIRE(std::isnan(sumn.line().data()[1]));
    REQUIRE(sumn.line().data()[2] == 6.0);
    REQUIRE(sumn.line().data()[3] == 10.0);
    REQUIRE(sumn.line().data()[4] == 11.0);

    REQUIRE(std::isnan(average.line().data()[0]));
    REQUIRE(std::isnan(average.line().data()[1]));
    REQUIRE(average.line().data()[2] == 2.0);
    REQUIRE(average.line().data()[3] == Approx(10.0 / 3.0));
    REQUIRE(average.line().data()[4] == Approx(11.0 / 3.0));
}

TEST_CASE("WeightedAverage defaults to linear weights and supports explicit weights", "[indicator][weightedaverage]") {
    auto source_default = make_line({1.0, 2.0, 3.0, 4.0});
    auto source_custom = make_line({1.0, 2.0, 3.0, 4.0});

    stratforge::WeightedAverage weighted_default(source_default, 3);
    stratforge::WeightedAverage weighted_custom(source_custom, 3, 0.5, {0.5, 1.0, 1.5});

    run_indicator(source_default, weighted_default);
    run_indicator(source_custom, weighted_custom);

    REQUIRE(std::isnan(weighted_default.line().data()[0]));
    REQUIRE(std::isnan(weighted_default.line().data()[1]));
    REQUIRE(weighted_default.line().data()[2] == Approx(14.0));
    REQUIRE(weighted_default.line().data()[3] == Approx(20.0));

    REQUIRE(std::isnan(weighted_custom.line().data()[0]));
    REQUIRE(std::isnan(weighted_custom.line().data()[1]));
    REQUIRE(weighted_custom.line().data()[2] == Approx(3.5));
    REQUIRE(weighted_custom.line().data()[3] == Approx(5.0));
}

TEST_CASE("AnyN and AllN convert window truthiness to 1 or 0", "[indicator][anyn][alln]") {
    auto source_any = make_line({0.0, 0.0, 2.0, 0.0, 4.0});
    auto source_all = make_line({1.0, 2.0, 0.0, 3.0, 4.0});

    stratforge::AnyN anyn(source_any, 3);
    stratforge::AllN alln(source_all, 3);

    run_indicator(source_any, anyn);
    run_indicator(source_all, alln);

    REQUIRE(std::isnan(anyn.line().data()[0]));
    REQUIRE(std::isnan(anyn.line().data()[1]));
    REQUIRE(anyn.line().data()[2] == 1.0);
    REQUIRE(anyn.line().data()[3] == 1.0);
    REQUIRE(anyn.line().data()[4] == 1.0);

    REQUIRE(std::isnan(alln.line().data()[0]));
    REQUIRE(std::isnan(alln.line().data()[1]));
    REQUIRE(alln.line().data()[2] == 0.0);
    REQUIRE(alln.line().data()[3] == 0.0);
    REQUIRE(alln.line().data()[4] == 0.0);
}

TEST_CASE("Find index primitives match backtrader tie-breaking", "[indicator][findindex]") {
    auto source_first_high = make_line({1.0, 5.0, 3.0, 5.0, 4.0});
    auto source_last_high = make_line({1.0, 5.0, 3.0, 5.0, 4.0});
    auto source_first_low = make_line({4.0, 1.0, 3.0, 1.0, 5.0});
    auto source_last_low = make_line({4.0, 1.0, 3.0, 1.0, 5.0});

    stratforge::FindFirstIndexHighest first_high(source_first_high, 4);
    stratforge::FindLastIndexHighest last_high(source_last_high, 4);
    stratforge::FindFirstIndexLowest first_low(source_first_low, 4);
    stratforge::FindLastIndexLowest last_low(source_last_low, 4);

    run_indicator(source_first_high, first_high);
    run_indicator(source_last_high, last_high);
    run_indicator(source_first_low, first_low);
    run_indicator(source_last_low, last_low);

    REQUIRE(std::isnan(first_high.line().data()[2]));
    REQUIRE(first_high.line().data()[3] == 0.0);
    REQUIRE(first_high.line().data()[4] == 1.0);

    REQUIRE(std::isnan(last_high.line().data()[2]));
    REQUIRE(last_high.line().data()[3] == 2.0);
    REQUIRE(last_high.line().data()[4] == 3.0);

    REQUIRE(std::isnan(first_low.line().data()[2]));
    REQUIRE(first_low.line().data()[3] == 0.0);
    REQUIRE(first_low.line().data()[4] == 1.0);

    REQUIRE(std::isnan(last_low.line().data()[2]));
    REQUIRE(last_low.line().data()[3] == 2.0);
    REQUIRE(last_low.line().data()[4] == 3.0);
}

TEST_CASE("NonZeroDifference carries forward the last non-zero spread", "[indicator][nzd]") {
    auto line1 = make_line({10.0, 12.0, 12.0, 14.0, 13.0});
    auto line2 = make_line({11.0, 12.0, 12.0, 13.0, 13.0});

    stratforge::NonZeroDifference nzd(line1, line2);
    for (std::size_t i = 0; i < line1.size(); ++i) {
        nzd.next();
        if (i + 1 < line1.size()) {
            line1.advance();
            line2.advance();
        }
    }

    REQUIRE(nzd.line().data()[0] == -1.0);
    REQUIRE(nzd.line().data()[1] == -1.0);
    REQUIRE(nzd.line().data()[2] == -1.0);
    REQUIRE(nzd.line().data()[3] == 1.0);
    REQUIRE(nzd.line().data()[4] == 1.0);
}

TEST_CASE("CrossUp and CrossDown use last non-zero difference semantics", "[indicator][crossup][crossdown]") {
    auto up_left = make_line({10.0, 10.0, 11.0, 12.0, 11.0});
    auto up_right = make_line({11.0, 10.0, 10.0, 11.0, 12.0});
    auto down_left = make_line({12.0, 12.0, 11.0, 10.0, 11.0});
    auto down_right = make_line({11.0, 12.0, 12.0, 11.0, 10.0});

    stratforge::CrossUp crossup(up_left, up_right);
    stratforge::CrossDown crossdown(down_left, down_right);

    for (std::size_t i = 0; i < up_left.size(); ++i) {
        crossup.next();
        crossdown.next();
        if (i + 1 < up_left.size()) {
            up_left.advance();
            up_right.advance();
            down_left.advance();
            down_right.advance();
        }
    }

    REQUIRE(std::isnan(crossup.line().data()[0]));
    REQUIRE(crossup.line().data()[1] == 0.0);
    REQUIRE(crossup.line().data()[2] == 1.0);
    REQUIRE(crossup.line().data()[3] == 0.0);
    REQUIRE(crossup.line().data()[4] == 0.0);

    REQUIRE(std::isnan(crossdown.line().data()[0]));
    REQUIRE(crossdown.line().data()[1] == 0.0);
    REQUIRE(crossdown.line().data()[2] == 1.0);
    REQUIRE(crossdown.line().data()[3] == 0.0);
    REQUIRE(crossdown.line().data()[4] == 0.0);
}

TEST_CASE("UpDay and DownDay families match Wilder-style close comparisons", "[indicator][upday][downday]") {
    auto upday_source = make_line({10.0, 12.0, 11.0, 11.0, 15.0});
    auto upday_bool_source = make_line({10.0, 12.0, 11.0, 11.0, 15.0});
    auto downday_source = make_line({10.0, 12.0, 11.0, 11.0, 15.0});
    auto downday_bool_source = make_line({10.0, 12.0, 11.0, 11.0, 15.0});

    stratforge::UpDay upday(upday_source, 1);
    stratforge::UpDayBool upday_bool(upday_bool_source, 1);
    stratforge::DownDay downday(downday_source, 1);
    stratforge::DownDayBool downday_bool(downday_bool_source, 1);

    run_indicator(upday_source, upday);
    run_indicator(upday_bool_source, upday_bool);
    run_indicator(downday_source, downday);
    run_indicator(downday_bool_source, downday_bool);

    REQUIRE(std::isnan(upday.line().data()[0]));
    REQUIRE(upday.line().data()[1] == 2.0);
    REQUIRE(upday.line().data()[2] == 0.0);
    REQUIRE(upday.line().data()[3] == 0.0);
    REQUIRE(upday.line().data()[4] == 4.0);

    REQUIRE(std::isnan(upday_bool.line().data()[0]));
    REQUIRE(upday_bool.line().data()[1] == 1.0);
    REQUIRE(upday_bool.line().data()[2] == 0.0);
    REQUIRE(upday_bool.line().data()[3] == 0.0);
    REQUIRE(upday_bool.line().data()[4] == 1.0);

    REQUIRE(std::isnan(downday.line().data()[0]));
    REQUIRE(downday.line().data()[1] == 0.0);
    REQUIRE(downday.line().data()[2] == 1.0);
    REQUIRE(downday.line().data()[3] == 0.0);
    REQUIRE(downday.line().data()[4] == 0.0);

    REQUIRE(std::isnan(downday_bool.line().data()[0]));
    REQUIRE(downday_bool.line().data()[1] == 0.0);
    REQUIRE(downday_bool.line().data()[2] == 1.0);
    REQUIRE(downday_bool.line().data()[3] == 0.0);
    REQUIRE(downday_bool.line().data()[4] == 0.0);
}

TEST_CASE("UpMove and DownMove expose signed one-bar differences", "[indicator][upmove][downmove]") {
    auto upmove_source = make_line({10.0, 12.0, 11.0, 15.0});
    auto downmove_source = make_line({10.0, 12.0, 11.0, 15.0});

    stratforge::UpMove upmove(upmove_source);
    stratforge::DownMove downmove(downmove_source);

    run_indicator(upmove_source, upmove);
    run_indicator(downmove_source, downmove);

    REQUIRE(std::isnan(upmove.line().data()[0]));
    REQUIRE(upmove.line().data()[1] == 2.0);
    REQUIRE(upmove.line().data()[2] == -1.0);
    REQUIRE(upmove.line().data()[3] == 4.0);

    REQUIRE(std::isnan(downmove.line().data()[0]));
    REQUIRE(downmove.line().data()[1] == -2.0);
    REQUIRE(downmove.line().data()[2] == 1.0);
    REQUIRE(downmove.line().data()[3] == -4.0);
}

TEST_CASE("TrueHigh and TrueLow use previous close gaps", "[indicator][truehigh][truelow]") {
    auto high = make_line({10.0, 11.0, 14.0, 13.0});
    auto low = make_line({8.0, 9.0, 10.0, 11.0});
    auto close_high = make_line({9.0, 12.0, 11.0, 12.0});
    auto close_low = make_line({9.0, 12.0, 11.0, 12.0});

    stratforge::TrueHigh true_high(high, close_high);
    stratforge::TrueLow true_low(low, close_low);

    for (std::size_t i = 0; i < high.size(); ++i) {
        true_high.next();
        true_low.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
            close_high.advance();
            close_low.advance();
        }
    }

    REQUIRE(std::isnan(true_high.line().data()[0]));
    REQUIRE(true_high.line().data()[1] == 11.0);
    REQUIRE(true_high.line().data()[2] == 14.0);
    REQUIRE(true_high.line().data()[3] == 13.0);

    REQUIRE(std::isnan(true_low.line().data()[0]));
    REQUIRE(true_low.line().data()[1] == 9.0);
    REQUIRE(true_low.line().data()[2] == 10.0);
    REQUIRE(true_low.line().data()[3] == 11.0);
}

TEST_CASE("PercentRank counts values strictly below the current bar", "[indicator][pctrank]") {
    auto source = make_line({10.0, 12.0, 11.0, 12.0, 15.0, 12.0});

    stratforge::PercentRank pctrank(source, 4);
    run_indicator(source, pctrank);

    REQUIRE(std::isnan(pctrank.line().data()[0]));
    REQUIRE(std::isnan(pctrank.line().data()[2]));
    REQUIRE(pctrank.line().data()[3] == Approx(0.5));
    REQUIRE(pctrank.line().data()[4] == Approx(0.75));
    REQUIRE(pctrank.line().data()[5] == Approx(0.25));
}
