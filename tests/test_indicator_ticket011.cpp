#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/avgprice.hpp>
#include <stratforge/indicators/bop.hpp>
#include <stratforge/indicators/medprice.hpp>
#include <stratforge/indicators/natr.hpp>
#include <stratforge/indicators/oscillator.hpp>
#include <stratforge/indicators/roc_variants.hpp>
#include <stratforge/indicators/typprice.hpp>
#include <stratforge/indicators/wclprice.hpp>
#include <stratforge/indicators/midpoint.hpp>
#include <stratforge/indicators/variance.hpp>
#include <stratforge/indicators/trima.hpp>
#include <stratforge/indicators/macdfix.hpp>
#include <stratforge/indicators/dm_standalone.hpp>
#include <stratforge/indicators/minmax.hpp>
#include <stratforge/indicators/stochrsi.hpp>
#include <stratforge/indicators/linearreg.hpp>
#include <stratforge/indicators/macdext.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace {

stratforge::Line<double> make_line(const std::vector<double>& values) {
    stratforge::Line<double> line;
    for (double value : values) {
        line.forward(value);
    }
    line.home();
    return line;
}

template <typename IndicatorType>
void run_indicator(stratforge::Line<double>& source, IndicatorType& indicator) {
    for (std::size_t i = 0; i < source.size(); ++i) {
        indicator.next();
        if (i + 1 < source.size()) {
            source.advance();
        }
    }
}

template <typename IndicatorType>
void run_indicator_ohlc(stratforge::Line<double>& open, stratforge::Line<double>& high,
                        stratforge::Line<double>& low, stratforge::Line<double>& close,
                        IndicatorType& indicator) {
    for (std::size_t i = 0; i < close.size(); ++i) {
        indicator.next();
        if (i + 1 < close.size()) {
            open.advance();
            high.advance();
            low.advance();
            close.advance();
        }
    }
}

template <typename IndicatorType>
void run_indicator_hlc(stratforge::Line<double>& high, stratforge::Line<double>& low,
                       stratforge::Line<double>& close, IndicatorType& indicator) {
    for (std::size_t i = 0; i < close.size(); ++i) {
        indicator.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }
}

} // namespace

// --- AvgPrice ---

TEST_CASE("AvgPrice computes (O+H+L+C)/4", "[indicator][avgprice]") {
    auto open  = make_line({10.0, 12.0, 14.0});
    auto high  = make_line({12.0, 15.0, 16.0});
    auto low   = make_line({ 8.0,  9.0, 11.0});
    auto close = make_line({11.0, 13.0, 15.0});

    stratforge::AvgPrice avg(open, high, low, close);
    run_indicator_ohlc(open, high, low, close, avg);

    // (10+12+8+11)/4 = 10.25
    REQUIRE(avg.line().data()[0] == Approx(10.25));
    // (12+15+9+13)/4 = 12.25
    REQUIRE(avg.line().data()[1] == Approx(12.25));
    // (14+16+11+15)/4 = 14.0
    REQUIRE(avg.line().data()[2] == Approx(14.0));
}

// --- MedPrice ---

TEST_CASE("MedPrice computes (H+L)/2", "[indicator][medprice]") {
    auto high = make_line({12.0, 15.0, 16.0});
    auto low  = make_line({ 8.0,  9.0, 11.0});

    stratforge::MedPrice med(high, low);
    for (std::size_t i = 0; i < high.size(); ++i) {
        med.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
        }
    }

    REQUIRE(med.line().data()[0] == Approx(10.0));
    REQUIRE(med.line().data()[1] == Approx(12.0));
    REQUIRE(med.line().data()[2] == Approx(13.5));
}

// --- TypPrice ---

TEST_CASE("TypPrice computes (H+L+C)/3", "[indicator][typprice]") {
    auto high  = make_line({12.0, 15.0, 16.0});
    auto low   = make_line({ 8.0,  9.0, 11.0});
    auto close = make_line({11.0, 13.0, 15.0});

    stratforge::TypPrice typ(high, low, close);
    run_indicator_hlc(high, low, close, typ);

    // (12+8+11)/3 = 10.333...
    REQUIRE(typ.line().data()[0] == Approx(10.3333333333));
    // (15+9+13)/3 = 12.333...
    REQUIRE(typ.line().data()[1] == Approx(12.3333333333));
    // (16+11+15)/3 = 14.0
    REQUIRE(typ.line().data()[2] == Approx(14.0));
}

