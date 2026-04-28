#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/indicators/stochrsi.hpp>
#include <stratforge/indicators/macdext.hpp>
#include <stratforge/indicators/aroon.hpp>
#include <stratforge/indicators/directionalmovement.hpp>
#include <stratforge/indicators/envelope.hpp>
#include <stratforge/indicators/oscillator.hpp>
#include <stratforge/indicators/oscillator_extra.hpp>

#include "test_helpers.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;
using stratforge::test::generate_sine_data;
using stratforge::test::generate_trend_data;
using stratforge::test::make_line;
using stratforge::test::run_indicator;
using stratforge::test::run_indicator_hl;
using stratforge::test::run_indicator_hlc;

// =============================================================================
// StochasticSlow (alias for Stochastic)
// =============================================================================

TEST_CASE("StochasticSlow is alias for Stochastic", "[indicator][stochastic][slow]") {
    static_assert(std::is_same_v<stratforge::StochasticSlow, stratforge::Stochastic>,
                  "StochasticSlow must be an alias for Stochastic");
}

TEST_CASE("StochasticSlow produces slow %K and %D", "[indicator][stochastic][slow]") {
    auto values = generate_sine_data(30);
    auto high  = make_line(values);
    auto low   = make_line(values);
    auto close = make_line(values);

    // Adjust high/low from close
    for (std::size_t i = 0; i < values.size(); ++i) {
        high.data()[i] = values[i] + 2.0;
        low.data()[i] = values[i] - 2.0;
    }

    stratforge::StochasticSlow stoch(high, low, close, 5, 3, 3);
    run_indicator_hlc(high, low, close, stoch);

    // minimum_period = 5 + 3 + 3 - 2 = 9
    REQUIRE(stoch.minimum_period() == 9);

    // Early values should be NaN
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(std::isnan(stoch.percK().data()[i]));
    }

    // After warmup, percK and percD should be valid and in [0, 100]
    bool found_valid_k = false;
    bool found_valid_d = false;
    for (std::size_t i = 0; i < stoch.percK().data().size(); ++i) {
        const double k = stoch.percK().data()[i];
        const double d = stoch.percD().data()[i];
        if (!std::isnan(k)) {
            REQUIRE(k >= 0.0);
            REQUIRE(k <= 100.0);
            found_valid_k = true;
        }
        if (!std::isnan(d)) {
            REQUIRE(d >= 0.0);
            REQUIRE(d <= 100.0);
            found_valid_d = true;
        }
    }
    REQUIRE(found_valid_k);
    REQUIRE(found_valid_d);
}

// =============================================================================
// StochRSI percD coverage
// =============================================================================

TEST_CASE("StochRSI percD produces valid SMA of percK", "[indicator][stochrsi][percd]") {
    auto values = generate_sine_data(60);
    auto source = make_line(values);

    stratforge::StochRSI stochrsi(source, 14, 14, 3);
    run_indicator(source, stochrsi);

    // percD should lag behind percK by period_d
    bool found_valid_d = false;
    for (std::size_t i = 0; i < stochrsi.percD().data().size(); ++i) {
        const double d = stochrsi.percD().data()[i];
        if (!std::isnan(d)) {
            REQUIRE(d >= 0.0);
            REQUIRE(d <= 100.0);
            found_valid_d = true;
        }
    }
    REQUIRE(found_valid_d);

    // Verify percD is SMA(percK, 3): check last value manually
    const auto& k_data = stochrsi.percK().data();
    const auto& d_data = stochrsi.percD().data();
    const std::size_t last = d_data.size() - 1;
    if (!std::isnan(d_data[last]) && last >= 2) {
        const double expected_d = (k_data[last] + k_data[last - 1] + k_data[last - 2]) / 3.0;
        REQUIRE(d_data[last] == Approx(expected_d).epsilon(1e-10));
    }
}

// =============================================================================
// MACDExt with different MA types
// =============================================================================

