#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/indicators/accum.hpp>
#include <stratforge/indicators/awesomeoscillator.hpp>
#include <stratforge/indicators/bollingerpct.hpp>
#include <stratforge/core/line.hpp>
#include <stratforge/indicators/envelope.hpp>
#include <stratforge/indicators/dpo.hpp>
#include <stratforge/indicators/kama.hpp>
#include <stratforge/indicators/momentum.hpp>
#include <stratforge/indicators/obv.hpp>
#include <stratforge/indicators/oscillator.hpp>
#include <stratforge/indicators/dema.hpp>
#include <stratforge/indicators/dma.hpp>
#include <stratforge/indicators/dv2.hpp>
#include <stratforge/indicators/exponentialsmoothing.hpp>
#include <stratforge/indicators/percentchange.hpp>
#include <stratforge/indicators/prettygoodoscillator.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/aroon.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/indicators/tema.hpp>
#include <stratforge/indicators/tsi.hpp>
#include <stratforge/indicators/trix.hpp>
#include <stratforge/indicators/ultimateoscillator.hpp>
#include <stratforge/indicators/vortex.hpp>
#include <stratforge/indicators/williams.hpp>
#include <stratforge/indicators/zerolag.hpp>
#include <stratforge/indicators/zlema.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;
using stratforge::test::make_line;
using stratforge::test::run_indicator;

TEST_CASE("Momentum emits NaN during warmup then trailing differences", "[indicator][momentum]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::Momentum momentum(source, 2);
    run_indicator(source, momentum);

    REQUIRE(std::isnan(momentum.line().data()[0]));
    REQUIRE(std::isnan(momentum.line().data()[1]));
    REQUIRE(momentum.line().data()[2] == 3.0);
    REQUIRE(momentum.line().data()[3] == 1.0);
    REQUIRE(momentum.line().data()[4] == 2.0);
}

TEST_CASE("ROC100 scales ROC by 100", "[indicator][roc100]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::ROC100 roc100(source, 2);
    run_indicator(source, roc100);

    REQUIRE(std::isnan(roc100.line().data()[0]));
    REQUIRE(std::isnan(roc100.line().data()[1]));
    REQUIRE(roc100.line().data()[2] == Approx(30.0));
    REQUIRE(roc100.line().data()[3] == Approx(9.0909090909));
    REQUIRE(roc100.line().data()[4] == Approx(15.3846153846));
}

TEST_CASE("MomentumOscillator uses 100 * current / previous-N", "[indicator][momentumosc]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::MomentumOscillator osc(source, 2);
    run_indicator(source, osc);

    REQUIRE(std::isnan(osc.line().data()[0]));
    REQUIRE(std::isnan(osc.line().data()[1]));
    REQUIRE(osc.line().data()[2] == Approx(130.0));
    REQUIRE(osc.line().data()[3] == Approx(109.0909090909));
    REQUIRE(osc.line().data()[4] == Approx(115.3846153846));
}

TEST_CASE("OBV accumulates volume by close direction", "[indicator][obv]") {
    auto close = make_line({10.0, 12.0, 11.0, 11.0, 15.0});
    auto volume = make_line({100.0, 200.0, 50.0, 25.0, 80.0});

    stratforge::OBV obv(close, volume);
    for (std::size_t i = 0; i < close.size(); ++i) {
        obv.next();
        if (i + 1 < close.size()) {
            close.advance();
            volume.advance();
        }
    }

    REQUIRE(obv.line().data()[0] == 100.0);
    REQUIRE(obv.line().data()[1] == 300.0);
    REQUIRE(obv.line().data()[2] == 250.0);
    REQUIRE(obv.line().data()[3] == 250.0);
    REQUIRE(obv.line().data()[4] == 330.0);
}

TEST_CASE("Envelope wraps source values by percentage", "[indicator][envelope]") {
    auto source = make_line({10.0, 12.0, 14.0});

    stratforge::Envelope envelope(source, 10.0);
    run_indicator(source, envelope);

    REQUIRE(envelope.src().data()[0] == Approx(10.0));
    REQUIRE(envelope.top().data()[1] == Approx(13.2));
    REQUIRE(envelope.bot().data()[2] == Approx(12.6));
}

