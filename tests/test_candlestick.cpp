#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/candlestick.hpp>

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
void run_ohlc(stratforge::Line<double>& o, stratforge::Line<double>& h,
              stratforge::Line<double>& l, stratforge::Line<double>& c, Ind& ind) {
    for (std::size_t i = 0; i < c.size(); ++i) {
        ind.next();
        if (i + 1 < c.size()) {
            o.advance(); h.advance(); l.advance(); c.advance();
        }
    }
}

} // namespace

// ============================================================
// CDLDoji
// ============================================================
TEST_CASE("CDLDoji detects doji candle", "[candlestick][doji]") {
    // Bar 0: clear doji (open==close, but range > 0)
    // Bar 1: normal candle (body > 5% of range)
    // Bar 2: flat bar (H==L) → 0
    auto open  = make_line({100.0, 100.0, 50.0});
    auto high  = make_line({105.0, 110.0, 50.0});
    auto low   = make_line({ 95.0,  90.0, 50.0});
    auto close = make_line({100.0, 108.0, 50.0});

    stratforge::CDLDoji doji(open, high, low, close);
    run_ohlc(open, high, low, close, doji);

    CHECK(doji.line().data()[0] == Approx(100.0));   // doji, close==open → bullish
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

    // body = 0.5, range = 20, 0.5/20 = 0.025 < 0.05 → doji, close < open → -100
    CHECK(doji.line().data()[0] == Approx(-100.0));
}

// ============================================================
// CDLHammer
// ============================================================
TEST_CASE("CDLHammer detects hammer pattern", "[candlestick][hammer]") {
    // Hammer: body at top, long lower shadow, no upper shadow
    // body_top=101, body_bot=100, body=1, range=10
    // lower_shadow=100-91=9 >= 2*1=2 ✓, upper_shadow=101-101=0 <= 0.1 ✓
    // body=1 < 10/3=3.33 ✓
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
    // Both bars bullish → no engulfing
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
    // upper_shadow=110-101=9 >= 2 ✓, lower_shadow=100-100=0 <= 0.1 ✓
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
    // body=10, range=10.4, 10/10.4 = 0.9615 >= 0.95 ✓
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
    // open=100, close=101, high=105, low=95 → range=10
    // body=1 < 0.3*10=3 ✓
    // upper_shadow=105-101=4 > 0.2*10=2 ✓
    // lower_shadow=100-95=5 > 2 ✓
    auto open  = make_line({100.0, 100.0});
    auto high  = make_line({105.0, 101.0});
    auto low   = make_line({ 95.0,  99.0});
    auto close = make_line({101.0, 101.0});

    stratforge::CDLSpinningTop st(open, high, low, close);
    run_ohlc(open, high, low, close, st);

    CHECK(st.line().data()[0] == Approx(100.0));  // spinning top, close > open → +100
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
// minimum_period checks
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
}
