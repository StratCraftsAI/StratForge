#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/candlestick.hpp>

#include "test_helpers.hpp"

#include <vector>

using Catch::Approx;
using stratforge::test::make_line;

// Local alias: candlestick tests previously used `run_ohlc`; map it to the
// canonical `run_indicator_ohlc` from test_helpers.hpp.
template <typename Ind>
inline void run_ohlc(::stratforge::Line<double>& o, ::stratforge::Line<double>& h,
                     ::stratforge::Line<double>& l, ::stratforge::Line<double>& c, Ind& ind) {
    ::stratforge::test::run_indicator_ohlc(o, h, l, c, ind);
}

// ============================================================
// CDLDoji
// ============================================================
TEST_CASE("CDLDoji detects doji candle", "[candlestick][doji]") {
    // Bar 0: clear doji (open==close, but range > 0)
    // Bar 1: normal candle (body > 5% of range)
    // Bar 2: flat bar (H==L) -> 0
    auto open  = make_line({100.0, 100.0, 50.0});
    auto high  = make_line({105.0, 110.0, 50.0});
    auto low   = make_line({ 95.0,  90.0, 50.0});
    auto close = make_line({100.0, 108.0, 50.0});

    stratforge::CDLDoji doji(open, high, low, close);
    run_ohlc(open, high, low, close, doji);

    CHECK(doji.line().data()[0] == Approx(100.0));   // doji, close==open -> bullish
    CHECK(doji.line().data()[1] == Approx(0.0));     // big body
    CHECK(doji.line().data()[2] == Approx(0.0));     // flat bar
}

TEST_CASE("CDLDoji bearish doji", "[candlestick][doji]") {
    // close slightly below open but within threshold
    auto open  = make_line({100.5});
    auto high  = make_line({110.0});
    auto low   = make_line({ 90.0});
    auto close = make_line({100.0});

    stratforge::CDLDoji doji(open, high, low, close);
    run_ohlc(open, high, low, close, doji);

    // body = 0.5, range = 20, 0.5/20 = 0.025 < 0.05 -> doji, close < open -> -100
    CHECK(doji.line().data()[0] == Approx(-100.0));
}

// ============================================================
// CDLHammer
// ============================================================
TEST_CASE("CDLHammer detects hammer pattern", "[candlestick][hammer]") {
    // Hammer: body at top, long lower shadow, no upper shadow
    // body_top=101, body_bot=100, body=1, range=10
    // lower_shadow=100-91=9 >= 2*1=2 [ok], upper_shadow=101-101=0 <= 0.1 [ok]
    // body=1 < 10/3=3.33 [ok]
    auto open  = make_line({100.0, 100.0});
    auto high  = make_line({101.0, 110.0});
    auto low   = make_line({ 91.0,  90.0});
    auto close = make_line({101.0, 105.0});

    stratforge::CDLHammer hammer(open, high, low, close);
    run_ohlc(open, high, low, close, hammer);

    CHECK(hammer.line().data()[0] == Approx(100.0));  // hammer detected
    CHECK(hammer.line().data()[1] == Approx(0.0));    // not a hammer
}

TEST_CASE("CDLHammer flat bar returns 0", "[candlestick][hammer]") {
    auto open  = make_line({50.0});
    auto high  = make_line({50.0});
    auto low   = make_line({50.0});
    auto close = make_line({50.0});

    stratforge::CDLHammer hammer(open, high, low, close);
    run_ohlc(open, high, low, close, hammer);

    CHECK(hammer.line().data()[0] == Approx(0.0));
}

// ============================================================
// CDLEngulfing
// ============================================================
TEST_CASE("CDLEngulfing bullish engulfing", "[candlestick][engulfing]") {
    // Bar 0: bearish candle (open=105, close=100)
    // Bar 1: bullish candle that engulfs (open=99, close=106)
    auto open  = make_line({105.0, 99.0});
    auto high  = make_line({106.0, 107.0});
    auto low   = make_line({ 99.0,  98.0});
    auto close = make_line({100.0, 106.0});

    stratforge::CDLEngulfing eng(open, high, low, close);
    run_ohlc(open, high, low, close, eng);

    CHECK(eng.line().data()[0] == Approx(0.0));      // first bar, no prior
    CHECK(eng.line().data()[1] == Approx(100.0));    // bullish engulfing
}

TEST_CASE("CDLEngulfing bearish engulfing", "[candlestick][engulfing]") {
    // Bar 0: bullish (open=100, close=105)
    // Bar 1: bearish engulfing (open=106, close=99)
    auto open  = make_line({100.0, 106.0});
    auto high  = make_line({106.0, 107.0});
    auto low   = make_line({ 99.0,  98.0});
    auto close = make_line({105.0,  99.0});

    stratforge::CDLEngulfing eng(open, high, low, close);
    run_ohlc(open, high, low, close, eng);

    CHECK(eng.line().data()[1] == Approx(-100.0));   // bearish engulfing
}

