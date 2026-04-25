#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/data/csv_data.hpp>
#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/cci.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/momentum.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/indicators/hilbert.hpp>
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

void require_line_matches_fixture(const stratforge::Line<double>& line,
                                  const std::vector<std::string>& expected_values,
                                  const std::string& label) {
    REQUIRE(line.size() == expected_values.size());
    for (std::size_t i = 0; i < expected_values.size(); ++i) {
        const double expected = stratforge::test::parse_golden_double(expected_values[i]);
        const double actual = line.data()[i];
        INFO("fixture=" << label << " bar=" << i);
        if (std::isnan(expected)) {
            REQUIRE(std::isnan(actual));
        } else {
            REQUIRE_THAT(actual, WithinRel(expected, 0.000001));
        }
    }
}

template <typename IndicatorType>
void require_indicator_matches_talib_fixture(const std::string& fixture_relpath) {
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

    require_line_matches_fixture(indicator.line(), fixture.values, fixture_relpath);
}

void require_atr_matches_talib_fixture(const std::string& fixture_relpath) {
    const auto fixture = stratforge::test::load_indicator_golden_reference(source_path(fixture_relpath));

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

    require_line_matches_fixture(indicator.line(), fixture.values, fixture_relpath);
}

void require_ht_trendline_matches_talib_fixture(const std::string& fixture_relpath) {
    const auto fixture = stratforge::test::load_indicator_golden_reference(source_path(fixture_relpath));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::HT_Trendline indicator(feed.close());
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.line(), fixture.values, fixture_relpath);
}

void require_mama_matches_talib_fixture(const std::string& fixture_relpath) {
    const auto fixture = stratforge::test::load_mama_golden_reference(source_path(fixture_relpath));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.bars);

    stratforge::MAMA indicator(feed.close(), fixture.fast_limit, fixture.slow_limit);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.mama(), fixture.mama, fixture_relpath + ":mama");
    require_line_matches_fixture(indicator.fama(), fixture.fama, fixture_relpath + ":fama");
}

} // namespace

TEST_CASE("TA-Lib cross-validation matches 2006 baseline dataset",
          "[phase7][talib][dataset_2006]") {
    require_indicator_matches_talib_fixture<stratforge::SMA>(
        "tools/golden_extract/output/talib/sma_p30_2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::EMA>(
        "tools/golden_extract/output/talib/ema_p30_2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::RSI>(
        "tools/golden_extract/output/talib/rsi_p14_2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::StdDev>(
        "tools/golden_extract/output/talib/stddev_p20_2006-day-001.json");
    require_atr_matches_talib_fixture(
        "tools/golden_extract/output/talib/atr_p14_2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::WMA>(
        "tools/golden_extract/output/talib/wma_p14_2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::ROC100>(
        "tools/golden_extract/output/talib/roc_p12_2006-day-001.json");
    require_ht_trendline_matches_talib_fixture(
        "tools/golden_extract/output/talib/ht_trendline_2006-day-001.json");
    require_mama_matches_talib_fixture(
        "tools/golden_extract/output/talib/mama_fast05_slow005_2006-day-001.json");
}

TEST_CASE("TA-Lib cross-validation matches multi-year dataset",
          "[phase7][talib][dataset_multi_year]") {
    require_indicator_matches_talib_fixture<stratforge::SMA>(
        "tools/golden_extract/output/talib/sma_p30_2005-2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::EMA>(
        "tools/golden_extract/output/talib/ema_p30_2005-2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::RSI>(
        "tools/golden_extract/output/talib/rsi_p14_2005-2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::StdDev>(
        "tools/golden_extract/output/talib/stddev_p20_2005-2006-day-001.json");
    require_atr_matches_talib_fixture(
        "tools/golden_extract/output/talib/atr_p14_2005-2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::WMA>(
        "tools/golden_extract/output/talib/wma_p14_2005-2006-day-001.json");
    require_indicator_matches_talib_fixture<stratforge::ROC100>(
        "tools/golden_extract/output/talib/roc_p12_2005-2006-day-001.json");
}
