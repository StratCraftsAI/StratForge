#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/oscillator_extra.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/indicators/trend.hpp>
#include <stratforge/indicators/volatility.hpp>
#include <stratforge/indicators/volume.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;
using stratforge::test::make_line;
using stratforge::test::run_indicator;

TEST_CASE("CMF and MFI emit bounded money-flow readings after warmup", "[indicator][phase8][moneyflow]") {
    auto high1 = make_line({10, 11, 12, 13, 12});
    auto low1 = make_line({8, 9, 10, 11, 10});
    auto close1 = make_line({9.5, 10.5, 11.5, 11.2, 10.3});
    auto volume1 = make_line({100, 120, 140, 130, 160});

    auto high2 = make_line({10, 11, 12, 13, 12});
    auto low2 = make_line({8, 9, 10, 11, 10});
    auto close2 = make_line({9.5, 10.5, 11.5, 11.2, 10.3});
    auto volume2 = make_line({100, 120, 140, 130, 160});

    stratforge::CMF cmf(high1, low1, close1, volume1, 3);
    stratforge::MFI mfi(high2, low2, close2, volume2, 3);

    for (std::size_t i = 0; i < close1.size(); ++i) {
        cmf.next();
        mfi.next();
        if (i + 1 < close1.size()) {
            high1.advance();
            low1.advance();
            close1.advance();
            volume1.advance();
            high2.advance();
            low2.advance();
            close2.advance();
            volume2.advance();
        }
    }

    REQUIRE(std::isnan(cmf.line().data()[1]));
    REQUIRE(cmf.line().data()[2] == Approx(0.5));
    REQUIRE(cmf.line().data()[4] < 0.0);
    REQUIRE(std::fabs(cmf.line().data()[4]) <= 1.0);

    REQUIRE(std::isnan(mfi.line().data()[2]));
    REQUIRE(mfi.line().data()[3] > 0.0);
    REQUIRE(mfi.line().data()[3] <= 100.0);
    REQUIRE(mfi.line().data()[4] < mfi.line().data()[3]);
}

TEST_CASE("EMV smooths midpoint movement by volume-adjusted range", "[indicator][phase8][emv]") {
    auto high = make_line({10, 12, 13, 15, 16});
    auto low = make_line({8, 10, 11, 13, 14});
    auto volume = make_line({100000, 120000, 90000, 80000, 70000});

    stratforge::EMV emv(high, low, volume, 2);
    for (std::size_t i = 0; i < high.size(); ++i) {
        emv.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
            volume.advance();
        }
    }

    REQUIRE(std::isnan(emv.line().data()[1]));
    REQUIRE(emv.line().data()[2] > 0.0);
    REQUIRE(emv.line().data()[4] > emv.line().data()[2]);
}

TEST_CASE("Keltner and ChaikinVolatility derive ATR and range-expansion channels", "[indicator][phase8][volatility2]") {
    auto high1 = make_line({10, 12, 11, 13, 14, 15});
    auto low1 = make_line({8, 9, 8, 10, 11, 12});
    auto close1 = make_line({9, 11, 10, 12, 13, 14});

    auto high2 = make_line({10, 12, 11, 13, 14, 15});
    auto low2 = make_line({8, 9, 8, 10, 11, 12});

    stratforge::Keltner keltner(high1, low1, close1, 3, 1.5);
    stratforge::ChaikinVolatility chv(high2, low2, 2, 2);

    for (std::size_t i = 0; i < close1.size(); ++i) {
        keltner.next();
        chv.next();
        if (i + 1 < close1.size()) {
            high1.advance();
            low1.advance();
            close1.advance();
            high2.advance();
            low2.advance();
        }
    }

    REQUIRE(std::isnan(keltner.mid().data()[2]));
    REQUIRE(std::isfinite(keltner.mid().data()[3]));
    REQUIRE(keltner.top().data()[3] > keltner.mid().data()[3]);
    REQUIRE(keltner.bottom().data()[3] < keltner.mid().data()[3]);
    REQUIRE(keltner.mid().data()[5] > keltner.mid().data()[3]);

    REQUIRE(std::isnan(chv.line().data()[2]));
    REQUIRE(std::isfinite(chv.line().data()[3]));
    REQUIRE(chv.line().data()[5] < chv.line().data()[3]);
}