TEST_CASE("CDLEngulfing no pattern when same color", "[candlestick][engulfing]") {
    // Both bars bullish -> no engulfing
    auto open  = make_line({100.0, 99.0});
    auto high  = make_line({106.0, 107.0});
    auto low   = make_line({ 99.0,  98.0});
    auto close = make_line({105.0, 106.0});

    stratforge::CDLEngulfing eng(open, high, low, close);
    run_ohlc(open, high, low, close, eng);

    CHECK(eng.line().data()[1] == Approx(0.0));
}

// ============================================================
// CDLMorningStar
// ============================================================
TEST_CASE("CDLMorningStar detects morning star", "[candlestick][morningstar]") {
    // Bar 0: large bearish (open=110, close=100, range=12)
    // Bar 1: small body (open=99, close=98)
    // Bar 2: large bullish closing above bar 0 midpoint (105)
    auto open  = make_line({110.0, 99.0,  98.0});
    auto high  = make_line({112.0, 100.0, 108.0});
    auto low   = make_line({100.0, 97.0,  97.0});
    auto close = make_line({100.0, 98.0,  107.0});

    stratforge::CDLMorningStar ms(open, high, low, close);
    run_ohlc(open, high, low, close, ms);

    CHECK(ms.line().data()[0] == Approx(0.0));
    CHECK(ms.line().data()[1] == Approx(0.0));
    CHECK(ms.line().data()[2] == Approx(100.0));
}

TEST_CASE("CDLMorningStar no pattern when bar 0 bullish", "[candlestick][morningstar]") {
    auto open  = make_line({100.0, 99.0, 98.0});
    auto high  = make_line({112.0, 100.0, 108.0});
    auto low   = make_line({100.0, 97.0,  97.0});
    auto close = make_line({110.0, 98.0,  107.0});

    stratforge::CDLMorningStar ms(open, high, low, close);
    run_ohlc(open, high, low, close, ms);

    CHECK(ms.line().data()[2] == Approx(0.0));
}

// ============================================================
// CDLEveningStar
// ============================================================
TEST_CASE("CDLEveningStar detects evening star", "[candlestick][eveningstar]") {
    // Bar 0: large bullish (open=100, close=110)
    // Bar 1: small body (open=111, close=112)
    // Bar 2: large bearish closing below bar 0 midpoint (105)
    auto open  = make_line({100.0, 111.0, 112.0});
    auto high  = make_line({112.0, 113.0, 113.0});
    auto low   = make_line({ 99.0, 110.0,  98.0});
    auto close = make_line({110.0, 112.0, 103.0});

    stratforge::CDLEveningStar es(open, high, low, close);
    run_ohlc(open, high, low, close, es);

    CHECK(es.line().data()[0] == Approx(0.0));
    CHECK(es.line().data()[1] == Approx(0.0));
    CHECK(es.line().data()[2] == Approx(-100.0));
}

// ============================================================
// CDLHarami
// ============================================================
TEST_CASE("CDLHarami bullish harami", "[candlestick][harami]") {
    // Bar 0: large bearish (open=110, close=100)
    // Bar 1: small bullish inside (open=102, close=108)
    auto open  = make_line({110.0, 102.0});
    auto high  = make_line({112.0, 109.0});
    auto low   = make_line({ 99.0, 101.0});
    auto close = make_line({100.0, 108.0});

    stratforge::CDLHarami har(open, high, low, close);
    run_ohlc(open, high, low, close, har);

    CHECK(har.line().data()[1] == Approx(100.0));
}

TEST_CASE("CDLHarami bearish harami", "[candlestick][harami]") {
    // Bar 0: large bullish (open=100, close=110)
    // Bar 1: small bearish inside (open=108, close=102)
    auto open  = make_line({100.0, 108.0});
    auto high  = make_line({112.0, 109.0});
    auto low   = make_line({ 99.0, 101.0});
    auto close = make_line({110.0, 102.0});

    stratforge::CDLHarami har(open, high, low, close);
    run_ohlc(open, high, low, close, har);

    CHECK(har.line().data()[1] == Approx(-100.0));
}

TEST_CASE("CDLHarami no pattern when body not contained", "[candlestick][harami]") {
    auto open  = make_line({105.0, 95.0});
    auto high  = make_line({106.0, 115.0});
    auto low   = make_line({ 99.0,  94.0});
    auto close = make_line({100.0, 112.0});

    stratforge::CDLHarami har(open, high, low, close);
    run_ohlc(open, high, low, close, har);

    CHECK(har.line().data()[1] == Approx(0.0));
}