TEST_CASE("SMAEnvelope mirrors SMA warmup and adds bands", "[indicator][smaenvelope]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::SMAEnvelope envelope(source, 3, 10.0);
    run_indicator(source, envelope);

    REQUIRE(std::isnan(envelope.line().data()[0]));
    REQUIRE(std::isnan(envelope.top().data()[1]));
    REQUIRE(envelope.line().data()[2] == Approx(11.3333333333));
    REQUIRE(envelope.top().data()[2] == Approx(12.4666666667));
    REQUIRE(envelope.bot().data()[4] == Approx(12.0));
}

TEST_CASE("Oscillator subtracts comparison line from source", "[indicator][oscillator]") {
    auto source = make_line({10.0, 12.0, 11.0});
    auto baseline = make_line({9.0, 12.5, 10.5});

    stratforge::Oscillator osc(source, baseline);
    for (std::size_t i = 0; i < source.size(); ++i) {
        osc.next();
        if (i + 1 < source.size()) {
            source.advance();
            baseline.advance();
        }
    }

    REQUIRE(osc.line().data()[0] == Approx(1.0));
    REQUIRE(osc.line().data()[1] == Approx(-0.5));
    REQUIRE(osc.line().data()[2] == Approx(0.5));
}

TEST_CASE("SMAOscillator tracks source minus SMA", "[indicator][smaoscillator]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0});

    stratforge::SMAOscillator osc(source, 3);
    run_indicator(source, osc);

    REQUIRE(std::isnan(osc.line().data()[0]));
    REQUIRE(std::isnan(osc.line().data()[1]));
    REQUIRE(osc.line().data()[2] == Approx(1.6666666667));
    REQUIRE(osc.line().data()[3] == Approx(0.0));
    REQUIRE(osc.line().data()[4] == Approx(1.6666666667));
}

TEST_CASE("PriceOscillator emits EMA spread after slow warmup", "[indicator][po]") {
    auto source = make_line({10.0, 11.0, 12.0, 13.0, 14.0, 15.0});

    stratforge::PriceOscillator po(source, 2, 4);
    run_indicator(source, po);

    REQUIRE(std::isnan(po.line().data()[0]));
    REQUIRE(std::isnan(po.line().data()[2]));
    REQUIRE(po.line().data()[3] == Approx(1.0));
    REQUIRE(po.line().data()[4] == Approx(1.0));
    REQUIRE(po.line().data()[5] == Approx(1.0));
}

TEST_CASE("PPO computes percentage spread with signal and histogram", "[indicator][ppo]") {
    auto source = make_line({10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0});

    stratforge::PercentagePriceOscillator ppo(source, 2, 4, 2);
    run_indicator(source, ppo);

    REQUIRE(std::isnan(ppo.ppo().data()[2]));
    REQUIRE(ppo.ppo().data()[3] == Approx(8.6956521739));
    REQUIRE(ppo.ppo().data()[4] == Approx(8.0));
    REQUIRE(std::isnan(ppo.signal().data()[3]));
    REQUIRE(ppo.signal().data()[4] == Approx(8.3478260870));
    REQUIRE(ppo.histo().data()[4] == Approx(-0.3478260870));
    REQUIRE(ppo.signal().data()[6] == Approx(7.1713254137));
    REQUIRE(ppo.histo().data()[6] == Approx(-0.2747736895));
}

TEST_CASE("KAMA matches backtrader-style adaptive smoothing warmup", "[indicator][kama]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0});

    stratforge::KAMA kama(source, 3, 2, 30);
    run_indicator(source, kama);

    REQUIRE(std::isnan(kama.line().data()[0]));
    REQUIRE(std::isnan(kama.line().data()[2]));
    REQUIRE(kama.line().data()[3] == Approx(12.0));
    REQUIRE(kama.line().data()[4] == Approx(12.6513277065));
    REQUIRE(kama.line().data()[6] == Approx(13.4144713014));
    REQUIRE(kama.line().data()[7] == Approx(14.2458787108));
}

TEST_CASE("DEMA matches double EMA composition semantics", "[indicator][dema]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0});

    stratforge::DEMA dema(source, 3);
    run_indicator(source, dema);

    REQUIRE(std::isnan(dema.line().data()[0]));
    REQUIRE(std::isnan(dema.line().data()[3]));
    REQUIRE(dema.line().data()[4] == Approx(14.5555555556));
    REQUIRE(dema.line().data()[5] == Approx(14.4444444444));
    REQUIRE(dema.line().data()[7] == Approx(17.6944444444));
}

