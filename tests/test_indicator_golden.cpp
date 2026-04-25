#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/aroon.hpp>
#include <stratforge/indicators/cci.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/crossover.hpp>
#include <stratforge/indicators/directionalmovement.hpp>
#include <stratforge/indicators/dv2.hpp>
#include <stratforge/indicators/heikinashi.hpp>
#include <stratforge/indicators/hma.hpp>
#include <stratforge/indicators/macd.hpp>
#include <stratforge/indicators/meandeviation.hpp>
#include <stratforge/indicators/momentum.hpp>
#include <stratforge/indicators/psar.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/smma.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/indicators/stochastic.hpp>
#include <stratforge/indicators/tsi.hpp>
#include <stratforge/indicators/ultimateoscillator.hpp>
#include <stratforge/indicators/vortex.hpp>
#include <stratforge/indicators/williams.hpp>
#include <stratforge/indicators/wma.hpp>

#include "golden_reference.hpp"

#include <cmath>
#include <filesystem>
#include <string>

using Catch::Matchers::WithinRel;

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

} // namespace

template <typename IndicatorType>
void require_indicator_matches_fixture(const std::string& fixture_relpath) {
    const auto fixture = stratforge::test::load_indicator_golden_reference(source_path(fixture_relpath));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    IndicatorType indicator(feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    REQUIRE(indicator.line().size() == fixture.values.size());

    for (std::size_t i = 0; i < fixture.values.size(); ++i) {
        const double expected = stratforge::test::parse_golden_double(fixture.values[i]);
        const double actual = indicator.line().data()[i];

        INFO("bar=" << i);
        if (std::isnan(expected)) {
            REQUIRE(std::isnan(actual));
        } else {
            REQUIRE_THAT(actual, WithinRel(expected, 0.000001));
        }
    }
}

void require_line_matches_fixture(const stratforge::Line<double>& line,
                                  const std::vector<std::string>& expected_values,
                                  const std::string& label) {
    REQUIRE(line.size() == expected_values.size());
    for (std::size_t i = 0; i < expected_values.size(); ++i) {
        const double expected = stratforge::test::parse_golden_double(expected_values[i]);
        const double actual = line.data()[i];

        INFO("line=" << label << " bar=" << i);
        if (std::isnan(expected)) {
            REQUIRE(std::isnan(actual));
        } else {
            REQUIRE_THAT(actual, WithinRel(expected, 0.000001));
        }
    }
}

TEST_CASE("SMA matches backtrader golden reference", "[golden][indicator][sma]") {
    require_indicator_matches_fixture<stratforge::SMA>("tests/golden/sma_p30_2006_day_001.json");
}

TEST_CASE("EMA matches backtrader golden reference", "[golden][indicator][ema]") {
    require_indicator_matches_fixture<stratforge::EMA>("tests/golden/ema_p30_2006_day_001.json");
}

TEST_CASE("RSI matches backtrader golden reference", "[golden][indicator][rsi]") {
    require_indicator_matches_fixture<stratforge::RSI>("tests/golden/rsi_p14_2006_day_001.json");
}

TEST_CASE("StdDev matches backtrader golden reference", "[golden][indicator][stddev]") {
    require_indicator_matches_fixture<stratforge::StdDev>("tests/golden/stddev_p20_2006_day_001.json");
}

TEST_CASE("ATR matches backtrader golden reference", "[golden][indicator][atr]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/atr_p14_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::ATR indicator(feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    REQUIRE(indicator.line().size() == fixture.values.size());

    for (std::size_t i = 0; i < fixture.values.size(); ++i) {
        const double expected = stratforge::test::parse_golden_double(fixture.values[i]);
        const double actual = indicator.line().data()[i];

        INFO("bar=" << i);
        if (std::isnan(expected)) {
            REQUIRE(std::isnan(actual));
        } else {
            REQUIRE_THAT(actual, WithinRel(expected, 0.000001));
        }
    }
}

TEST_CASE("BollingerBands matches backtrader golden reference", "[golden][indicator][bollinger]") {
    const auto fixture = stratforge::test::load_bollinger_golden_reference(
        source_path("tests/golden/bollinger_p20_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::BollingerBands indicator(feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    REQUIRE(indicator.mid().size() == fixture.mid.size());
    REQUIRE(indicator.top().size() == fixture.top.size());
    REQUIRE(indicator.bottom().size() == fixture.bottom.size());

    for (std::size_t i = 0; i < fixture.mid.size(); ++i) {
        const double expected_mid = stratforge::test::parse_golden_double(fixture.mid[i]);
        const double actual_mid = indicator.mid().data()[i];
        INFO("line=mid bar=" << i);
        if (std::isnan(expected_mid)) {
            REQUIRE(std::isnan(actual_mid));
        } else {
            REQUIRE_THAT(actual_mid, WithinRel(expected_mid, 0.000001));
        }
    }

    for (std::size_t i = 0; i < fixture.top.size(); ++i) {
        const double expected_top = stratforge::test::parse_golden_double(fixture.top[i]);
        const double actual_top = indicator.top().data()[i];
        INFO("line=top bar=" << i);
        if (std::isnan(expected_top)) {
            REQUIRE(std::isnan(actual_top));
        } else {
            REQUIRE_THAT(actual_top, WithinRel(expected_top, 0.000001));
        }
    }

    for (std::size_t i = 0; i < fixture.bottom.size(); ++i) {
        const double expected_bottom = stratforge::test::parse_golden_double(fixture.bottom[i]);
        const double actual_bottom = indicator.bottom().data()[i];
        INFO("line=bottom bar=" << i);
        if (std::isnan(expected_bottom)) {
            REQUIRE(std::isnan(actual_bottom));
        } else {
            REQUIRE_THAT(actual_bottom, WithinRel(expected_bottom, 0.000001));
        }
    }
}

TEST_CASE("MACD matches backtrader golden reference", "[golden][indicator][macd]") {
    const auto fixture = stratforge::test::load_macd_golden_reference(
        source_path("tests/golden/macd_12_26_9_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::MACD indicator(feed.close(), fixture.fast_period, fixture.slow_period, fixture.signal_period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    REQUIRE(indicator.macd().size() == fixture.macd.size());
    REQUIRE(indicator.signal().size() == fixture.signal.size());
    REQUIRE(indicator.histogram().size() == fixture.histogram.size());

    for (std::size_t i = 0; i < fixture.macd.size(); ++i) {
        const double expected = stratforge::test::parse_golden_double(fixture.macd[i]);
        const double actual = indicator.macd().data()[i];
        INFO("line=macd bar=" << i);
        if (std::isnan(expected)) {
            REQUIRE(std::isnan(actual));
        } else {
            REQUIRE_THAT(actual, WithinRel(expected, 0.000001));
        }
    }

    for (std::size_t i = 0; i < fixture.signal.size(); ++i) {
        const double expected = stratforge::test::parse_golden_double(fixture.signal[i]);
        const double actual = indicator.signal().data()[i];
        INFO("line=signal bar=" << i);
        if (std::isnan(expected)) {
            REQUIRE(std::isnan(actual));
        } else {
            REQUIRE_THAT(actual, WithinRel(expected, 0.000001));
        }
    }

    for (std::size_t i = 0; i < fixture.histogram.size(); ++i) {
        const double expected = stratforge::test::parse_golden_double(fixture.histogram[i]);
        const double actual = indicator.histogram().data()[i];
        INFO("line=histogram bar=" << i);
        if (std::isnan(expected)) {
            REQUIRE(std::isnan(actual));
        } else {
            REQUIRE_THAT(actual, WithinRel(expected, 0.000001));
        }
    }
}

TEST_CASE("SMMA matches backtrader golden reference", "[golden][indicator][smma]") {
    require_indicator_matches_fixture<stratforge::SMMA>("tests/golden/smma_p14_2006_day_001.json");
}

TEST_CASE("WMA matches backtrader golden reference", "[golden][indicator][wma]") {
    require_indicator_matches_fixture<stratforge::WMA>("tests/golden/wma_p14_2006_day_001.json");
}

TEST_CASE("CCI matches backtrader golden reference", "[golden][indicator][cci]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/cci_p20_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::CCI indicator(feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "cci");
}

TEST_CASE("Aroon matches backtrader golden reference", "[golden][indicator][aroon]") {
    const auto fixture = stratforge::test::load_aroon_golden_reference(
        source_path("tests/golden/aroon_p14_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::Aroon indicator(feed.high(), feed.low(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.up(), fixture.up, "aroon_up");
    require_line_matches_fixture(indicator.down(), fixture.down, "aroon_down");
    require_line_matches_fixture(indicator.line(), fixture.oscillator, "aroon_osc");
}

TEST_CASE("Stochastic matches backtrader golden reference", "[golden][indicator][stochastic]") {
    const auto fixture = stratforge::test::load_stochastic_golden_reference(
        source_path("tests/golden/stochastic_p14_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::Stochastic indicator(
        feed.high(), feed.low(), feed.close(), fixture.period, fixture.period_dfast, fixture.period_dslow);
    stratforge::StochasticFull full(
        feed.high(), feed.low(), feed.close(), fixture.period, fixture.period_dfast, fixture.period_dslow);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        full.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.percK(), fixture.percK, "stoch_percK");
    require_line_matches_fixture(indicator.percD(), fixture.percD, "stoch_percD");
    require_line_matches_fixture(full.percK(), fixture.fullK, "stoch_fullK");
    require_line_matches_fixture(full.percD(), fixture.fullD, "stoch_fullD");
    require_line_matches_fixture(full.percDSlow(), fixture.fullDSlow, "stoch_fullDSlow");
}

TEST_CASE("DirectionalMovement matches backtrader golden reference", "[golden][indicator][directionalmovement]") {
    const auto fixture = stratforge::test::load_directionalmovement_golden_reference(
        source_path("tests/golden/directionalmovement_p14_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::DirectionalMovement indicator(feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.adx(), fixture.adx, "adx");
    require_line_matches_fixture(indicator.adxr(), fixture.adxr, "adxr");
    require_line_matches_fixture(indicator.plus_di(), fixture.plus_di, "plus_di");
    require_line_matches_fixture(indicator.minus_di(), fixture.minus_di, "minus_di");
}

TEST_CASE("CrossOver matches backtrader golden reference", "[golden][indicator][crossover]") {
    const auto fixture = stratforge::test::load_crossover_golden_reference(
        source_path("tests/golden/crossover_sma10_sma30_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::SMA fast(feed.close(), fixture.fast_period);
    stratforge::SMA slow(feed.close(), fixture.slow_period);
    stratforge::CrossOver cross(fast.line(), slow.line());

    for (std::size_t i = 0; i < fixture.bars; ++i) {
        fast.next();
        slow.next();
        cross.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(cross.line(), fixture.values, "crossover");
}

TEST_CASE("HMA matches backtrader golden reference", "[golden][indicator][hma]") {
    require_indicator_matches_fixture<stratforge::HMA>("tests/golden/hma_p30_2006_day_001.json");
}

TEST_CASE("ROC matches backtrader golden reference", "[golden][indicator][roc]") {
    require_indicator_matches_fixture<stratforge::ROC>("tests/golden/roc_p12_2006_day_001.json");
}

TEST_CASE("MeanDeviation matches backtrader golden reference", "[golden][indicator][meandeviation]") {
    require_indicator_matches_fixture<stratforge::MeanDeviation>("tests/golden/meandeviation_p20_2006_day_001.json");
}

TEST_CASE("MACDHisto matches backtrader golden reference", "[golden][indicator][macdhisto]") {
    const auto fixture = stratforge::test::load_macd_golden_reference(
        source_path("tests/golden/macdhisto_12_26_9_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::MACDHisto indicator(feed.close(), fixture.fast_period, fixture.slow_period, fixture.signal_period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.macd(), fixture.macd, "macdhisto_macd");
    require_line_matches_fixture(indicator.signal(), fixture.signal, "macdhisto_signal");
    require_line_matches_fixture(indicator.histo(), fixture.histogram, "macdhisto_histo");
}

TEST_CASE("ADXR and DI match backtrader golden reference", "[golden][indicator][adxr]") {
    const auto fixture = stratforge::test::load_adxr_golden_reference(
        source_path("tests/golden/adxr_p14_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::ADXR adxr(feed.high(), feed.low(), feed.close(), fixture.period);
    stratforge::DirectionalIndicator di(feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        adxr.next();
        di.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(adxr.line(), fixture.adxr, "adxr");
    require_line_matches_fixture(di.plus_di(), fixture.plus_di, "di_plus");
    require_line_matches_fixture(di.minus_di(), fixture.minus_di, "di_minus");
}

TEST_CASE("HeikinAshi matches backtrader golden reference", "[golden][indicator][heikinashi]") {
    const auto fixture = stratforge::test::load_heikinashi_golden_reference(
        source_path("tests/golden/heikinashi_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::HeikinAshi indicator(feed.open(), feed.high(), feed.low(), feed.close());
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.ha_open(), fixture.open, "ha_open");
    require_line_matches_fixture(indicator.ha_high(), fixture.high, "ha_high");
    require_line_matches_fixture(indicator.ha_low(), fixture.low, "ha_low");
    require_line_matches_fixture(indicator.ha_close(), fixture.close, "ha_close");
}

TEST_CASE("ParabolicSAR matches backtrader golden reference", "[golden][indicator][psar]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/psar_p2_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::ParabolicSAR indicator(feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "psar");
}

TEST_CASE("DV2 matches backtrader golden reference", "[golden][indicator][dv2]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/dv2_p252_ma2_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::DV2 indicator(feed.high(), feed.low(), feed.close(), fixture.period, fixture.maperiod);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "dv2");
}

TEST_CASE("TSI matches backtrader golden reference", "[golden][indicator][tsi]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/tsi_25_13_1_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::TSI indicator(feed.close(), fixture.period1, fixture.period2, fixture.pchange);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "tsi");
}

TEST_CASE("RMI matches backtrader golden reference", "[golden][indicator][rmi]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/rmi_p20_l5_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::RMI indicator(feed.close(), fixture.period, fixture.lookback);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "rmi");
}

TEST_CASE("WilliamsAD matches backtrader golden reference", "[golden][indicator][williamsad]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/williamsad_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::WilliamsAD indicator(feed.high(), feed.low(), feed.close());
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "williamsad");
}

TEST_CASE("WillR matches backtrader golden reference", "[golden][indicator][willr]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/willr_p14_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::WillR indicator(feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "willr");
}

TEST_CASE("UltimateOscillator matches backtrader golden reference", "[golden][indicator][ultimateoscillator]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/ultimateoscillator_7_14_28_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::UltimateOscillator indicator(feed.high(), feed.low(), feed.close(), 7, 14, 28);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, "ultimateoscillator");
}

TEST_CASE("Vortex matches backtrader golden reference", "[golden][indicator][vortex]") {
    const auto fixture = stratforge::test::load_vortex_golden_reference(
        source_path("tests/golden/vortex_p14_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::Vortex indicator(feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.vi_plus(), fixture.vi_plus, "vortex_vi_plus");
    require_line_matches_fixture(indicator.vi_minus(), fixture.vi_minus, "vortex_vi_minus");
}