// ============================================================
// CDLShootingStar
// ============================================================
TEST_CASE("CDLShootingStar detects shooting star", "[candlestick][shootingstar]") {
    // Body at bottom, long upper shadow
    // body_bot=100, body_top=101, body=1, range=10
    // upper_shadow=110-101=9 >= 2 [ok], lower_shadow=100-100=0 <= 0.1 [ok]
    auto open  = make_line({100.0, 100.0});
    auto high  = make_line({110.0, 101.0});
    auto low   = make_line({100.0,  99.0});
    auto close = make_line({101.0, 101.0});

    stratforge::CDLShootingStar ss(open, high, low, close);
    run_ohlc(open, high, low, close, ss);

    CHECK(ss.line().data()[0] == Approx(-100.0));  // shooting star
    CHECK(ss.line().data()[1] == Approx(0.0));     // not a shooting star
}

// ============================================================
// CDLHangingMan
// ============================================================
TEST_CASE("CDLHangingMan detects hanging man", "[candlestick][hangingman]") {
    // Same shape as hammer but bearish signal
    auto open  = make_line({100.0});
    auto high  = make_line({101.0});
    auto low   = make_line({ 91.0});
    auto close = make_line({101.0});

    stratforge::CDLHangingMan hm(open, high, low, close);
    run_ohlc(open, high, low, close, hm);

    CHECK(hm.line().data()[0] == Approx(-100.0));
}

// ============================================================
// CDLMarubozu
// ============================================================
TEST_CASE("CDLMarubozu bullish marubozu", "[candlestick][marubozu]") {
    // Almost full body: open=100, close=110, high=110.2, low=99.8
    // body=10, range=10.4, 10/10.4 = 0.9615 >= 0.95 [ok]
    auto open  = make_line({100.0, 100.0});
    auto high  = make_line({110.2, 110.0});
    auto low   = make_line({ 99.8,  90.0});
    auto close = make_line({110.0, 105.0});

    stratforge::CDLMarubozu m(open, high, low, close);
    run_ohlc(open, high, low, close, m);

    CHECK(m.line().data()[0] == Approx(100.0));   // bullish marubozu
    CHECK(m.line().data()[1] == Approx(0.0));     // too much shadow
}

TEST_CASE("CDLMarubozu bearish marubozu", "[candlestick][marubozu]") {
    auto open  = make_line({110.0});
    auto high  = make_line({110.2});
    auto low   = make_line({ 99.8});
    auto close = make_line({100.0});

    stratforge::CDLMarubozu m(open, high, low, close);
    run_ohlc(open, high, low, close, m);

    CHECK(m.line().data()[0] == Approx(-100.0));
}

TEST_CASE("CDLMarubozu flat bar returns 0", "[candlestick][marubozu]") {
    auto open  = make_line({50.0});
    auto high  = make_line({50.0});
    auto low   = make_line({50.0});
    auto close = make_line({50.0});

    stratforge::CDLMarubozu m(open, high, low, close);
    run_ohlc(open, high, low, close, m);

    CHECK(m.line().data()[0] == Approx(0.0));
}

// ============================================================
// CDLSpinningTop
// ============================================================
TEST_CASE("CDLSpinningTop detects spinning top", "[candlestick][spinningtop]") {
    // Small body centered with shadows on both sides
    // open=100, close=101, high=105, low=95 -> range=10
    // body=1 < 0.3*10=3 [ok]
    // upper_shadow=105-101=4 > 0.2*10=2 [ok]
    // lower_shadow=100-95=5 > 2 [ok]
    auto open  = make_line({100.0, 100.0});
    auto high  = make_line({105.0, 101.0});
    auto low   = make_line({ 95.0,  99.0});
    auto close = make_line({101.0, 101.0});

    stratforge::CDLSpinningTop st(open, high, low, close);
    run_ohlc(open, high, low, close, st);

    CHECK(st.line().data()[0] == Approx(100.0));  // spinning top, close > open -> +100
    CHECK(st.line().data()[1] == Approx(0.0));    // shadows too small
}

TEST_CASE("CDLSpinningTop bearish spinning top", "[candlestick][spinningtop]") {
    // close < open
    auto open  = make_line({101.0});
    auto high  = make_line({105.0});
    auto low   = make_line({ 95.0});
    auto close = make_line({100.0});

    stratforge::CDLSpinningTop st(open, high, low, close);
    run_ohlc(open, high, low, close, st);

    CHECK(st.line().data()[0] == Approx(-100.0));
}

// ============================================================
// CDLThreeWhiteSoldiers
// ============================================================
TEST_CASE("CDLThreeWhiteSoldiers detects pattern", "[candlestick][threewhitesoldiers]") {
    // Three bullish candles with higher closes, each opening within previous body
    auto open  = make_line({100.0, 102.0, 105.0});
    auto high  = make_line({104.0, 107.0, 110.0});
    auto low   = make_line({ 99.0, 101.0, 104.0});
    auto close = make_line({103.0, 106.0, 109.0});

    stratforge::CDLThreeWhiteSoldiers tws(open, high, low, close);
    run_ohlc(open, high, low, close, tws);

    CHECK(tws.line().data()[0] == Approx(0.0));
    CHECK(tws.line().data()[1] == Approx(0.0));
    CHECK(tws.line().data()[2] == Approx(100.0));
}