TEST_CASE("ZeroLagIndicator matches error-correcting EMA behavior", "[indicator][zerolag]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0});

    stratforge::ZeroLagIndicator zl(source, 3, 5);
    run_indicator(source, zl);

    REQUIRE(std::isnan(zl.line().data()[0]));
    REQUIRE(std::isnan(zl.line().data()[1]));
    REQUIRE(zl.line().data()[2] == Approx(11.3333333333));
    REQUIRE(zl.line().data()[3] == Approx(11.6666666667));
    REQUIRE(zl.line().data()[6] == Approx(14.8333333333));
    REQUIRE(zl.line().data()[7] == Approx(16.4166666667));
}

TEST_CASE("DMA averages zero-lag and HMA outputs", "[indicator][dma]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0});

    stratforge::DMA dma(source, 3, 5, 2);
    run_indicator(source, dma);

    REQUIRE(std::isnan(dma.line().data()[0]));
    REQUIRE(std::isnan(dma.line().data()[1]));
    REQUIRE(dma.line().data()[2] == Approx(12.5));
    REQUIRE(dma.line().data()[3] == Approx(11.6666666667));
    REQUIRE(dma.line().data()[6] == Approx(15.75));
    REQUIRE(dma.line().data()[7] == Approx(17.5416666667));
}

TEST_CASE("KAMA and DMA wrappers expose envelope and oscillator outputs", "[indicator][kama][dma][wrappers]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0});

    stratforge::KAMAEnvelope kama_env(source, 3, 2, 30, 10.0);
    stratforge::KAMAOscillator kama_osc(source, 3, 2, 30);
    stratforge::DMAEnvelope dma_env(source, 3, 5, 2, 10.0);
    stratforge::DMAOscillator dma_osc(source, 3, 5, 2);

    for (std::size_t i = 0; i < source.size(); ++i) {
        kama_env.next();
        kama_osc.next();
        dma_env.next();
        dma_osc.next();
        if (i + 1 < source.size()) {
            source.advance();
        }
    }

    REQUIRE(kama_env.top().data()[4] == Approx(13.9164604771));
    REQUIRE(kama_env.bot().data()[7] == Approx(12.8212908397));
    REQUIRE(kama_osc.line().data()[4] == Approx(2.3486722935));
    REQUIRE(kama_osc.line().data()[7] == Approx(3.7541212892));

    REQUIRE(dma_env.top().data()[2] == Approx(13.75));
    REQUIRE(dma_env.bot().data()[7] == Approx(15.7875));
    REQUIRE(dma_osc.line().data()[6] == Approx(0.25));
    REQUIRE(dma_osc.line().data()[7] == Approx(0.4583333333));
}

TEST_CASE("TEMA matches triple EMA composition semantics", "[indicator][tema]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0});

    stratforge::TEMA tema(source, 3);
    run_indicator(source, tema);

    REQUIRE(std::isnan(tema.line().data()[0]));
    REQUIRE(std::isnan(tema.line().data()[5]));
    REQUIRE(tema.line().data()[6] == Approx(15.8703703704));
    REQUIRE(tema.line().data()[7] == Approx(17.8796296296));
    REQUIRE(tema.line().data()[9] == Approx(18.8327546296));
}

TEST_CASE("ZLEMA matches lag-adjusted EMA behavior", "[indicator][zlema]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0});

    stratforge::ZLEMA zlema(source, 3);
    run_indicator(source, zlema);

    REQUIRE(std::isnan(zlema.line().data()[0]));
    REQUIRE(std::isnan(zlema.line().data()[2]));
    REQUIRE(zlema.line().data()[3] == Approx(12.6666666667));
    REQUIRE(zlema.line().data()[6] == Approx(16.0833333333));
    REQUIRE(zlema.line().data()[9] == Approx(19.0104166667));
}