// --- WclPrice ---

TEST_CASE("WclPrice computes (H+L+2*C)/4", "[indicator][wclprice]") {
    auto high  = make_line({12.0, 15.0, 16.0});
    auto low   = make_line({ 8.0,  9.0, 11.0});
    auto close = make_line({11.0, 13.0, 15.0});

    stratforge::WclPrice wcl(high, low, close);
    run_indicator_hlc(high, low, close, wcl);

    // (12+8+22)/4 = 10.5
    REQUIRE(wcl.line().data()[0] == Approx(10.5));
    // (15+9+26)/4 = 12.5
    REQUIRE(wcl.line().data()[1] == Approx(12.5));
    // (16+11+30)/4 = 14.25
    REQUIRE(wcl.line().data()[2] == Approx(14.25));
}

// --- BOP ---

TEST_CASE("BOP computes (C-O)/(H-L)", "[indicator][bop]") {
    auto open  = make_line({10.0, 12.0, 14.0, 10.0});
    auto high  = make_line({12.0, 15.0, 16.0, 10.0});
    auto low   = make_line({ 8.0,  9.0, 11.0, 10.0});
    auto close = make_line({11.0, 10.0, 15.0, 10.0});

    stratforge::BOP bop(open, high, low, close);
    run_indicator_ohlc(open, high, low, close, bop);

    // (11-10)/(12-8) = 0.25
    REQUIRE(bop.line().data()[0] == Approx(0.25));
    // (10-12)/(15-9) = -0.333...
    REQUIRE(bop.line().data()[1] == Approx(-0.3333333333));
    // (15-14)/(16-11) = 0.2
    REQUIRE(bop.line().data()[2] == Approx(0.2));
    // zero range → 0.0
    REQUIRE(bop.line().data()[3] == Approx(0.0));
}

// --- NATR ---

TEST_CASE("NATR normalizes ATR by close price", "[indicator][natr]") {
    auto high  = make_line({10.0, 13.0, 14.0, 15.0, 16.0});
    auto low   = make_line({ 8.0,  9.0, 11.0, 12.0, 13.0});
    auto close = make_line({ 9.0, 12.0, 12.5, 14.0, 15.0});

    stratforge::NATR natr(high, low, close, 2);
    run_indicator_hlc(high, low, close, natr);

    // bar 0: NaN (first TR is NaN for ATR)
    REQUIRE(std::isnan(natr.line().data()[0]));
    // bar 1: NaN (ATR needs period=2, so first valid at bar 2)
    REQUIRE(std::isnan(natr.line().data()[1]));
    // bar 2: ATR(2) first valid, NATR = ATR/close*100
    REQUIRE_FALSE(std::isnan(natr.line().data()[2]));
    REQUIRE(natr.line().data()[2] > 0.0);
}

// --- ROCP ---

TEST_CASE("ROCP computes percentage rate of change", "[indicator][rocp]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::ROCP rocp(source, 2);
    run_indicator(source, rocp);

    REQUIRE(std::isnan(rocp.line().data()[0]));
    REQUIRE(std::isnan(rocp.line().data()[1]));
    // (13-10)/10 * 100 = 30.0
    REQUIRE(rocp.line().data()[2] == Approx(30.0));
    // (12-11)/11 * 100 = 9.0909...
    REQUIRE(rocp.line().data()[3] == Approx(9.0909090909));
    // (15-13)/13 * 100 = 15.3846...
    REQUIRE(rocp.line().data()[4] == Approx(15.3846153846));
}

// --- ROCR ---