TEST_CASE("CDLThreeWhiteSoldiers no pattern when not ascending", "[candlestick][threewhitesoldiers]") {
    // Second close not higher than first
    auto open  = make_line({100.0, 101.0, 102.0});
    auto high  = make_line({104.0, 103.0, 106.0});
    auto low   = make_line({ 99.0, 100.0, 101.0});
    auto close = make_line({103.0, 102.0, 105.0});

    stratforge::CDLThreeWhiteSoldiers tws(open, high, low, close);
    run_ohlc(open, high, low, close, tws);

    CHECK(tws.line().data()[2] == Approx(0.0));
}

// ============================================================
// CDLThreeBlackCrows
// ============================================================
TEST_CASE("CDLThreeBlackCrows detects pattern", "[candlestick][threeblackcrows]") {
    // Three bearish candles with lower closes, each opening within previous body
    auto open  = make_line({110.0, 108.0, 105.0});
    auto high  = make_line({111.0, 109.0, 106.0});
    auto low   = make_line({106.0, 104.0, 101.0});
    auto close = make_line({107.0, 105.0, 102.0});

    stratforge::CDLThreeBlackCrows tbc(open, high, low, close);
    run_ohlc(open, high, low, close, tbc);

    CHECK(tbc.line().data()[0] == Approx(0.0));
    CHECK(tbc.line().data()[1] == Approx(0.0));
    CHECK(tbc.line().data()[2] == Approx(-100.0));
}

TEST_CASE("CDLThreeBlackCrows no pattern when bullish bar present", "[candlestick][threeblackcrows]") {
    auto open  = make_line({110.0, 108.0, 105.0});
    auto high  = make_line({111.0, 110.0, 106.0});
    auto low   = make_line({106.0, 107.0, 101.0});
    auto close = make_line({107.0, 109.0, 102.0}); // bar 1 bullish

    stratforge::CDLThreeBlackCrows tbc(open, high, low, close);
    run_ohlc(open, high, low, close, tbc);

    CHECK(tbc.line().data()[2] == Approx(0.0));
}

// ============================================================
// CDLPiercingLine
// ============================================================
TEST_CASE("CDLPiercingLine detects pattern", "[candlestick][piercingline]") {
    // Bar 0: bearish (open=110, close=100), midpoint=105
    // Bar 1: bullish, opens below prev close, closes above midpoint but below prev open
    auto open  = make_line({110.0,  98.0});
    auto high  = make_line({112.0, 108.0});
    auto low   = make_line({ 99.0,  97.0});
    auto close = make_line({100.0, 107.0});

    stratforge::CDLPiercingLine pl(open, high, low, close);
    run_ohlc(open, high, low, close, pl);

    CHECK(pl.line().data()[0] == Approx(0.0));
    CHECK(pl.line().data()[1] == Approx(100.0));
}

TEST_CASE("CDLPiercingLine no pattern when close below midpoint", "[candlestick][piercingline]") {
    auto open  = make_line({110.0,  98.0});
    auto high  = make_line({112.0, 104.0});
    auto low   = make_line({ 99.0,  97.0});
    auto close = make_line({100.0, 103.0}); // 103 < midpoint 105

    stratforge::CDLPiercingLine pl(open, high, low, close);
    run_ohlc(open, high, low, close, pl);

    CHECK(pl.line().data()[1] == Approx(0.0));
}

// ============================================================
// CDLDarkCloudCover
// ============================================================
TEST_CASE("CDLDarkCloudCover detects pattern", "[candlestick][darkcloudcover]") {
    // Bar 0: bullish (open=100, close=110), midpoint=105
    // Bar 1: bearish, opens above prev close, closes below midpoint but above prev open
    auto open  = make_line({100.0, 112.0});
    auto high  = make_line({111.0, 113.0});
    auto low   = make_line({ 99.0,  98.0});
    auto close = make_line({110.0, 103.0});

    stratforge::CDLDarkCloudCover dcc(open, high, low, close);
    run_ohlc(open, high, low, close, dcc);

    CHECK(dcc.line().data()[0] == Approx(0.0));
    CHECK(dcc.line().data()[1] == Approx(-100.0));
}

TEST_CASE("CDLDarkCloudCover no pattern when close above midpoint", "[candlestick][darkcloudcover]") {
    auto open  = make_line({100.0, 112.0});
    auto high  = make_line({111.0, 113.0});
    auto low   = make_line({ 99.0, 106.0});
    auto close = make_line({110.0, 107.0}); // 107 > midpoint 105

    stratforge::CDLDarkCloudCover dcc(open, high, low, close);
    run_ohlc(open, high, low, close, dcc);

    CHECK(dcc.line().data()[1] == Approx(0.0));
}