TEST_CASE("TEMA and ZLEMA wrappers expose envelope and oscillator outputs", "[indicator][tema][zlema][wrappers]") {
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0});

    stratforge::TEMAEnvelope tema_env(source, 3, 10.0);
    stratforge::TEMAOscillator tema_osc(source, 3);
    stratforge::ZLEMAEnvelope zlema_env(source, 3, 10.0);
    stratforge::ZLEMAOscillator zlema_osc(source, 3);

    for (std::size_t i = 0; i < source.size(); ++i) {
        tema_env.next();
        tema_osc.next();
        zlema_env.next();
        zlema_osc.next();
        if (i + 1 < source.size()) {
            source.advance();
        }
    }

    REQUIRE(tema_env.top().data()[6] == Approx(17.4574074074));
    REQUIRE(tema_env.bot().data()[9] == Approx(16.9494791667));
    REQUIRE(tema_osc.line().data()[7] == Approx(0.1203703704));
    REQUIRE(tema_osc.line().data()[8] == Approx(-0.3391203704));

    REQUIRE(zlema_env.top().data()[3] == Approx(13.9333333333));
    REQUIRE(zlema_env.bot().data()[9] == Approx(17.109375));
    REQUIRE(zlema_osc.line().data()[6] == Approx(-0.0833333333));
    REQUIRE(zlema_osc.line().data()[8] == Approx(-0.0208333333));
}

TEST_CASE("AO and AccDeOsc match backtrader-style median-price composition", "[indicator][ao][accdeosc]") {
    auto high = make_line({11.0, 12.0, 14.0, 13.0, 16.0, 15.0, 17.0, 19.0, 18.0, 20.0, 21.0, 19.0});
    auto low = make_line({9.0, 10.0, 12.0, 11.0, 14.0, 13.0, 15.0, 17.0, 16.0, 18.0, 19.0, 17.0});

    stratforge::AO ao(high, low, 3, 5);
    stratforge::AccDeOsc acc(high, low, 3);

    for (std::size_t i = 0; i < high.size(); ++i) {
        ao.next();
        acc.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
        }
    }

    REQUIRE(std::isnan(ao.line().data()[3]));
    REQUIRE(ao.line().data()[4] == Approx(1.1333333333));
    REQUIRE(ao.line().data()[9] == Approx(1.2));
    REQUIRE(ao.line().data()[11] == Approx(0.6));

    for (double value : acc.line().data()) {
        REQUIRE(std::isnan(value));
    }
}

TEST_CASE("DPO matches shifted SMA subtraction", "[indicator][dpo]") {
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0});

    stratforge::DPO dpo(close, 4);
    run_indicator(close, dpo);

    REQUIRE(std::isnan(dpo.line().data()[3]));
    REQUIRE(dpo.line().data()[4] == Approx(3.5));
    REQUIRE(dpo.line().data()[7] == Approx(3.75));
    REQUIRE(dpo.line().data()[11] == Approx(-0.5));
    REQUIRE(dpo.line().data()[13] == Approx(2.5));
}

TEST_CASE("PGO matches SMA distance scaled by ATR", "[indicator][pgo]") {
    auto high = make_line({11.0, 12.0, 14.0, 13.0, 16.0, 15.0, 17.0, 19.0, 18.0, 20.0, 21.0, 19.0, 22.0, 23.0});
    auto low = make_line({9.0, 10.0, 12.0, 11.0, 14.0, 13.0, 15.0, 17.0, 16.0, 18.0, 19.0, 17.0, 20.0, 21.0});
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0});

    stratforge::PGO pgo(high, low, close, 3);
    for (std::size_t i = 0; i < close.size(); ++i) {
        pgo.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(pgo.line().data()[2]));
    REQUIRE(pgo.line().data()[3] == Approx(0.0));
    REQUIRE(pgo.line().data()[4] == Approx(0.5769230769));
    REQUIRE(pgo.line().data()[11] == Approx(-0.3783227939));
    REQUIRE(pgo.line().data()[13] == Approx(0.6104265305));
}

TEST_CASE("BollingerBandsPct exposes mid top bot and pctb lines", "[indicator][bollinger][pctb]") {
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0});

    stratforge::BollingerBandsPct bbpct(close, 3, 2.0);
    run_indicator(close, bbpct);

    REQUIRE(std::isnan(bbpct.mid().data()[1]));
    REQUIRE(bbpct.mid().data()[2] == Approx(11.3333333333));
    REQUIRE(bbpct.top().data()[7] == Approx(19.2659863237));
    REQUIRE(bbpct.bot().data()[13] == Approx(16.9339869909));
    REQUIRE(bbpct.pctb().data()[2] == Approx(0.8340765524));
    REQUIRE(bbpct.pctb().data()[11] == Approx(0.1938137822));
}