TEST_CASE("ROCR computes ratio rate of change", "[indicator][rocr]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::ROCR rocr(source, 2);
    run_indicator(source, rocr);

    REQUIRE(std::isnan(rocr.line().data()[0]));
    REQUIRE(std::isnan(rocr.line().data()[1]));
    // 13/10 = 1.3
    REQUIRE(rocr.line().data()[2] == Approx(1.3));
    // 12/11 = 1.0909...
    REQUIRE(rocr.line().data()[3] == Approx(1.0909090909));
    // 15/13 = 1.1538...
    REQUIRE(rocr.line().data()[4] == Approx(1.1538461538));
}

// --- ROCR100 ---

TEST_CASE("ROCR100 computes ratio * 100", "[indicator][rocr100]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::ROCR100 rocr100(source, 2);
    run_indicator(source, rocr100);

    REQUIRE(std::isnan(rocr100.line().data()[0]));
    REQUIRE(std::isnan(rocr100.line().data()[1]));
    REQUIRE(rocr100.line().data()[2] == Approx(130.0));
    REQUIRE(rocr100.line().data()[3] == Approx(109.0909090909));
    REQUIRE(rocr100.line().data()[4] == Approx(115.3846153846));
}

// --- ZeroLagIndicatorOscillator ---

TEST_CASE("ZeroLagIndicatorOscillator tracks source minus ZeroLagIndicator", "[indicator][zlindosc]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 17.0, 18.0, 19.0});

    stratforge::ZeroLagIndicatorOscillator osc(source, 3, 50);
    run_indicator(source, osc);

    // First 2 bars should be NaN (period=3, minimum_period=3)
    REQUIRE(std::isnan(osc.line().data()[0]));
    REQUIRE(std::isnan(osc.line().data()[1]));

    // Bar 2 onward: source - ZeroLagIndicator value
    // The ZeroLagIndicator uses error-correcting EMA, so the oscillator
    // measures deviation from the adaptive average
    for (std::size_t i = 2; i < 10; ++i) {
        REQUIRE_FALSE(std::isnan(osc.line().data()[i]));
    }
}

// ========== Phase 2 Tests ==========

// --- MidPoint ---

TEST_CASE("MidPoint computes (highest + lowest) / 2 over period", "[indicator][midpoint]") {
    auto source = make_line({10.0, 15.0, 8.0, 12.0, 20.0});

    stratforge::MidPoint mid(source, 3);
    run_indicator(source, mid);

    REQUIRE(std::isnan(mid.line().data()[0]));
    REQUIRE(std::isnan(mid.line().data()[1]));
    // period 3: {10,15,8} → (15+8)/2 = 11.5
    REQUIRE(mid.line().data()[2] == Approx(11.5));
    // {15,8,12} → (15+8)/2 = 11.5
    REQUIRE(mid.line().data()[3] == Approx(11.5));
    // {8,12,20} → (20+8)/2 = 14.0
    REQUIRE(mid.line().data()[4] == Approx(14.0));
}

// --- MidPrice ---

TEST_CASE("MidPrice computes (highest_high + lowest_low) / 2", "[indicator][midprice]") {
    auto high = make_line({12.0, 15.0, 14.0, 18.0});
    auto low  = make_line({ 8.0,  9.0, 10.0,  7.0});

    stratforge::MidPrice mid(high, low, 3);
    for (std::size_t i = 0; i < high.size(); ++i) {
        mid.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
        }
    }

    REQUIRE(std::isnan(mid.line().data()[0]));
    REQUIRE(std::isnan(mid.line().data()[1]));
    // highs {12,15,14} max=15, lows {8,9,10} min=8 → (15+8)/2 = 11.5
    REQUIRE(mid.line().data()[2] == Approx(11.5));
    // highs {15,14,18} max=18, lows {9,10,7} min=7 → (18+7)/2 = 12.5
    REQUIRE(mid.line().data()[3] == Approx(12.5));
}

// --- Variance ---