// ============================================================
// CDLAbandonedBaby
// ============================================================
TEST_CASE("CDLAbandonedBaby bullish pattern", "[candlestick][abandonedbaby]") {
    // Bar 0: bearish (open=110, close=100)
    // Bar 1: doji with gap down (h1 < l0=99)
    // Bar 2: bullish with gap up (l2 > h1)
    auto open  = make_line({110.0,  95.0, 98.0});
    auto high  = make_line({111.0,  96.0, 105.0});
    auto low   = make_line({ 99.0,  94.0,  97.0});
    auto close = make_line({100.0,  95.1, 104.0}); // bar 1 doji: body=0.1, range=2 -> 0.05 <= 0.1

    stratforge::CDLAbandonedBaby ab(open, high, low, close);
    run_ohlc(open, high, low, close, ab);

    CHECK(ab.line().data()[0] == Approx(0.0));
    CHECK(ab.line().data()[1] == Approx(0.0));
    CHECK(ab.line().data()[2] == Approx(100.0));
}

TEST_CASE("CDLAbandonedBaby bearish pattern", "[candlestick][abandonedbaby]") {
    // Bar 0: bullish (open=100, close=110)
    // Bar 1: doji with gap up (l1 > h0=111)
    // Bar 2: bearish with gap down (h2 < l1)
    auto open  = make_line({100.0, 115.0, 112.0});
    auto high  = make_line({111.0, 116.0, 113.0});
    auto low   = make_line({ 99.0, 114.0, 106.0});
    auto close = make_line({110.0, 115.1, 107.0}); // bar 1 doji: body=0.1, range=2

    stratforge::CDLAbandonedBaby ab(open, high, low, close);
    run_ohlc(open, high, low, close, ab);

    CHECK(ab.line().data()[2] == Approx(-100.0));
}

TEST_CASE("CDLAbandonedBaby no pattern without gap", "[candlestick][abandonedbaby]") {
    // No gap between bars
    auto open  = make_line({110.0, 100.0, 100.0});
    auto high  = make_line({111.0, 101.0, 105.0});
    auto low   = make_line({ 99.0,  99.0,  99.0}); // l1=99 not < l0=99
    auto close = make_line({100.0, 100.1, 104.0});

    stratforge::CDLAbandonedBaby ab(open, high, low, close);
    run_ohlc(open, high, low, close, ab);

    CHECK(ab.line().data()[2] == Approx(0.0));
}

// ============================================================
// CDLThreeInside
// ============================================================
TEST_CASE("CDLThreeInside bullish pattern", "[candlestick][threeinside]") {
    // Bar 0: bearish (open=110, close=100) -> body top=110, bot=100
    // Bar 1: bullish harami (open=102, close=108) inside bar 0
    // Bar 2: closes above bar 0 body top (110)
    auto open  = make_line({110.0, 102.0, 108.0});
    auto high  = make_line({112.0, 109.0, 115.0});
    auto low   = make_line({ 99.0, 101.0, 107.0});
    auto close = make_line({100.0, 108.0, 112.0});

    stratforge::CDLThreeInside ti(open, high, low, close);
    run_ohlc(open, high, low, close, ti);

    CHECK(ti.line().data()[2] == Approx(100.0));
}

TEST_CASE("CDLThreeInside bearish pattern", "[candlestick][threeinside]") {
    // Bar 0: bullish (open=100, close=110) -> body top=110, bot=100
    // Bar 1: bearish harami (open=108, close=102) inside bar 0
    // Bar 2: closes below bar 0 body bot (100)
    auto open  = make_line({100.0, 108.0, 101.0});
    auto high  = make_line({112.0, 109.0, 102.0});
    auto low   = make_line({ 99.0, 101.0,  95.0});
    auto close = make_line({110.0, 102.0,  98.0});

    stratforge::CDLThreeInside ti(open, high, low, close);
    run_ohlc(open, high, low, close, ti);

    CHECK(ti.line().data()[2] == Approx(-100.0));
}

// ============================================================
// CDLThreeOutside
// ============================================================
TEST_CASE("CDLThreeOutside bullish pattern", "[candlestick][threeoutside]") {
    // Bar 0: bearish (open=105, close=100)
    // Bar 1: bullish engulfing (open=99, close=106) engulfs bar 0
    // Bar 2: closes higher than bar 1 close
    auto open  = make_line({105.0,  99.0, 105.0});
    auto high  = make_line({106.0, 107.0, 110.0});
    auto low   = make_line({ 99.0,  98.0, 104.0});
    auto close = make_line({100.0, 106.0, 109.0});

    stratforge::CDLThreeOutside to(open, high, low, close);
    run_ohlc(open, high, low, close, to);

    CHECK(to.line().data()[2] == Approx(100.0));
}