TEST_CASE("MACDExt with EMA produces valid output", "[indicator][macdext][ema]") {
    auto values = generate_trend_data(50);
    auto source = make_line(values);

    stratforge::MACDExt macdext(source, 5, stratforge::MaType::EMA, 10, stratforge::MaType::EMA,
                             3, stratforge::MaType::EMA);
    run_indicator(source, macdext);

    bool found_valid = false;
    for (std::size_t i = 0; i < macdext.macd().data().size(); ++i) {
        if (!std::isnan(macdext.macd().data()[i])) {
            found_valid = true;
            // In an uptrend, fast EMA > slow EMA, so MACD > 0
            REQUIRE(macdext.macd().data()[i] > 0.0);
            break;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("MACDExt with WMA produces valid output", "[indicator][macdext][wma]") {
    auto values = generate_trend_data(50);
    auto source = make_line(values);

    stratforge::MACDExt macdext(source, 5, stratforge::MaType::WMA, 10, stratforge::MaType::WMA,
                             3, stratforge::MaType::WMA);
    run_indicator(source, macdext);

    bool found_valid = false;
    for (std::size_t i = 0; i < macdext.macd().data().size(); ++i) {
        if (!std::isnan(macdext.macd().data()[i])) {
            found_valid = true;
            break;
        }
    }
    REQUIRE(found_valid);

    // Signal and histogram should also have valid values
    bool found_signal = false;
    bool found_histo = false;
    for (std::size_t i = 0; i < macdext.signal().data().size(); ++i) {
        if (!std::isnan(macdext.signal().data()[i])) found_signal = true;
        if (!std::isnan(macdext.histogram().data()[i])) found_histo = true;
    }
    REQUIRE(found_signal);
    REQUIRE(found_histo);
}

TEST_CASE("MACDExt with DEMA produces valid output", "[indicator][macdext][dema]") {
    auto values = generate_trend_data(60);
    auto source = make_line(values);

    stratforge::MACDExt macdext(source, 5, stratforge::MaType::DEMA, 10, stratforge::MaType::DEMA,
                             3, stratforge::MaType::DEMA);
    run_indicator(source, macdext);

    bool found_valid = false;
    for (std::size_t i = 0; i < macdext.macd().data().size(); ++i) {
        if (!std::isnan(macdext.macd().data()[i])) {
            found_valid = true;
            break;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("MACDExt with TEMA produces valid output", "[indicator][macdext][tema]") {
    auto values = generate_trend_data(80);
    auto source = make_line(values);

    stratforge::MACDExt macdext(source, 5, stratforge::MaType::TEMA, 10, stratforge::MaType::TEMA,
                             3, stratforge::MaType::TEMA);
    run_indicator(source, macdext);

    bool found_valid = false;
    for (std::size_t i = 0; i < macdext.macd().data().size(); ++i) {
        if (!std::isnan(macdext.macd().data()[i])) {
            found_valid = true;
            break;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("MACDExt with mixed MA types", "[indicator][macdext][mixed]") {
    auto values = generate_sine_data(60);
    auto source = make_line(values);

    stratforge::MACDExt macdext(source, 5, stratforge::MaType::EMA, 10, stratforge::MaType::SMA,
                             3, stratforge::MaType::WMA);
    run_indicator(source, macdext);

    bool found_valid = false;
    for (std::size_t i = 0; i < macdext.macd().data().size(); ++i) {
        if (!std::isnan(macdext.macd().data()[i])) {
            found_valid = true;
            break;
        }
    }
    REQUIRE(found_valid);
}

// =============================================================================
// Aroon aliases: AroonIndicator, AroonOsc, AroonUpDown, AroonUpDownOscillator
// =============================================================================

TEST_CASE("AroonOscillator inherits from Aroon", "[indicator][aroon][osc]") {
    auto values = generate_sine_data(30);
    auto high = make_line(values);
    auto low = make_line(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
        high.data()[i] = values[i] + 2.0;
        low.data()[i] = values[i] - 2.0;
    }

    stratforge::AroonOscillator osc(high, low, 5);
    run_indicator_hl(high, low, osc);

    // Oscillator value = up - down, in range [-100, 100]
    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        const double v = osc.line().data()[i];
        if (!std::isnan(v)) {
            REQUIRE(v >= -100.0);
            REQUIRE(v <= 100.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("AroonUpDown exposes up and down lines", "[indicator][aroon][updown]") {
    auto values = generate_sine_data(30);
    auto high = make_line(values);
    auto low = make_line(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
        high.data()[i] = values[i] + 2.0;
        low.data()[i] = values[i] - 2.0;
    }

    stratforge::AroonUpDown updown(high, low, 5);
    run_indicator_hl(high, low, updown);

    // line() outputs NaN (by design), but up() and down() should have values
    bool found_up = false;
    bool found_down = false;
    for (std::size_t i = 0; i < updown.up().data().size(); ++i) {
        if (!std::isnan(updown.up().data()[i])) {
            REQUIRE(updown.up().data()[i] >= 0.0);
            REQUIRE(updown.up().data()[i] <= 100.0);
            found_up = true;
        }
        if (!std::isnan(updown.down().data()[i])) {
            REQUIRE(updown.down().data()[i] >= 0.0);
            REQUIRE(updown.down().data()[i] <= 100.0);
            found_down = true;
        }
    }
    REQUIRE(found_up);
    REQUIRE(found_down);

    // aroonup() and aroondown() should be same as up()/down()
    for (std::size_t i = 0; i < updown.aroonup().data().size(); ++i) {
        if (!std::isnan(updown.aroonup().data()[i])) {
            REQUIRE(updown.aroonup().data()[i] == updown.up().data()[i]);
        }
    }
}

TEST_CASE("AroonIndicator is alias for AroonUpDown", "[indicator][aroon][alias]") {
    static_assert(std::is_same_v<stratforge::AroonIndicator, stratforge::AroonUpDown>,
                  "AroonIndicator must be alias for AroonUpDown");
}

TEST_CASE("AroonUpDownOscillator exposes up/down and oscillator", "[indicator][aroon][updownosc]") {
    auto values = generate_sine_data(30);
    auto high = make_line(values);
    auto low = make_line(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
        high.data()[i] = values[i] + 2.0;
        low.data()[i] = values[i] - 2.0;
    }

    stratforge::AroonUpDownOscillator audo(high, low, 5);
    run_indicator_hl(high, low, audo);

    bool found_valid = false;
    for (std::size_t i = 0; i < audo.line().data().size(); ++i) {
        if (!std::isnan(audo.line().data()[i])) {
            REQUIRE(audo.line().data()[i] >= -100.0);
            REQUIRE(audo.line().data()[i] <= 100.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);

    // up() and down() should also be accessible
    bool found_up = false;
    for (std::size_t i = 0; i < audo.up().data().size(); ++i) {
        if (!std::isnan(audo.up().data()[i])) found_up = true;
    }
    REQUIRE(found_up);
}

TEST_CASE("AroonUpDownOsc is alias for AroonUpDownOscillator", "[indicator][aroon][alias]") {
    static_assert(std::is_same_v<stratforge::AroonUpDownOsc, stratforge::AroonUpDownOscillator>,
                  "AroonUpDownOsc must be alias for AroonUpDownOscillator");
}

// =============================================================================
// PlusDirectionalIndicator / MinusDirectionalIndicator (standalone wrappers)
// =============================================================================

TEST_CASE("PlusDirectionalIndicator outputs +DI", "[indicator][dm][plusdi]") {
    auto values = generate_sine_data(40);
    auto high  = make_line(values);
    auto low   = make_line(values);
    auto close = make_line(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
        high.data()[i] = values[i] + 3.0;
        low.data()[i] = values[i] - 3.0;
    }

    stratforge::PlusDirectionalIndicator pdi(high, low, close, 5);
    run_indicator_hlc(high, low, close, pdi);

    REQUIRE(pdi.minimum_period() == 10); // period * 2

    bool found_valid = false;
    for (std::size_t i = 0; i < pdi.line().data().size(); ++i) {
        if (!std::isnan(pdi.line().data()[i])) {
            REQUIRE(pdi.line().data()[i] >= 0.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("MinusDirectionalIndicator outputs -DI", "[indicator][dm][minusdi]") {
    auto values = generate_sine_data(40);
    auto high  = make_line(values);
    auto low   = make_line(values);
    auto close = make_line(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
        high.data()[i] = values[i] + 3.0;
        low.data()[i] = values[i] - 3.0;
    }

    stratforge::MinusDirectionalIndicator mdi(high, low, close, 5);
    run_indicator_hlc(high, low, close, mdi);

    bool found_valid = false;
    for (std::size_t i = 0; i < mdi.line().data().size(); ++i) {
        if (!std::isnan(mdi.line().data()[i])) {
            REQUIRE(mdi.line().data()[i] >= 0.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("PlusDI and MinusDI match DirectionalMovement outputs", "[indicator][dm][consistency]") {
    auto values = generate_sine_data(40);
    auto high1  = make_line(values);
    auto low1   = make_line(values);
    auto close1 = make_line(values);
    auto high2  = make_line(values);
    auto low2   = make_line(values);
    auto close2 = make_line(values);
    auto high3  = make_line(values);
    auto low3   = make_line(values);
    auto close3 = make_line(values);

    for (std::size_t i = 0; i < values.size(); ++i) {
        high1.data()[i] = high2.data()[i] = high3.data()[i] = values[i] + 3.0;
        low1.data()[i] = low2.data()[i] = low3.data()[i] = values[i] - 3.0;
    }

    stratforge::DirectionalMovement dm(high1, low1, close1, 5);
    stratforge::PlusDirectionalIndicator pdi(high2, low2, close2, 5);
    stratforge::MinusDirectionalIndicator mdi(high3, low3, close3, 5);

    run_indicator_hlc(high1, low1, close1, dm);
    run_indicator_hlc(high2, low2, close2, pdi);
    run_indicator_hlc(high3, low3, close3, mdi);

    for (std::size_t i = 0; i < dm.plus_di().data().size(); ++i) {
        if (!std::isnan(dm.plus_di().data()[i])) {
            REQUIRE(pdi.line().data()[i] == Approx(dm.plus_di().data()[i]).epsilon(1e-10));
        }
        if (!std::isnan(dm.minus_di().data()[i])) {
            REQUIRE(mdi.line().data()[i] == Approx(dm.minus_di().data()[i]).epsilon(1e-10));
        }
    }
}

TEST_CASE("DirectionalIndicator exposes plus_di and minus_di", "[indicator][dm][directional]") {
    auto values = generate_sine_data(40);
    auto high  = make_line(values);
    auto low   = make_line(values);
    auto close = make_line(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
        high.data()[i] = values[i] + 3.0;
        low.data()[i] = values[i] - 3.0;
    }

    stratforge::DirectionalIndicator di(high, low, close, 5);
    run_indicator_hlc(high, low, close, di);

    bool found_plus = false;
    bool found_minus = false;
    for (std::size_t i = 0; i < di.plus_di().data().size(); ++i) {
        if (!std::isnan(di.plus_di().data()[i])) found_plus = true;
        if (!std::isnan(di.minus_di().data()[i])) found_minus = true;
    }
    REQUIRE(found_plus);
    REQUIRE(found_minus);
}

TEST_CASE("ADX standalone matches DirectionalMovement ADX", "[indicator][dm][adx]") {
    auto values = generate_sine_data(50);
    auto high1  = make_line(values);
    auto low1   = make_line(values);
    auto close1 = make_line(values);
    auto high2  = make_line(values);
    auto low2   = make_line(values);
    auto close2 = make_line(values);

    for (std::size_t i = 0; i < values.size(); ++i) {
        high1.data()[i] = high2.data()[i] = values[i] + 3.0;
        low1.data()[i] = low2.data()[i] = values[i] - 3.0;
    }

    stratforge::DirectionalMovement dm(high1, low1, close1, 5);
    stratforge::ADX adx(high2, low2, close2, 5);

    run_indicator_hlc(high1, low1, close1, dm);
    run_indicator_hlc(high2, low2, close2, adx);

    for (std::size_t i = 0; i < dm.adx().data().size(); ++i) {
        if (!std::isnan(dm.adx().data()[i])) {
            REQUIRE(adx.line().data()[i] == Approx(dm.adx().data()[i]).epsilon(1e-10));
        }
    }
}

TEST_CASE("ADXR standalone matches DirectionalMovement ADXR", "[indicator][dm][adxr]") {
    auto values = generate_sine_data(60);
    auto high1  = make_line(values);
    auto low1   = make_line(values);
    auto close1 = make_line(values);
    auto high2  = make_line(values);
    auto low2   = make_line(values);
    auto close2 = make_line(values);

    for (std::size_t i = 0; i < values.size(); ++i) {
        high1.data()[i] = high2.data()[i] = values[i] + 3.0;
        low1.data()[i] = low2.data()[i] = values[i] - 3.0;
    }

    stratforge::DirectionalMovement dm(high1, low1, close1, 5);
    stratforge::ADXR adxr(high2, low2, close2, 5);

    run_indicator_hlc(high1, low1, close1, dm);
    run_indicator_hlc(high2, low2, close2, adxr);

    for (std::size_t i = 0; i < dm.adxr().data().size(); ++i) {
        if (!std::isnan(dm.adxr().data()[i])) {
            REQUIRE(adxr.line().data()[i] == Approx(dm.adxr().data()[i]).epsilon(1e-10));
        }
    }
}

// =============================================================================
// Envelope wrappers (EMA, SMMA, WMA, HMA, DEMA, ZLIndicatorEnvelope)
// =============================================================================

TEST_CASE("EMAEnvelope produces MA with bands", "[indicator][envelope][ema]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::EMAEnvelope env(source, 5, 2.5);
    run_indicator(source, env);

    bool found_valid = false;
    for (std::size_t i = 0; i < env.src().data().size(); ++i) {
        if (!std::isnan(env.src().data()[i])) {
            const double src = env.src().data()[i];
            REQUIRE(env.top().data()[i] == Approx(src * 1.025).epsilon(1e-10));
            REQUIRE(env.bot().data()[i] == Approx(src * 0.975).epsilon(1e-10));
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("SMMAEnvelope produces MA with bands", "[indicator][envelope][smma]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::SMMAEnvelope env(source, 5, 3.0);
    run_indicator(source, env);

    bool found_valid = false;
    for (std::size_t i = 0; i < env.src().data().size(); ++i) {
        if (!std::isnan(env.src().data()[i])) {
            const double src = env.src().data()[i];
            REQUIRE(env.top().data()[i] == Approx(src * 1.03).epsilon(1e-10));
            REQUIRE(env.bottom().data()[i] == Approx(src * 0.97).epsilon(1e-10));
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("WMAEnvelope produces MA with bands", "[indicator][envelope][wma]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::WMAEnvelope env(source, 5, 2.0);
    run_indicator(source, env);

    bool found_valid = false;
    for (std::size_t i = 0; i < env.src().data().size(); ++i) {
        if (!std::isnan(env.src().data()[i])) {
            found_valid = true;
            REQUIRE(env.top().data()[i] > env.src().data()[i]);
            REQUIRE(env.bot().data()[i] < env.src().data()[i]);
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("HMAEnvelope produces MA with bands", "[indicator][envelope][hma]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::HMAEnvelope env(source, 5, 2.5);
    run_indicator(source, env);

    bool found_valid = false;
    for (std::size_t i = 0; i < env.src().data().size(); ++i) {
        if (!std::isnan(env.src().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("DEMAEnvelope produces MA with bands", "[indicator][envelope][dema]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::DEMAEnvelope env(source, 5, 2.5);
    run_indicator(source, env);

    bool found_valid = false;
    for (std::size_t i = 0; i < env.src().data().size(); ++i) {
        if (!std::isnan(env.src().data()[i])) {
            const double src = env.src().data()[i];
            REQUIRE(env.top().data()[i] == Approx(src * 1.025).epsilon(1e-10));
            REQUIRE(env.bot().data()[i] == Approx(src * 0.975).epsilon(1e-10));
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("ZLIndicatorEnvelope produces bands around ZeroLagIndicator", "[indicator][envelope][zlindicator]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::ZLIndicatorEnvelope env(source, 5, 50, 2.5);
    run_indicator(source, env);

    bool found_valid = false;
    for (std::size_t i = 0; i < env.src().data().size(); ++i) {
        if (!std::isnan(env.src().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

// Verify alias chains
TEST_CASE("Envelope alias type checks", "[indicator][envelope][alias]") {
    static_assert(std::is_same_v<stratforge::ExponentialMovingAverageEnvelope, stratforge::EMAEnvelope>);
    static_assert(std::is_same_v<stratforge::SmoothedMovingAverageEnvelope, stratforge::SMMAEnvelope>);
    static_assert(std::is_same_v<stratforge::WilderMAEnvelope, stratforge::SMMAEnvelope>);
    static_assert(std::is_same_v<stratforge::WeightedMovingAverageEnvelope, stratforge::WMAEnvelope>);
    static_assert(std::is_same_v<stratforge::HullMAEnvelope, stratforge::HMAEnvelope>);
    static_assert(std::is_same_v<stratforge::DoubleExponentialMovingAverageEnvelope, stratforge::DEMAEnvelope>);
    static_assert(std::is_same_v<stratforge::ZeroLagIndicatorEnvelope, stratforge::ZLIndicatorEnvelope>);
}

// =============================================================================
// Oscillator wrappers (EMA, SMMA, WMA, HMA, DEMA)
// =============================================================================

TEST_CASE("EMAOscillator computes source - EMA", "[indicator][oscillator][ema]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::EMAOscillator osc(source, 5);
    run_indicator(source, osc);

    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        if (!std::isnan(osc.line().data()[i])) {
            // In uptrend, source > EMA, so oscillator > 0
            REQUIRE(osc.line().data()[i] > 0.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("SMMAOscillator computes source - SMMA", "[indicator][oscillator][smma]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::SMMAOscillator osc(source, 5);
    run_indicator(source, osc);

    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        if (!std::isnan(osc.line().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("WMAOscillator computes source - WMA", "[indicator][oscillator][wma]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::WMAOscillator osc(source, 5);
    run_indicator(source, osc);

    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        if (!std::isnan(osc.line().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("HMAOscillator computes source - HMA", "[indicator][oscillator][hma]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::HMAOscillator osc(source, 5);
    run_indicator(source, osc);

    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        if (!std::isnan(osc.line().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("DEMAOscillator computes source - DEMA", "[indicator][oscillator][dema]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::DEMAOscillator osc(source, 5);
    run_indicator(source, osc);

    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        if (!std::isnan(osc.line().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("TEMAOscillator computes source - TEMA", "[indicator][oscillator][tema]") {
    auto values = generate_trend_data(40);
    auto source = make_line(values);

    stratforge::TEMAOscillator osc(source, 5);
    run_indicator(source, osc);

    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        if (!std::isnan(osc.line().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("ZLEMAOscillator computes source - ZLEMA", "[indicator][oscillator][zlema]") {
    auto values = generate_trend_data(30);
    auto source = make_line(values);

    stratforge::ZLEMAOscillator osc(source, 5);
    run_indicator(source, osc);

    bool found_valid = false;
    for (std::size_t i = 0; i < osc.line().data().size(); ++i) {
        if (!std::isnan(osc.line().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

// Verify alias chains
TEST_CASE("Oscillator alias type checks", "[indicator][oscillator][alias]") {
    static_assert(std::is_same_v<stratforge::ExponentialMovingAverageOscillator, stratforge::EMAOscillator>);
    static_assert(std::is_same_v<stratforge::SmoothedMovingAverageOscillator, stratforge::SMMAOscillator>);
    static_assert(std::is_same_v<stratforge::WilderMAOscillator, stratforge::SMMAOscillator>);
    static_assert(std::is_same_v<stratforge::WeightedMovingAverageOscillator, stratforge::WMAOscillator>);
    static_assert(std::is_same_v<stratforge::HullMAOscillator, stratforge::HMAOscillator>);
    static_assert(std::is_same_v<stratforge::DoubleExponentialMovingAverageOscillator, stratforge::DEMAOscillator>);
    static_assert(std::is_same_v<stratforge::TripleExponentialMovingAverageOscillator, stratforge::TEMAOscillator>);
    static_assert(std::is_same_v<stratforge::ZeroLagEmaOscillator, stratforge::ZLEMAOscillator>);
    static_assert(std::is_same_v<stratforge::ECOscillator, stratforge::ZeroLagIndicatorOscillator>);
}

// =============================================================================
// PPOShort (PercentagePriceOscillatorShort)
// =============================================================================

TEST_CASE("PPOShort uses fast EMA as denominator", "[indicator][oscillator][pposhort]") {
    auto values = generate_trend_data(50);
    auto source1 = make_line(values);
    auto source2 = make_line(values);

    stratforge::PPO ppo(source1, 12, 26, 9);
    stratforge::PPOShort ppo_short(source2, 12, 26, 9);

    run_indicator(source1, ppo);
    run_indicator(source2, ppo_short);

    // Both should produce valid output, but different values due to denominator
    bool found_ppo = false;
    bool found_pposhort = false;
    bool values_differ = false;
    for (std::size_t i = 0; i < ppo.ppo().data().size(); ++i) {
        if (!std::isnan(ppo.ppo().data()[i])) found_ppo = true;
        if (!std::isnan(ppo_short.ppo().data()[i])) found_pposhort = true;
        if (!std::isnan(ppo.ppo().data()[i]) && !std::isnan(ppo_short.ppo().data()[i])) {
            if (std::abs(ppo.ppo().data()[i] - ppo_short.ppo().data()[i]) > 1e-6) {
                values_differ = true;
            }
        }
    }
    REQUIRE(found_ppo);
    REQUIRE(found_pposhort);
    REQUIRE(values_differ); // Different denominators should produce different values
}

TEST_CASE("PPOShort signal and histogram", "[indicator][oscillator][pposhort][signal]") {
    auto values = generate_sine_data(60);
    auto source = make_line(values);

    stratforge::PPOShort ppo_short(source, 12, 26, 9);
    run_indicator(source, ppo_short);

    bool found_signal = false;
    bool found_histo = false;
    for (std::size_t i = 0; i < ppo_short.signal().data().size(); ++i) {
        if (!std::isnan(ppo_short.signal().data()[i])) found_signal = true;
        if (!std::isnan(ppo_short.histo().data()[i])) found_histo = true;
    }
    REQUIRE(found_signal);
    REQUIRE(found_histo);
}

// =============================================================================
// ChandeMomentumOscillator (CMO) and T3
// =============================================================================

TEST_CASE("ChandeMomentumOscillator produces values in [-100, 100]", "[indicator][oscillator_extra][cmo]") {
    auto values = generate_sine_data(30);
    auto source = make_line(values);

    stratforge::CMO cmo(source, 10);
    run_indicator(source, cmo);

    REQUIRE(cmo.minimum_period() == 11);

    bool found_valid = false;
    for (std::size_t i = 0; i < cmo.line().data().size(); ++i) {
        if (!std::isnan(cmo.line().data()[i])) {
            REQUIRE(cmo.line().data()[i] >= -100.0);
            REQUIRE(cmo.line().data()[i] <= 100.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("T3MovingAverage produces smoothed output", "[indicator][oscillator_extra][t3]") {
    auto values = generate_trend_data(40);
    auto source = make_line(values);

    stratforge::T3 t3(source, 5, 0.7);
    run_indicator(source, t3);

    // minimum_period = 6*5 - 5 = 25
    REQUIRE(t3.minimum_period() == 25);

    bool found_valid = false;
    for (std::size_t i = 0; i < t3.line().data().size(); ++i) {
        if (!std::isnan(t3.line().data()[i])) {
            REQUIRE(t3.line().data()[i] > 0.0);
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

TEST_CASE("CoppockCurve produces valid output", "[indicator][oscillator_extra][coppock]") {
    auto values = generate_sine_data(50);
    auto source = make_line(values);

    stratforge::Coppock coppock(source, 11, 14, 10);
    run_indicator(source, coppock);

    bool found_valid = false;
    for (std::size_t i = 0; i < coppock.line().data().size(); ++i) {
        if (!std::isnan(coppock.line().data()[i])) {
            found_valid = true;
        }
    }
    REQUIRE(found_valid);
}

// =============================================================================
// Envelope: raw Envelope (no MA)
// =============================================================================

TEST_CASE("Envelope wraps source with percentage bands", "[indicator][envelope][raw]") {
    auto source = make_line({100.0, 110.0, 90.0});

    stratforge::Envelope env(source, 5.0); // 5% bands
    run_indicator(source, env);

    REQUIRE(env.src().data()[0] == Approx(100.0));
    REQUIRE(env.top().data()[0] == Approx(105.0));
    REQUIRE(env.bot().data()[0] == Approx(95.0));

    REQUIRE(env.src().data()[1] == Approx(110.0));
    REQUIRE(env.top().data()[1] == Approx(115.5));
    REQUIRE(env.bot().data()[1] == Approx(104.5));

    REQUIRE(env.src().data()[2] == Approx(90.0));
    REQUIRE(env.top().data()[2] == Approx(94.5));
    REQUIRE(env.bottom().data()[2] == Approx(85.5));
}