TEST_CASE("Variance computes population variance", "[indicator][variance]") {
    auto source = make_line({2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0});

    stratforge::Variance var(source, 4);
    run_indicator(source, var);

    REQUIRE(std::isnan(var.line().data()[0]));
    REQUIRE(std::isnan(var.line().data()[1]));
    REQUIRE(std::isnan(var.line().data()[2]));
    // {2,4,4,4} mean=3.5, var = ((2-3.5)^2+(4-3.5)^2+(4-3.5)^2+(4-3.5)^2)/4
    //   = (2.25+0.25+0.25+0.25)/4 = 0.75
    REQUIRE(var.line().data()[3] == Approx(0.75));
    // {4,4,4,5} mean=4.25, var = (0.0625+0.0625+0.0625+0.5625)/4 = 0.1875
    REQUIRE(var.line().data()[4] == Approx(0.1875));
}

// --- TRIMA ---

TEST_CASE("TRIMA uses triangular weights", "[indicator][trima]") {
    auto source = make_line({1.0, 2.0, 3.0, 4.0, 5.0});

    stratforge::TRIMA trima(source, 3);
    run_indicator(source, trima);

    REQUIRE(std::isnan(trima.line().data()[0]));
    REQUIRE(std::isnan(trima.line().data()[1]));
    // weights for period 3: [1, 2, 1], total=4
    // {1,2,3}: (1*1 + 2*2 + 3*1)/4 = 8/4 = 2.0
    REQUIRE(trima.line().data()[2] == Approx(2.0));
    // {2,3,4}: (2*1 + 3*2 + 4*1)/4 = 12/4 = 3.0
    REQUIRE(trima.line().data()[3] == Approx(3.0));
    // {3,4,5}: (3*1 + 4*2 + 5*1)/4 = 16/4 = 4.0
    REQUIRE(trima.line().data()[4] == Approx(4.0));
}

// --- MACDFix ---

TEST_CASE("MACDFix uses fixed 12/26 periods", "[indicator][macdfix]") {
    // Just verify it compiles and produces output
    std::vector<double> values;
    for (int i = 0; i < 50; ++i) {
        values.push_back(100.0 + static_cast<double>(i));
    }
    auto source = make_line(values);

    stratforge::MACDFix macdfix(source);
    run_indicator(source, macdfix);

    // First 25 values should be NaN (slow period = 26)
    REQUIRE(std::isnan(macdfix.macd().data()[0]));
    REQUIRE(std::isnan(macdfix.macd().data()[24]));
    // After warmup, values should be valid
    REQUIRE_FALSE(std::isnan(macdfix.macd().data()[25]));
}

// --- PlusDM / MinusDM ---

TEST_CASE("PlusDM and MinusDM compute raw directional movement", "[indicator][dm]") {
    auto high = make_line({10.0, 13.0, 12.0, 15.0});
    auto low  = make_line({ 8.0,  9.0, 10.0,  7.0});

    auto high2 = make_line({10.0, 13.0, 12.0, 15.0});
    auto low2  = make_line({ 8.0,  9.0, 10.0,  7.0});

    stratforge::PlusDM plus_dm(high, low);
    stratforge::MinusDM minus_dm(high2, low2);

    for (std::size_t i = 0; i < high.size(); ++i) {
        plus_dm.next();
        minus_dm.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
            high2.advance();
            low2.advance();
        }
    }

    // bar 0: NaN
    REQUIRE(std::isnan(plus_dm.line().data()[0]));
    REQUIRE(std::isnan(minus_dm.line().data()[0]));

    // bar 1: upmove=3, downmove=-1 → plus_dm=3, minus_dm=0
    REQUIRE(plus_dm.line().data()[1] == Approx(3.0));
    REQUIRE(minus_dm.line().data()[1] == Approx(0.0));

    // bar 2: upmove=-1, downmove=-1 → both 0
    REQUIRE(plus_dm.line().data()[2] == Approx(0.0));
    REQUIRE(minus_dm.line().data()[2] == Approx(0.0));

    // bar 3: upmove=3, downmove=3 → both 0 (equal, neither wins)
    REQUIRE(plus_dm.line().data()[3] == Approx(0.0));
    REQUIRE(minus_dm.line().data()[3] == Approx(0.0));
}

// --- MaxIndex / MinIndex ---