TEST_CASE("CDLThreeOutside bearish pattern", "[candlestick][threeoutside]") {
    // Bar 0: bullish (open=100, close=105)
    // Bar 1: bearish engulfing (open=106, close=99)
    // Bar 2: closes lower than bar 1 close
    auto open  = make_line({100.0, 106.0, 100.0});
    auto high  = make_line({106.0, 107.0, 101.0});
    auto low   = make_line({ 99.0,  98.0,  95.0});
    auto close = make_line({105.0,  99.0,  96.0});

    stratforge::CDLThreeOutside to(open, high, low, close);
    run_ohlc(open, high, low, close, to);

    CHECK(to.line().data()[2] == Approx(-100.0));
}

// ============================================================
// CDLKicking
// ============================================================
TEST_CASE("CDLKicking bullish kicking", "[candlestick][kicking]") {
    // Bar 0: bearish marubozu (open=110, close=100, h=110.2, l=99.8)
    // Bar 1: bullish marubozu with gap up (open=112, close=122, h=122.2, l=111.8)
    auto open  = make_line({110.0, 112.0});
    auto high  = make_line({110.2, 122.2});
    auto low   = make_line({ 99.8, 111.8});
    auto close = make_line({100.0, 122.0});

    stratforge::CDLKicking kick(open, high, low, close);
    run_ohlc(open, high, low, close, kick);

    CHECK(kick.line().data()[1] == Approx(100.0));
}

TEST_CASE("CDLKicking bearish kicking", "[candlestick][kicking]") {
    // Bar 0: bullish marubozu (open=100, close=110, h=110.2, l=99.8)
    // Bar 1: bearish marubozu with gap down (open=98, close=88, h=98.2, l=87.8)
    auto open  = make_line({100.0, 98.0});
    auto high  = make_line({110.2, 98.2});
    auto low   = make_line({ 99.8, 87.8});
    auto close = make_line({110.0, 88.0});

    stratforge::CDLKicking kick(open, high, low, close);
    run_ohlc(open, high, low, close, kick);

    CHECK(kick.line().data()[1] == Approx(-100.0));
}

TEST_CASE("CDLKicking no pattern without gap", "[candlestick][kicking]") {
    // Marubozus but overlapping — no gap
    auto open  = make_line({110.0, 105.0});
    auto high  = make_line({110.2, 115.2});
    auto low   = make_line({ 99.8, 104.8});
    auto close = make_line({100.0, 115.0});

    stratforge::CDLKicking kick(open, high, low, close);
    run_ohlc(open, high, low, close, kick);

    CHECK(kick.line().data()[1] == Approx(0.0));
}

// ============================================================
// CDLBeltHold
// ============================================================
TEST_CASE("CDLBeltHold bullish belt hold", "[candlestick][belthold]") {
    // Opens at low, strong bullish close
    auto open  = make_line({100.0});
    auto high  = make_line({110.0});
    auto low   = make_line({100.0}); // open == low
    auto close = make_line({108.0}); // body=8, range=10, 8/10=0.8 >= 0.6

    stratforge::CDLBeltHold bh(open, high, low, close);
    run_ohlc(open, high, low, close, bh);

    CHECK(bh.line().data()[0] == Approx(100.0));
}

TEST_CASE("CDLBeltHold bearish belt hold", "[candlestick][belthold]") {
    // Opens at high, strong bearish close
    auto open  = make_line({110.0});
    auto high  = make_line({110.0}); // open == high
    auto low   = make_line({100.0});
    auto close = make_line({102.0}); // body=8, range=10, 0.8 >= 0.6

    stratforge::CDLBeltHold bh(open, high, low, close);
    run_ohlc(open, high, low, close, bh);

    CHECK(bh.line().data()[0] == Approx(-100.0));
}

TEST_CASE("CDLBeltHold no pattern when open not at extreme", "[candlestick][belthold]") {
    auto open  = make_line({105.0});
    auto high  = make_line({110.0});
    auto low   = make_line({100.0});
    auto close = make_line({109.0});

    stratforge::CDLBeltHold bh(open, high, low, close);
    run_ohlc(open, high, low, close, bh);

    CHECK(bh.line().data()[0] == Approx(0.0));
}

// ============================================================
// CDLCounterAttack
// ============================================================
TEST_CASE("CDLCounterAttack bullish counter attack", "[candlestick][counterattack]") {
    // Bar 0: bearish (open=110, close=100), range=12
    // Bar 1: bullish, closes at same level as bar 0 close (100)
    auto open  = make_line({110.0,  95.0});
    auto high  = make_line({112.0, 101.0});
    auto low   = make_line({ 98.0,  94.0});
    auto close = make_line({100.0, 100.0}); // closes match exactly

    stratforge::CDLCounterAttack ca(open, high, low, close);
    run_ohlc(open, high, low, close, ca);

    CHECK(ca.line().data()[1] == Approx(100.0));
}