TEST_CASE("PercentChange and CumSum match simple backtrader formulas", "[indicator][pctchange][cumsum]") {
    auto close_pct = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0});
    auto close_sum = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0});

    stratforge::PctChange pct(close_pct, 2);
    stratforge::CumSum cumsum(close_sum, 100.0);
    run_indicator(close_pct, pct);
    run_indicator(close_sum, cumsum);

    REQUIRE(std::isnan(pct.line().data()[1]));
    REQUIRE(pct.line().data()[2] == Approx(0.3));
    REQUIRE(pct.line().data()[11] == Approx(-0.0526315789));
    REQUIRE(pct.line().data()[13] == Approx(0.2222222222));

    REQUIRE(cumsum.line().data()[0] == Approx(110.0));
    REQUIRE(cumsum.line().data()[5] == Approx(175.0));
    REQUIRE(cumsum.line().data()[13] == Approx(326.0));
}

TEST_CASE("TRIX matches triple-smoothed rate of change", "[indicator][trix]") {
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0});

    stratforge::TRIX trix(close, 3);
    run_indicator(close, trix);

    REQUIRE(std::isnan(trix.line().data()[6]));
    REQUIRE(trix.line().data()[7] == Approx(8.4345961401));
    REQUIRE(trix.line().data()[10] == Approx(6.1839572969));
    REQUIRE(trix.line().data()[13] == Approx(4.5350804550));
}

TEST_CASE("Exponential smoothing uses SMA seed and EMA-style updates", "[indicator][expsmoothing]") {
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0});

    stratforge::ExponentialSmoothing smooth(close, 3);
    run_indicator(close, smooth);

    REQUIRE(std::isnan(smooth.line().data()[0]));
    REQUIRE(std::isnan(smooth.line().data()[1]));
    REQUIRE(smooth.line().data()[2] == Approx(11.3333333333));
    REQUIRE(smooth.line().data()[3] == Approx(11.6666666667));
    REQUIRE(smooth.line().data()[4] == Approx(13.3333333333));
    REQUIRE(smooth.line().data()[5] == Approx(13.6666666667));
}

TEST_CASE("Dynamic exponential smoothing reads alpha from an input line", "[indicator][expsmoothingdynamic]") {
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0});
    auto alpha = make_line({0.2, 0.2, 0.2, 0.4, 0.6, 0.3});

    stratforge::ExponentialSmoothingDynamic smooth(close, alpha, 3);
    for (std::size_t i = 0; i < close.size(); ++i) {
        smooth.next();
        if (i + 1 < close.size()) {
            close.advance();
            alpha.advance();
        }
    }

    REQUIRE(std::isnan(smooth.line().data()[0]));
    REQUIRE(std::isnan(smooth.line().data()[1]));
    REQUIRE(smooth.line().data()[2] == Approx(11.3333333333));
    REQUIRE(smooth.line().data()[3] == Approx(11.6));
    REQUIRE(smooth.line().data()[4] == Approx(13.64));
    REQUIRE(smooth.line().data()[5] == Approx(13.748));
}