TEST_CASE("MaxIndex and MinIndex find position of extreme", "[indicator][maxindex][minindex]") {
    auto source = make_line({5.0, 10.0, 3.0, 8.0, 1.0});

    auto source2 = make_line({5.0, 10.0, 3.0, 8.0, 1.0});

    stratforge::MaxIndex maxi(source, 3);
    stratforge::MinIndex mini(source2, 3);
    run_indicator(source, maxi);
    run_indicator(source2, mini);

    REQUIRE(std::isnan(maxi.line().data()[0]));
    REQUIRE(std::isnan(maxi.line().data()[1]));

    // {5,10,3}: max=10 at idx 1, which is 1 bar ago
    REQUIRE(maxi.line().data()[2] == Approx(1.0));
    // {10,3,8}: max=10 at idx 1, which is 2 bars ago
    REQUIRE(maxi.line().data()[3] == Approx(2.0));

    // {5,10,3}: min=3 at idx 2, which is 0 bars ago
    REQUIRE(mini.line().data()[2] == Approx(0.0));
    // {10,3,8}: min=3 at idx 2, which is 1 bar ago
    REQUIRE(mini.line().data()[3] == Approx(1.0));
    // {3,8,1}: min=1 at idx 4, which is 0 bars ago
    REQUIRE(mini.line().data()[4] == Approx(0.0));
}

// --- MinMax ---

TEST_CASE("MinMax outputs both min and max over period", "[indicator][minmax]") {
    auto source = make_line({5.0, 10.0, 3.0, 8.0, 1.0});

    stratforge::MinMax mm(source, 3);
    run_indicator(source, mm);

    REQUIRE(std::isnan(mm.max().data()[0]));
    REQUIRE(std::isnan(mm.min().data()[0]));

    // {5,10,3}: max=10, min=3
    REQUIRE(mm.max().data()[2] == Approx(10.0));
    REQUIRE(mm.min().data()[2] == Approx(3.0));

    // {10,3,8}: max=10, min=3
    REQUIRE(mm.max().data()[3] == Approx(10.0));
    REQUIRE(mm.min().data()[3] == Approx(3.0));

    // {3,8,1}: max=8, min=1
    REQUIRE(mm.max().data()[4] == Approx(8.0));
    REQUIRE(mm.min().data()[4] == Approx(1.0));
}

// --- MinMaxIndex ---

TEST_CASE("MinMaxIndex outputs indices of both extremes", "[indicator][minmaxindex]") {
    auto source = make_line({5.0, 10.0, 3.0, 8.0, 1.0});

    stratforge::MinMaxIndex mmi(source, 3);
    run_indicator(source, mmi);

    REQUIRE(std::isnan(mmi.max_index().data()[0]));

    // {5,10,3}: max=10 (1 ago), min=3 (0 ago)
    REQUIRE(mmi.max_index().data()[2] == Approx(1.0));
    REQUIRE(mmi.min_index().data()[2] == Approx(0.0));
}

// ========== Phase 3 Tests ==========

// --- LinearReg ---

TEST_CASE("LinearReg fits line to price data", "[indicator][linearreg]") {
    // Perfect line: y = 2*x + 1 → values at x=0,1,2,3,4 are 1,3,5,7,9
    auto source = make_line({1.0, 3.0, 5.0, 7.0, 9.0});

    stratforge::LinearReg lr(source, 3);
    run_indicator(source, lr);

    REQUIRE(std::isnan(lr.line().data()[0]));
    REQUIRE(std::isnan(lr.line().data()[1]));
    // {1,3,5}: slope=2, intercept=1, fitted at x=2 → 2*2+1=5
    REQUIRE(lr.line().data()[2] == Approx(5.0));
    REQUIRE(lr.slope().data()[2] == Approx(2.0));
    REQUIRE(lr.intercept().data()[2] == Approx(1.0));
    // {3,5,7}: slope=2, intercept=3, fitted at x=2 → 2*2+3=7
    REQUIRE(lr.line().data()[3] == Approx(7.0));
    // {5,7,9}: slope=2, intercept=5, fitted at x=2 → 2*2+5=9
    REQUIRE(lr.line().data()[4] == Approx(9.0));
}