TEST_CASE("CDLCounterAttack bearish counter attack", "[candlestick][counterattack]") {
    auto open  = make_line({100.0, 115.0});
    auto high  = make_line({112.0, 116.0});
    auto low   = make_line({ 98.0, 108.0});
    auto close = make_line({110.0, 110.0}); // closes match

    stratforge::CDLCounterAttack ca(open, high, low, close);
    run_ohlc(open, high, low, close, ca);

    CHECK(ca.line().data()[1] == Approx(-100.0));
}

// ============================================================
// CDLHomingPigeon
// ============================================================
TEST_CASE("CDLHomingPigeon detects pattern", "[candlestick][homingpigeon]") {
    // Two bullish candles, second body inside first
    auto open  = make_line({100.0, 102.0});
    auto high  = make_line({112.0, 109.0});
    auto low   = make_line({ 99.0, 101.0});
    auto close = make_line({110.0, 108.0}); // both bullish, o2>=o1, c2<=c1

    stratforge::CDLHomingPigeon hp(open, high, low, close);
    run_ohlc(open, high, low, close, hp);

    CHECK(hp.line().data()[1] == Approx(100.0));
}

TEST_CASE("CDLHomingPigeon no pattern when second bearish", "[candlestick][homingpigeon]") {
    auto open  = make_line({100.0, 108.0});
    auto high  = make_line({112.0, 109.0});
    auto low   = make_line({ 99.0, 101.0});
    auto close = make_line({110.0, 102.0}); // second is bearish

    stratforge::CDLHomingPigeon hp(open, high, low, close);
    run_ohlc(open, high, low, close, hp);

    CHECK(hp.line().data()[1] == Approx(0.0));
}

// ============================================================
// CDLMatchingLow
// ============================================================
TEST_CASE("CDLMatchingLow detects pattern", "[candlestick][matchinglow]") {
    // Two bearish candles with same close
    auto open  = make_line({110.0, 108.0});
    auto high  = make_line({112.0, 109.0});
    auto low   = make_line({ 99.0,  99.0});
    auto close = make_line({100.0, 100.0}); // same close

    stratforge::CDLMatchingLow ml(open, high, low, close);
    run_ohlc(open, high, low, close, ml);

    CHECK(ml.line().data()[1] == Approx(100.0));
}

TEST_CASE("CDLMatchingLow no pattern when different closes", "[candlestick][matchinglow]") {
    auto open  = make_line({110.0, 108.0});
    auto high  = make_line({112.0, 109.0});
    auto low   = make_line({ 99.0,  97.0});
    auto close = make_line({100.0,  98.0}); // different closes

    stratforge::CDLMatchingLow ml(open, high, low, close);
    run_ohlc(open, high, low, close, ml);

    CHECK(ml.line().data()[1] == Approx(0.0));
}

// ============================================================
// CDLTasukiGap
// ============================================================
TEST_CASE("CDLTasukiGap upside gap", "[candlestick][tasukigap]") {
    // Bar 0: bullish (open=100, close=105, high=106)
    // Bar 1: bullish with gap up (low=108 > high0=106, open=108, close=112)
    // Bar 2: bearish, opens within bar 1, closes partially filling gap
    auto open  = make_line({100.0, 108.0, 111.0});
    auto high  = make_line({106.0, 113.0, 112.0});
    auto low   = make_line({ 99.0, 108.0, 107.0});
    auto close = make_line({105.0, 112.0, 107.0}); // c2=107: < l1=108 and > h0=106

    stratforge::CDLTasukiGap tg(open, high, low, close);
    run_ohlc(open, high, low, close, tg);

    CHECK(tg.line().data()[2] == Approx(100.0));
}

TEST_CASE("CDLTasukiGap downside gap", "[candlestick][tasukigap]") {
    // Bar 0: bearish (open=110, close=105, low=104)
    // Bar 1: bearish with gap down (high1=102 < low0=104, open=102, close=98)
    // Bar 2: bullish, opens within bar 1, closes partially filling gap
    auto open  = make_line({110.0, 102.0, 99.0});
    auto high  = make_line({111.0, 102.0, 103.0});
    auto low   = make_line({104.0,  97.0,  98.0});
    auto close = make_line({105.0,  98.0, 103.0}); // c2=103: > h1=102 and < l0=104

    stratforge::CDLTasukiGap tg(open, high, low, close);
    run_ohlc(open, high, low, close, tg);

    CHECK(tg.line().data()[2] == Approx(-100.0));
}