TEST_CASE("RSI variants diverge by smoothing mode and safe division behavior", "[indicator][rsi][variants]") {
    auto rising = make_line({10.0, 11.0, 12.0, 13.0, 14.0});
    auto flat = make_line({10.0, 10.0, 10.0, 10.0, 10.0});
    auto mixed_smma = make_line({10.0, 12.0, 11.0, 13.0, 12.0, 14.0});
    auto mixed_sma = make_line({10.0, 12.0, 11.0, 13.0, 12.0, 14.0});
    auto mixed_ema = make_line({10.0, 12.0, 11.0, 13.0, 12.0, 14.0});

    stratforge::RSI rsi_rising(rising, 3);
    stratforge::RSI_Safe rsi_safe_flat(flat, 3);
    stratforge::RSI rsi_flat(flat, 3);
    stratforge::RSI rsi_smma(mixed_smma, 3);
    stratforge::RSI_SMA rsi_sma(mixed_sma, 3);
    stratforge::RSI_EMA rsi_ema(mixed_ema, 3);

    run_indicator(rising, rsi_rising);
    flat.home();
    run_indicator(flat, rsi_safe_flat);
    flat.home();
    run_indicator(flat, rsi_flat);
    run_indicator(mixed_smma, rsi_smma);
    run_indicator(mixed_sma, rsi_sma);
    run_indicator(mixed_ema, rsi_ema);

    REQUIRE(std::isnan(rsi_rising.line().data()[2]));
    REQUIRE(rsi_rising.line().data()[3] == Approx(100.0));
    REQUIRE(rsi_rising.line().data()[4] == Approx(100.0));

    REQUIRE(std::isnan(rsi_safe_flat.line().data()[2]));
    REQUIRE(rsi_safe_flat.line().data()[3] == Approx(50.0));
    REQUIRE(rsi_safe_flat.line().data()[4] == Approx(50.0));

    REQUIRE(std::isnan(rsi_flat.line().data()[2]));
    REQUIRE(std::isnan(rsi_flat.line().data()[3]));
    REQUIRE(std::isnan(rsi_flat.line().data()[4]));

    REQUIRE(rsi_smma.line().data()[3] == Approx(80.0));
    REQUIRE(rsi_smma.line().data()[4] == Approx(61.5384615385));
    REQUIRE(rsi_smma.line().data()[5] == Approx(77.2727272727));

    REQUIRE(rsi_sma.line().data()[3] == Approx(80.0));
    REQUIRE(rsi_sma.line().data()[4] == Approx(50.0));
    REQUIRE(rsi_sma.line().data()[5] == Approx(80.0));

    REQUIRE(rsi_ema.line().data()[3] == Approx(80.0));
    REQUIRE(rsi_ema.line().data()[4] == Approx(50.0));
    REQUIRE(rsi_ema.line().data()[5] == Approx(80.0));
}

TEST_CASE("TRIXSignal adds an EMA signal line on top of TRIX", "[indicator][trixsignal]") {
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0, 23.0, 24.0});

    stratforge::TrixSignal trix_signal(close, 3, 1, 3);
    run_indicator(close, trix_signal);

    REQUIRE(std::isnan(trix_signal.line().data()[6]));
    REQUIRE(trix_signal.line().data()[7] == Approx(8.4345961401));
    REQUIRE(std::isnan(trix_signal.signal().data()[7]));
    REQUIRE(std::isnan(trix_signal.signal().data()[8]));
    REQUIRE(trix_signal.signal().data()[9] == Approx(7.1566721398));
    REQUIRE(trix_signal.signal().data()[10] == Approx(6.6703147183));
    REQUIRE(trix_signal.signal().data()[15] == Approx(4.7719456424));
}

TEST_CASE("TSI and RMI match backtrader-style momentum smoothing semantics", "[indicator][tsi][rmi]") {
    auto close_tsi = make_line({11.0, 13.0, 16.0, 12.0, 18.0, 15.0, 21.0, 17.0, 22.0, 20.0, 25.0, 21.0, 27.0, 24.0, 29.0, 26.0, 31.0, 28.0, 33.0, 30.0});
    auto close_rmi = make_line({11.0, 13.0, 16.0, 12.0, 18.0, 15.0, 21.0, 17.0, 22.0, 20.0, 25.0, 21.0, 27.0, 24.0, 29.0, 26.0, 31.0, 28.0, 33.0, 30.0});

    stratforge::TSI tsi(close_tsi, 5, 3, 1);
    stratforge::RMI rmi(close_rmi, 5, 2);
    run_indicator(close_tsi, tsi);
    run_indicator(close_rmi, rmi);

    REQUIRE(std::isnan(tsi.line().data()[6]));
    REQUIRE(tsi.line().data()[7] == Approx(30.0724637681));
    REQUIRE(tsi.line().data()[10] == Approx(38.9096264696));
    REQUIRE(tsi.line().data()[15] == Approx(20.0291827014));
    REQUIRE(tsi.line().data()[19] == Approx(19.0024918080));

    REQUIRE(std::isnan(rmi.line().data()[5]));
    REQUIRE(rmi.line().data()[6] == Approx(92.8571428571));
    REQUIRE(rmi.line().data()[10] == Approx(96.7995999500));
    REQUIRE(rmi.line().data()[15] == Approx(98.7959706095));
    REQUIRE(rmi.line().data()[19] == Approx(99.4749211453));
}

