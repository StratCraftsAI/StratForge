#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/mavp.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace {

stratforge::Line<double> make_line(const std::vector<double>& values) {
    stratforge::Line<double> line;
    for (double v : values) line.forward(v);
    line.home();
    return line;
}

template <typename Ind>
std::vector<double> run_indicator(stratforge::Line<double>& src,
                                  stratforge::Line<double>& per, Ind& ind) {
    std::vector<double> results;
    for (std::size_t i = 0; i < src.size(); ++i) {
        ind.next();
        results.push_back(ind.line().data()[i]);
        if (i + 1 < src.size()) {
            src.advance();
            per.advance();
        }
    }
    return results;
}

} // namespace

// ============================================================
// Basic variable-period SMA
// ============================================================
TEST_CASE("MAVP basic variable period SMA", "[mavp]") {
    // Source: 10, 20, 30, 40, 50
    // Period: 2,  2,  3,  3,  2
    auto source = make_line({10.0, 20.0, 30.0, 40.0, 50.0});
    auto period = make_line({ 2.0,  2.0,  3.0,  3.0,  2.0});

    stratforge::MAVP mavp(source, period);
    auto results = run_indicator(source, period, mavp);

    // Bar 0: period=2, idx=0, need 2 bars -> NaN
    CHECK(std::isnan(results[0]));
    // Bar 1: period=2, idx=1, SMA(10,20)=15
    CHECK(results[1] == Approx(15.0));
    // Bar 2: period=3, idx=2, SMA(10,20,30)=20
    CHECK(results[2] == Approx(20.0));
    // Bar 3: period=3, idx=3, SMA(20,30,40)=30
    CHECK(results[3] == Approx(30.0));
    // Bar 4: period=2, idx=4, SMA(40,50)=45
    CHECK(results[4] == Approx(45.0));
}

// ============================================================
// Period clamping
// ============================================================
TEST_CASE("MAVP clamps period to [2, max_period]", "[mavp]") {
    auto source = make_line({1.0, 2.0, 3.0, 4.0, 5.0});
    // Period values: 1 (below min -> clamped to 2), 100 (above max -> clamped to 5)
    auto period = make_line({1.0, 1.0, 100.0, 100.0, 100.0});

    stratforge::MAVP mavp(source, period, 5);
    auto results = run_indicator(source, period, mavp);

    // Bar 0: period clamped to 2, idx=0 -> NaN
    CHECK(std::isnan(results[0]));
    // Bar 1: period clamped to 2, SMA(1,2)=1.5
    CHECK(results[1] == Approx(1.5));
    // Bar 2: period clamped to 5, idx=2, need 5 bars -> NaN
    CHECK(std::isnan(results[2]));
    // Bar 3: period clamped to 5, idx=3 -> NaN
    CHECK(std::isnan(results[3]));
    // Bar 4: period clamped to 5, SMA(1,2,3,4,5)=3
    CHECK(results[4] == Approx(3.0));
}

// ============================================================
// Constant period matches SMA
// ============================================================
TEST_CASE("MAVP with constant period matches SMA", "[mavp]") {
    auto source = make_line({2.0, 4.0, 6.0, 8.0, 10.0});
    auto period = make_line({3.0, 3.0, 3.0, 3.0,  3.0});

    stratforge::MAVP mavp(source, period);
    auto results = run_indicator(source, period, mavp);

    CHECK(std::isnan(results[0]));
    CHECK(std::isnan(results[1]));
    CHECK(results[2] == Approx(4.0));   // SMA(2,4,6)
    CHECK(results[3] == Approx(6.0));   // SMA(4,6,8)
    CHECK(results[4] == Approx(8.0));   // SMA(6,8,10)
}

// ============================================================
// minimum_period
// ============================================================
TEST_CASE("MAVP minimum_period is 2", "[mavp]") {
    stratforge::Line<double> dummy;
    stratforge::MAVP mavp(dummy, dummy);
    CHECK(mavp.minimum_period() == 2);
}

// ============================================================
// max_period accessor
// ============================================================
TEST_CASE("MAVP max_period accessor", "[mavp]") {
    stratforge::Line<double> dummy;
    stratforge::MAVP mavp1(dummy, dummy);
    CHECK(mavp1.max_period() == 50); // default

    stratforge::MAVP mavp2(dummy, dummy, 100);
    CHECK(mavp2.max_period() == 100);

    // max_period < 2 gets clamped to 2
    stratforge::MAVP mavp3(dummy, dummy, 1);
    CHECK(mavp3.max_period() == 2);
}