TEST_CASE("SuperTrend and ChandelierExit expose trend-following stop bands", "[indicator][phase8][trend]") {
    auto high1 = make_line({10, 11, 12, 13, 14, 13});
    auto low1 = make_line({8, 9, 10, 11, 12, 11});
    auto close1 = make_line({9, 10, 11, 12, 13, 11.5});

    auto high2 = make_line({10, 11, 12, 13, 14, 13});
    auto low2 = make_line({8, 9, 10, 11, 12, 11});
    auto close2 = make_line({9, 10, 11, 12, 13, 11.5});

    stratforge::SuperTrend st(high1, low1, close1, 3, 2.0);
    stratforge::ChandelierExit chandelier(high2, low2, close2, 3, 2.0);

    for (std::size_t i = 0; i < close1.size(); ++i) {
        st.next();
        chandelier.next();
        if (i + 1 < close1.size()) {
            high1.advance();
            low1.advance();
            close1.advance();
            high2.advance();
            low2.advance();
            close2.advance();
        }
    }

    REQUIRE(std::isnan(st.line().data()[2]));
    REQUIRE(std::isfinite(st.line().data()[3]));
    REQUIRE(st.upper_band().data()[5] >= st.lower_band().data()[5]);
    REQUIRE(std::isfinite(st.line().data()[5]));

    REQUIRE(std::isnan(chandelier.long_exit().data()[2]));
    REQUIRE(std::isfinite(chandelier.long_exit().data()[3]));
    REQUIRE(std::isfinite(chandelier.short_exit().data()[3]));
    REQUIRE(chandelier.long_exit().data()[4] > chandelier.long_exit().data()[3]);
}

TEST_CASE("CMO, T3, Coppock, and KDJ produce finite derived oscillators after warmup", "[indicator][phase8][osc2]") {
    auto source1 = make_line({10, 11, 13, 12, 15, 14, 16, 18, 17, 19, 20});
    auto source2 = make_line({10, 11, 13, 12, 15, 14, 16, 18, 17, 19, 20, 21, 20, 22, 24, 23, 25, 26, 27, 28});
    auto source3 = make_line({100, 101, 103, 104, 108, 110, 115, 117, 120, 123, 125, 127, 130, 133, 136, 140});
    auto high = make_line({10, 11, 12, 13, 14, 15, 14, 16, 17});
    auto low = make_line({8, 9, 10, 11, 12, 13, 12, 14, 15});
    auto close = make_line({9, 10, 11, 12, 13, 14, 13, 15, 16});

    stratforge::CMO cmo(source1, 3);
    stratforge::T3 t3(source2, 3, 0.7);
    stratforge::Coppock coppock(source3, 3, 4, 3);
    stratforge::KDJ kdj(high, low, close, 3, 2, 2);

    run_indicator(source1, cmo);
    run_indicator(source2, t3);
    run_indicator(source3, coppock);
    for (std::size_t i = 0; i < close.size(); ++i) {
        kdj.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(cmo.line().data()[2]));
    REQUIRE(cmo.line().data()[3] == Approx(50.0));
    REQUIRE(cmo.line().data()[10] == Approx(50.0));

    REQUIRE(std::isnan(t3.line().data()[5]));
    REQUIRE(std::isfinite(t3.line().data().back()));

    REQUIRE(std::isnan(coppock.line().data()[5]));
    REQUIRE(std::isfinite(coppock.line().data().back()));

    REQUIRE(std::isnan(kdj.j().data()[2]));
    REQUIRE(std::isfinite(kdj.k().data().back()));
    REQUIRE(std::isfinite(kdj.d().data().back()));
    REQUIRE(kdj.j().data().back() == Approx((3.0 * kdj.k().data().back()) - (2.0 * kdj.d().data().back())));
}