TEST_CASE("StochasticFast, StochasticSlow, and Aroon aliases expose expected output lines", "[indicator][stochasticfast][stochasticslow][aroonindicator]") {
    auto high = make_line({13.0, 15.0, 17.0, 18.0, 19.0, 21.0, 22.0, 24.0, 23.0, 26.0, 27.0, 29.0, 30.0, 31.0, 32.0, 33.0, 34.0, 35.0, 36.0, 37.0});
    auto low = make_line({8.0, 9.0, 10.0, 11.0, 10.0, 14.0, 13.0, 16.0, 15.0, 18.0, 17.0, 20.0, 19.0, 22.0, 21.0, 23.0, 22.0, 24.0, 23.0, 25.0});
    auto close_sf = make_line({12.0, 14.0, 16.0, 12.0, 18.0, 15.0, 21.0, 17.0, 22.0, 20.0, 25.0, 21.0, 27.0, 24.0, 29.0, 26.0, 31.0, 28.0, 33.0, 30.0});
    auto close_ss = make_line({12.0, 14.0, 16.0, 12.0, 18.0, 15.0, 21.0, 17.0, 22.0, 20.0, 25.0, 21.0, 27.0, 24.0, 29.0, 26.0, 31.0, 28.0, 33.0, 30.0});
    stratforge::StochasticFast sf(high, low, close_sf, 5, 3);
    stratforge::StochasticSlow ss(high, low, close_ss, 5, 3, 3);
    stratforge::AroonIndicator ai(high, low, 5);
    stratforge::AroonOsc ao(high, low, 5);

    for (std::size_t i = 0; i < high.size(); ++i) {
        sf.next();
        ss.next();
        ai.next();
        ao.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
            close_sf.advance();
            close_ss.advance();
        }
    }

    REQUIRE(sf.percK().data()[4] == Approx(90.9090909091));
    REQUIRE(sf.percD().data()[6] == Approx(77.5252525253));
    REQUIRE(sf.percK().data()[19] == Approx(53.3333333333));

    REQUIRE(ss.percK().data()[6] == Approx(77.5252525253));
    REQUIRE(ss.percD().data()[8] == Approx(72.4025974026));
    REQUIRE(ss.percD().data()[19] == Approx(63.7037037037));

    REQUIRE(ai.aroonup().data()[5] == Approx(100.0));
    REQUIRE(ai.aroondown().data()[7] == Approx(40.0));
    REQUIRE(ao.line().data()[9] == Approx(100.0));
    REQUIRE(ao.line().data()[18] == Approx(80.0));
}

TEST_CASE("WilliamsAD accumulates close-vs-true-range pressure", "[indicator][williamsad]") {
    auto high = make_line({11.0, 12.0, 14.0, 13.0, 16.0, 15.0, 17.0, 19.0, 18.0, 20.0, 21.0, 19.0, 22.0, 23.0, 24.0, 22.0});
    auto low = make_line({9.0, 10.0, 12.0, 11.0, 14.0, 13.0, 15.0, 17.0, 16.0, 18.0, 19.0, 17.0, 20.0, 21.0, 22.0, 20.0});
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0, 23.0, 21.0});

    stratforge::WilliamsAD wad(high, low, close);
    for (std::size_t i = 0; i < close.size(); ++i) {
        wad.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(wad.line().data()[0]));
    REQUIRE(wad.line().data()[1] == Approx(1.0));
    REQUIRE(wad.line().data()[4] == Approx(5.0));
    REQUIRE(wad.line().data()[11] == Approx(8.0));
    REQUIRE(wad.line().data()[15] == Approx(11.0));
}