// ============================================================
// CDLUpsideGap
// ============================================================
TEST_CASE("CDLUpsideGap detects pattern", "[candlestick][upsidegap]") {
    // Bar 0: bullish (open=100, close=105, high=106)
    // Bar 1: bullish with gap up (low1=108 > high0=106)
    // Bar 2: bullish, gap still open (low2=109 > high0=106)
    auto open  = make_line({100.0, 108.0, 112.0});
    auto high  = make_line({106.0, 113.0, 118.0});
    auto low   = make_line({ 99.0, 108.0, 109.0});
    auto close = make_line({105.0, 112.0, 117.0});

    stratforge::CDLUpsideGap ug(open, high, low, close);
    run_ohlc(open, high, low, close, ug);

    CHECK(ug.line().data()[2] == Approx(100.0));
}

TEST_CASE("CDLUpsideGap no pattern when gap filled", "[candlestick][upsidegap]") {
    // Third bar fills the gap
    auto open  = make_line({100.0, 108.0, 107.0});
    auto high  = make_line({106.0, 113.0, 112.0});
    auto low   = make_line({ 99.0, 108.0, 105.0}); // l2=105 < h0=106 -> gap filled
    auto close = make_line({105.0, 112.0, 111.0});

    stratforge::CDLUpsideGap ug(open, high, low, close);
    run_ohlc(open, high, low, close, ug);

    CHECK(ug.line().data()[2] == Approx(0.0));
}

// ============================================================
// minimum_period checks (all patterns)
// ============================================================
TEST_CASE("Candlestick minimum_period values", "[candlestick]") {
    stratforge::Line<double> dummy;

    stratforge::CDLDoji doji(dummy, dummy, dummy, dummy);
    CHECK(doji.minimum_period() == 1);

    stratforge::CDLHammer hammer(dummy, dummy, dummy, dummy);
    CHECK(hammer.minimum_period() == 1);

    stratforge::CDLEngulfing eng(dummy, dummy, dummy, dummy);
    CHECK(eng.minimum_period() == 2);

    stratforge::CDLMorningStar ms(dummy, dummy, dummy, dummy);
    CHECK(ms.minimum_period() == 3);

    stratforge::CDLEveningStar es(dummy, dummy, dummy, dummy);
    CHECK(es.minimum_period() == 3);

    stratforge::CDLHarami har(dummy, dummy, dummy, dummy);
    CHECK(har.minimum_period() == 2);

    stratforge::CDLShootingStar ss(dummy, dummy, dummy, dummy);
    CHECK(ss.minimum_period() == 1);

    stratforge::CDLHangingMan hm(dummy, dummy, dummy, dummy);
    CHECK(hm.minimum_period() == 1);

    stratforge::CDLMarubozu m(dummy, dummy, dummy, dummy);
    CHECK(m.minimum_period() == 1);

    stratforge::CDLSpinningTop st(dummy, dummy, dummy, dummy);
    CHECK(st.minimum_period() == 1);

    // New patterns
    stratforge::CDLThreeWhiteSoldiers tws(dummy, dummy, dummy, dummy);
    CHECK(tws.minimum_period() == 3);

    stratforge::CDLThreeBlackCrows tbc(dummy, dummy, dummy, dummy);
    CHECK(tbc.minimum_period() == 3);

    stratforge::CDLPiercingLine pl(dummy, dummy, dummy, dummy);
    CHECK(pl.minimum_period() == 2);

    stratforge::CDLDarkCloudCover dcc(dummy, dummy, dummy, dummy);
    CHECK(dcc.minimum_period() == 2);

    stratforge::CDLAbandonedBaby ab(dummy, dummy, dummy, dummy);
    CHECK(ab.minimum_period() == 3);

    stratforge::CDLThreeInside ti(dummy, dummy, dummy, dummy);
    CHECK(ti.minimum_period() == 3);

    stratforge::CDLThreeOutside to(dummy, dummy, dummy, dummy);
    CHECK(to.minimum_period() == 3);

    stratforge::CDLKicking kick(dummy, dummy, dummy, dummy);
    CHECK(kick.minimum_period() == 2);

    stratforge::CDLBeltHold bh(dummy, dummy, dummy, dummy);
    CHECK(bh.minimum_period() == 1);

    stratforge::CDLCounterAttack ca(dummy, dummy, dummy, dummy);
    CHECK(ca.minimum_period() == 2);

    stratforge::CDLHomingPigeon hp(dummy, dummy, dummy, dummy);
    CHECK(hp.minimum_period() == 2);

    stratforge::CDLMatchingLow ml(dummy, dummy, dummy, dummy);
    CHECK(ml.minimum_period() == 2);

    stratforge::CDLTasukiGap tg(dummy, dummy, dummy, dummy);
    CHECK(tg.minimum_period() == 3);

    stratforge::CDLUpsideGap ug(dummy, dummy, dummy, dummy);
    CHECK(ug.minimum_period() == 3);
}