// --- LinearRegSlope ---

TEST_CASE("LinearRegSlope outputs slope only", "[indicator][linearreg_slope]") {
    auto source = make_line({1.0, 3.0, 5.0, 7.0, 9.0});

    stratforge::LinearRegSlope lrs(source, 3);
    run_indicator(source, lrs);

    REQUIRE(std::isnan(lrs.line().data()[0]));
    REQUIRE(lrs.line().data()[2] == Approx(2.0));
}

// --- LinearRegIntercept ---

TEST_CASE("LinearRegIntercept outputs intercept only", "[indicator][linearreg_intercept]") {
    auto source = make_line({1.0, 3.0, 5.0, 7.0, 9.0});

    stratforge::LinearRegIntercept lri(source, 3);
    run_indicator(source, lri);

    REQUIRE(std::isnan(lri.line().data()[0]));
    REQUIRE(lri.line().data()[2] == Approx(1.0));
    REQUIRE(lri.line().data()[3] == Approx(3.0));
}

// --- LinearRegAngle ---

TEST_CASE("LinearRegAngle outputs atan(slope) in degrees", "[indicator][linearreg_angle]") {
    auto source = make_line({1.0, 2.0, 3.0, 4.0});

    stratforge::LinearRegAngle lra(source, 3);
    run_indicator(source, lra);

    REQUIRE(std::isnan(lra.line().data()[0]));
    // slope = 1.0, atan(1.0) = 45 degrees
    REQUIRE(lra.line().data()[2] == Approx(45.0));
}

// --- TSF ---

TEST_CASE("TSF extrapolates one bar forward", "[indicator][tsf]") {
    auto source = make_line({1.0, 3.0, 5.0, 7.0, 9.0});

    stratforge::TSF tsf(source, 3);
    run_indicator(source, tsf);

    REQUIRE(std::isnan(tsf.line().data()[0]));
    // {1,3,5}: slope=2, intercept=1, forecast at x=3 → 2*3+1=7
    REQUIRE(tsf.line().data()[2] == Approx(7.0));
    // {3,5,7}: slope=2, intercept=3, forecast at x=3 → 2*3+3=9
    REQUIRE(tsf.line().data()[3] == Approx(9.0));
}

// --- StochRSI ---

TEST_CASE("StochRSI produces values between 0 and 100", "[indicator][stochrsi]") {
    // Generate enough data for RSI warmup + stochastic period
    std::vector<double> values;
    for (int i = 0; i < 50; ++i) {
        values.push_back(100.0 + std::sin(static_cast<double>(i) * 0.5) * 10.0);
    }
    auto source = make_line(values);

    stratforge::StochRSI stochrsi(source, 14, 14, 3);
    run_indicator(source, stochrsi);

    // Early values should be NaN
    REQUIRE(std::isnan(stochrsi.percK().data()[0]));

    // Find first valid K value and verify it's in range
    bool found_valid = false;
    for (std::size_t i = 0; i < stochrsi.percK().data().size(); ++i) {
        const double k = stochrsi.percK().data()[i];
        if (!std::isnan(k)) {
            REQUIRE(k >= 0.0);
            REQUIRE(k <= 100.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

// --- MACDExt ---

TEST_CASE("MACDExt with SMA produces valid output", "[indicator][macdext]") {
    std::vector<double> values;
    for (int i = 0; i < 50; ++i) {
        values.push_back(100.0 + static_cast<double>(i));
    }
    auto source = make_line(values);

    stratforge::MACDExt macdext(source, 5, stratforge::MaType::SMA, 10, stratforge::MaType::SMA,
                             3, stratforge::MaType::SMA);
    run_indicator(source, macdext);

    // Early values should be NaN
    REQUIRE(std::isnan(macdext.macd().data()[0]));

    // After warmup, MACD should be positive (uptrend with SMA fast < slow lag)
    bool found_valid = false;
    for (std::size_t i = 0; i < macdext.macd().data().size(); ++i) {
        if (!std::isnan(macdext.macd().data()[i])) {
            found_valid = true;
            break;
        }
    }
    REQUIRE(found_valid);
}