TEST_CASE("WillR matches backtrader windowed highest-lowest scaling", "[indicator][willr]") {
    auto high = make_line({11.0, 12.0, 14.0, 13.0, 16.0, 15.0, 17.0, 19.0, 18.0, 20.0, 21.0, 19.0, 22.0, 23.0, 24.0, 22.0});
    auto low = make_line({9.0, 10.0, 12.0, 11.0, 14.0, 13.0, 15.0, 17.0, 16.0, 18.0, 19.0, 17.0, 20.0, 21.0, 22.0, 20.0});
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0, 23.0, 21.0});

    stratforge::WillR willr(high, low, close, 3);
    for (std::size_t i = 0; i < close.size(); ++i) {
        willr.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(willr.line().data()[0]));
    REQUIRE(std::isnan(willr.line().data()[1]));
    REQUIRE(willr.line().data()[2] == Approx(-20.0));
    REQUIRE(willr.line().data()[7] == Approx(-16.6666666667));
    REQUIRE(willr.line().data()[11] == Approx(-75.0));
    REQUIRE(willr.line().data()[15] == Approx(-75.0));
}

TEST_CASE("UltimateOscillator matches weighted buying-pressure averages", "[indicator][ultimateoscillator]") {
    auto high = make_line({11.0, 12.0, 14.0, 13.0, 16.0, 15.0, 17.0, 19.0, 18.0, 20.0, 21.0, 19.0, 22.0, 23.0, 24.0, 22.0});
    auto low = make_line({9.0, 10.0, 12.0, 11.0, 14.0, 13.0, 15.0, 17.0, 16.0, 18.0, 19.0, 17.0, 20.0, 21.0, 22.0, 20.0});
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0, 23.0, 21.0});

    stratforge::UltimateOscillator uo(high, low, close, 3, 5, 7);
    for (std::size_t i = 0; i < close.size(); ++i) {
        uo.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(uo.line().data()[6]));
    REQUIRE(uo.line().data()[7] == Approx(63.1041890440));
    REQUIRE(uo.line().data()[10] == Approx(59.2580351979));
    REQUIRE(uo.line().data()[13] == Approx(56.3432390500));
    REQUIRE(uo.line().data()[15] == Approx(46.2943071966));
}

TEST_CASE("Vortex exposes plus and minus directional movement ratios", "[indicator][vortex]") {
    auto high = make_line({11.0, 12.0, 14.0, 13.0, 16.0, 15.0, 17.0, 19.0, 18.0, 20.0, 21.0, 19.0, 22.0, 23.0, 24.0, 22.0});
    auto low = make_line({9.0, 10.0, 12.0, 11.0, 14.0, 13.0, 15.0, 17.0, 16.0, 18.0, 19.0, 17.0, 20.0, 21.0, 22.0, 20.0});
    auto close = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0, 18.0, 17.0, 19.0, 20.0, 18.0, 21.0, 22.0, 23.0, 21.0});

    stratforge::Vortex vortex(high, low, close, 3);
    for (std::size_t i = 0; i < close.size(); ++i) {
        vortex.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(vortex.vi_plus().data()[0]));
    REQUIRE(std::isnan(vortex.vi_plus().data()[2]));
    REQUIRE(vortex.vi_plus().data()[3] == Approx(1.1428571429));
    REQUIRE(vortex.vi_plus().data()[10] == Approx(1.1428571429));
    REQUIRE(vortex.vi_plus().data()[15] == Approx(0.8571428571));

    REQUIRE(vortex.vi_minus().data()[3] == Approx(0.5714285714));
    REQUIRE(vortex.vi_minus().data()[11] == Approx(0.625));
    REQUIRE(vortex.vi_minus().data()[15] == Approx(0.8571428571));
}

TEST_CASE("DV2 matches smoothed close-to-midpoint percent rank semantics", "[indicator][dv2]") {
    auto high = make_line({12.0, 13.0, 15.0, 14.0, 17.0, 16.0, 18.0, 20.0, 19.0, 21.0, 22.0, 23.0});
    auto low = make_line({9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 15.0, 18.0, 19.0, 17.0});
    auto close = make_line({11.0, 12.0, 14.0, 12.5, 16.0, 14.5, 17.5, 18.0, 16.0, 20.0, 21.0, 18.5});

    stratforge::DV2 dv2(high, low, close, 5, 2);
    for (std::size_t i = 0; i < close.size(); ++i) {
        dv2.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(dv2.line().data()[4]));
    REQUIRE(dv2.line().data()[5] == Approx(20.0));
    REQUIRE(dv2.line().data()[6] == Approx(0.0));
    REQUIRE(dv2.line().data()[7] == Approx(80.0));
    REQUIRE(dv2.line().data()[10] == Approx(60.0));
    REQUIRE(dv2.line().data()[11] == Approx(20.0));
}
