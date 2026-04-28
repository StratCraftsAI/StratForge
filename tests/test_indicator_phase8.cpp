#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/core/line.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/indicators/hadelta.hpp>
#include <stratforge/indicators/ichimoku.hpp>
#include <stratforge/indicators/kst.hpp>
#include <stratforge/indicators/laguerre.hpp>
#include <stratforge/indicators/ols.hpp>
#include <stratforge/indicators/pivotpoint.hpp>
#include <stratforge/indicators/hurst.hpp>

#include "golden_reference.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using Catch::Matchers::WithinRel;
using Catch::Approx;

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

stratforge::Line<double> make_line(const std::vector<double>& values) {
    stratforge::Line<double> line;
    for (double value : values) {
        line.forward(value);
    }
    line.home();
    return line;
}

template <typename IndicatorType>
void run_single_line_indicator(stratforge::Line<double>& source, IndicatorType& indicator) {
    for (std::size_t i = 0; i < source.size(); ++i) {
        indicator.next();
        if (i + 1 < source.size()) {
            source.advance();
        }
    }
}

struct IchimokuGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t tenkan = 0;
    std::size_t kijun = 0;
    std::size_t senkou = 0;
    std::size_t senkou_lead = 0;
    std::size_t chikou = 0;
    std::vector<std::string> tenkan_sen;
    std::vector<std::string> kijun_sen;
    std::vector<std::string> senkou_span_a;
    std::vector<std::string> senkou_span_b;
    std::vector<std::string> chikou_span;
};

struct KstGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t rp1 = 0;
    std::size_t rp2 = 0;
    std::size_t rp3 = 0;
    std::size_t rp4 = 0;
    std::size_t rma1 = 0;
    std::size_t rma2 = 0;
    std::size_t rma3 = 0;
    std::size_t rma4 = 0;
    std::size_t rsignal = 0;
    std::vector<std::string> kst;
    std::vector<std::string> signal;
};

struct PivotPointGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::vector<std::string> p;
    std::vector<std::string> s1;
    std::vector<std::string> s2;
    std::vector<std::string> s3;
    std::vector<std::string> r1;
    std::vector<std::string> r2;
    std::vector<std::string> r3;
};

struct HaDeltaGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t period = 0;
    std::vector<std::string> ha_delta;
    std::vector<std::string> smoothed;
};

struct OlsSlopeInterceptGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t period = 0;
    std::vector<std::string> slope;
    std::vector<std::string> intercept;
};

struct OlsTransformationGoldenReference {
    std::string data_file;
    std::size_t bars = 0;
    std::size_t period = 0;
    std::vector<std::string> spread;
    std::vector<std::string> spread_mean;
    std::vector<std::string> spread_std;
    std::vector<std::string> zscore;
};

IchimokuGoldenReference load_ichimoku_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    REQUIRE(input.is_open());

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    stratforge::test::JsonCursor cursor(json);
    IchimokuGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "tenkan_sen") {
            fixture.tenkan_sen = stratforge::test::parse_string_array(cursor);
        } else if (key == "kijun_sen") {
            fixture.kijun_sen = stratforge::test::parse_string_array(cursor);
        } else if (key == "senkou_span_a") {
            fixture.senkou_span_a = stratforge::test::parse_string_array(cursor);
        } else if (key == "senkou_span_b") {
            fixture.senkou_span_b = stratforge::test::parse_string_array(cursor);
        } else if (key == "chikou_span") {
            fixture.chikou_span = stratforge::test::parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "tenkan") {
                    fixture.tenkan = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "kijun") {
                    fixture.kijun = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "senkou") {
                    fixture.senkou = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "senkou_lead") {
                    fixture.senkou_lead = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "chikou") {
                    fixture.chikou = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    stratforge::test::skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            stratforge::test::skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

KstGoldenReference load_kst_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    REQUIRE(input.is_open());

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    stratforge::test::JsonCursor cursor(json);
    KstGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "kst") {
            fixture.kst = stratforge::test::parse_string_array(cursor);
        } else if (key == "signal") {
            fixture.signal = stratforge::test::parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "rp1") {
                    fixture.rp1 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rp2") {
                    fixture.rp2 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rp3") {
                    fixture.rp3 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rp4") {
                    fixture.rp4 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rma1") {
                    fixture.rma1 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rma2") {
                    fixture.rma2 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rma3") {
                    fixture.rma3 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rma4") {
                    fixture.rma4 = static_cast<std::size_t>(cursor.parse_number());
                } else if (param_key == "rsignal") {
                    fixture.rsignal = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    stratforge::test::skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            stratforge::test::skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

PivotPointGoldenReference load_pivotpoint_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    REQUIRE(input.is_open());

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    stratforge::test::JsonCursor cursor(json);
    PivotPointGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "p") {
            fixture.p = stratforge::test::parse_string_array(cursor);
        } else if (key == "s1") {
            fixture.s1 = stratforge::test::parse_string_array(cursor);
        } else if (key == "s2") {
            fixture.s2 = stratforge::test::parse_string_array(cursor);
        } else if (key == "s3") {
            fixture.s3 = stratforge::test::parse_string_array(cursor);
        } else if (key == "r1") {
            fixture.r1 = stratforge::test::parse_string_array(cursor);
        } else if (key == "r2") {
            fixture.r2 = stratforge::test::parse_string_array(cursor);
        } else if (key == "r3") {
            fixture.r3 = stratforge::test::parse_string_array(cursor);
        } else {
            stratforge::test::skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

HaDeltaGoldenReference load_hadelta_golden_reference(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    REQUIRE(input.is_open());

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    stratforge::test::JsonCursor cursor(json);
    HaDeltaGoldenReference fixture;

    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');

        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "haDelta") {
            fixture.ha_delta = stratforge::test::parse_string_array(cursor);
        } else if (key == "smoothed") {
            fixture.smoothed = stratforge::test::parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    stratforge::test::skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            stratforge::test::skip_json_value(cursor);
        }

        static_cast<void>(cursor.consume(','));
    }

    return fixture;
}

template <typename ReferenceType>
ReferenceType load_ols_reference(const std::string& fixture_path);

template <>
OlsSlopeInterceptGoldenReference load_ols_reference<OlsSlopeInterceptGoldenReference>(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    REQUIRE(input.is_open());
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    stratforge::test::JsonCursor cursor(json);
    OlsSlopeInterceptGoldenReference fixture;
    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');
        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "slope") {
            fixture.slope = stratforge::test::parse_string_array(cursor);
        } else if (key == "intercept") {
            fixture.intercept = stratforge::test::parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    stratforge::test::skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            stratforge::test::skip_json_value(cursor);
        }
        static_cast<void>(cursor.consume(','));
    }
    return fixture;
}

template <>
OlsTransformationGoldenReference load_ols_reference<OlsTransformationGoldenReference>(const std::string& fixture_path) {
    std::ifstream input(fixture_path);
    REQUIRE(input.is_open());
    const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    stratforge::test::JsonCursor cursor(json);
    OlsTransformationGoldenReference fixture;
    cursor.expect('{');
    while (!cursor.consume('}')) {
        const auto key = cursor.parse_string();
        cursor.expect(':');
        if (key == "data_file") {
            fixture.data_file = cursor.parse_string();
        } else if (key == "bars") {
            fixture.bars = static_cast<std::size_t>(cursor.parse_number());
        } else if (key == "spread") {
            fixture.spread = stratforge::test::parse_string_array(cursor);
        } else if (key == "spread_mean") {
            fixture.spread_mean = stratforge::test::parse_string_array(cursor);
        } else if (key == "spread_std") {
            fixture.spread_std = stratforge::test::parse_string_array(cursor);
        } else if (key == "zscore") {
            fixture.zscore = stratforge::test::parse_string_array(cursor);
        } else if (key == "params") {
            cursor.expect('{');
            while (!cursor.consume('}')) {
                const auto param_key = cursor.parse_string();
                cursor.expect(':');
                if (param_key == "period") {
                    fixture.period = static_cast<std::size_t>(cursor.parse_number());
                } else {
                    stratforge::test::skip_json_value(cursor);
                }
                static_cast<void>(cursor.consume(','));
            }
        } else {
            stratforge::test::skip_json_value(cursor);
        }
        static_cast<void>(cursor.consume(','));
    }
    return fixture;
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

} // namespace

TEST_CASE("Ichimoku applies chikou backshift and senkou forward shift", "[indicator][ichimoku]") {
    auto high = make_line({10.0, 11.0, 12.0, 13.0, 14.0, 15.0});
    auto low = make_line({8.0, 9.0, 10.0, 11.0, 12.0, 13.0});
    auto close = make_line({9.0, 10.0, 11.0, 12.0, 13.0, 14.0});

    stratforge::Ichimoku ichimoku(high, low, close, 2, 3, 4, 2, 2);
    for (std::size_t i = 0; i < close.size(); ++i) {
        ichimoku.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(ichimoku.tenkan_sen().data()[0]));
    REQUIRE(ichimoku.tenkan_sen().data()[1] == Approx(9.5));
    REQUIRE(ichimoku.kijun_sen().data()[2] == Approx(10.0));
    REQUIRE(ichimoku.senkou_span_a().data()[4] == Approx(10.25));
    REQUIRE(ichimoku.chikou_span().data()[0] == Approx(11.0));
    REQUIRE(std::isnan(ichimoku.chikou_span().data()[5]));
}

TEST_CASE("KnowSureThing aggregates weighted smoothed ROC components", "[indicator][kst]") {
    auto source = make_line({10.0, 11.0, 12.0, 13.0, 14.0, 16.0, 18.0, 20.0});

    stratforge::KnowSureThing kst(source, 1, 2, 3, 4, 2, 2, 2, 2, 2);
    run_single_line_indicator(source, kst);

    REQUIRE(std::isnan(kst.kst().data()[0]));
    REQUIRE(std::isnan(kst.signal().data()[4]));
    REQUIRE(kst.kst().data()[5] == Approx(312.5507825508));
    REQUIRE(kst.signal().data()[6] == Approx(338.0967365967));
}

TEST_CASE("Pivot point variants match their textbook formulas", "[indicator][pivotpoint]") {
    auto open = make_line({10.0, 12.0});
    auto high = make_line({14.0, 15.0});
    auto low = make_line({8.0, 10.0});
    auto close = make_line({12.0, 11.0});

    stratforge::PivotPoint pivot(open, high, low, close);
    stratforge::FibonacciPivotPoint fib(open, high, low, close);
    stratforge::DemarkPivotPoint demark(open, high, low, close);

    for (std::size_t i = 0; i < close.size(); ++i) {
        pivot.next();
        fib.next();
        demark.next();
        if (i + 1 < close.size()) {
            open.advance();
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(pivot.p().data()[0] == Approx(11.3333333333));
    REQUIRE(pivot.s1().data()[0] == Approx(8.6666666667));
    REQUIRE(fib.r2().data()[1] == Approx(15.09));
    REQUIRE(demark.p().data()[0] == Approx(0.25));
    REQUIRE(demark.r1().data()[1] == Approx(-10.5));
}

TEST_CASE("Laguerre indicators track recursive filter state", "[indicator][laguerre]") {
    // LaguerreRSI default period=6, need at least 6 bars for first non-NaN output
    auto source = make_line({10.0, 11.0, 13.0, 12.0, 15.0, 14.0, 16.0});

    stratforge::LaguerreRSI lrsi(source);
    stratforge::LaguerreFilter lagf(source);
    run_single_line_indicator(source, lrsi);

    source.home();
    run_single_line_indicator(source, lagf);

    REQUIRE(std::isnan(lrsi.line().data()[0]));
    REQUIRE(std::isnan(lrsi.line().data()[4]));
    REQUIRE(!std::isnan(lrsi.line().data()[5]));
    REQUIRE(lagf.line().data()[0] == Approx(0.3125));
    REQUIRE(lagf.line().data()[4] == Approx(4.74609375));
    REQUIRE(lagf.line().data()[6] != 0.0);  // verify 7th bar produces output
}

TEST_CASE("haDelta computes Heikin-Ashi body and SMA smoothing", "[indicator][hadelta]") {
    auto open = make_line({10.0, 11.0, 12.0, 13.0});
    auto high = make_line({12.0, 13.0, 14.0, 15.0});
    auto low = make_line({9.0, 10.0, 11.0, 12.0});
    auto close = make_line({11.0, 12.0, 13.0, 14.0});

    stratforge::haDelta had(open, high, low, close);
    for (std::size_t i = 0; i < close.size(); ++i) {
        had.next();
        if (i + 1 < close.size()) {
            open.advance();
            high.advance();
            low.advance();
            close.advance();
        }
    }

    REQUIRE(std::isnan(had.ha_delta().data()[0]));
    REQUIRE(had.ha_delta().data()[1] == Approx(1.0));
    REQUIRE(std::isnan(had.smoothed().data()[1]));
    REQUIRE(had.smoothed().data()[3] == Approx(1.4166666667));
}

TEST_CASE("OLS regression indicators produce stable slope and zscore outputs", "[indicator][ols]") {
    auto x = make_line({1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
    auto y = make_line({3.0, 5.0, 7.0, 9.0, 11.0, 13.0});

    stratforge::OLS_Slope_InterceptN ols(y, x, 3);
    stratforge::OLS_TransformationN trans(y, x, 3);
    for (std::size_t i = 0; i < x.size(); ++i) {
        ols.next();
        trans.next();
        if (i + 1 < x.size()) {
            x.advance();
            y.advance();
        }
    }

    REQUIRE(std::isnan(ols.slope().data()[1]));
    REQUIRE(ols.slope().data()[2] == Approx(2.0));
    REQUIRE(ols.intercept().data()[5] == Approx(1.0));
    REQUIRE(std::isnan(trans.spread_mean().data()[3]));
    REQUIRE(std::fabs(trans.spread().data()[5]) < 1e-12);

    stratforge::OLS_BetaN beta(y, x, 3);
    x.home();
    y.home();
    for (std::size_t i = 0; i < x.size(); ++i) {
        beta.next();
        if (i + 1 < x.size()) {
            x.advance();
            y.advance();
        }
    }
    REQUIRE(beta.line().data()[2] == Approx(2.0));
}

TEST_CASE("Hurst exponent emits NaN warmup then finite values", "[indicator][hurst]") {
    std::vector<double> values;
    values.reserve(60);
    for (int i = 0; i < 60; ++i) {
        values.push_back(100.0 + std::sin(static_cast<double>(i) * 0.2) + (0.5 * static_cast<double>(i)));
    }

    auto source = make_line(values);
    stratforge::HurstExponent hurst(source, 40);
    run_single_line_indicator(source, hurst);

    REQUIRE(std::isnan(hurst.line().data()[38]));
    REQUIRE(!std::isnan(hurst.line().data()[39]));
    REQUIRE(std::isfinite(hurst.line().data().back()));
}

TEST_CASE("Ichimoku matches backtrader golden reference", "[golden][indicator][ichimoku]") {
    const auto fixture = load_ichimoku_golden_reference(
        source_path("tests/golden/ichimoku_9_26_52_26_26_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());

    stratforge::Ichimoku indicator(feed.high(),
                               feed.low(),
                               feed.close(),
                               fixture.tenkan,
                               fixture.kijun,
                               fixture.senkou,
                               fixture.senkou_lead,
                               fixture.chikou);

    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.tenkan_sen(), fixture.tenkan_sen, "tenkan_sen");
    require_line_matches_fixture(indicator.kijun_sen(), fixture.kijun_sen, "kijun_sen");
    require_line_matches_fixture(indicator.senkou_span_a(), fixture.senkou_span_a, "senkou_span_a");
    require_line_matches_fixture(indicator.senkou_span_b(), fixture.senkou_span_b, "senkou_span_b");
    require_line_matches_fixture(indicator.chikou_span(), fixture.chikou_span, "chikou_span");
}

TEST_CASE("KST matches backtrader golden reference", "[golden][indicator][kst]") {
    const auto fixture = load_kst_golden_reference(
        source_path("tests/golden/kst_10_15_20_30_10_10_10_10_9_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());

    stratforge::KnowSureThing indicator(feed.close(),
                                    fixture.rp1,
                                    fixture.rp2,
                                    fixture.rp3,
                                    fixture.rp4,
                                    fixture.rma1,
                                    fixture.rma2,
                                    fixture.rma3,
                                    fixture.rma4,
                                    fixture.rsignal);

    for (std::size_t i = 0; i < fixture.bars; ++i) {
        indicator.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(indicator.kst(), fixture.kst, "kst");
    require_line_matches_fixture(indicator.signal(), fixture.signal, "signal");
}

TEST_CASE("PivotPoint variants match backtrader golden reference", "[golden][indicator][pivotpoint]") {
    const auto pivot_fixture = load_pivotpoint_golden_reference(
        source_path("tests/golden/pivotpoint_2006_day_001.json"));
    const auto fib_fixture = load_pivotpoint_golden_reference(
        source_path("tests/golden/fibonaccipivotpoint_2006_day_001.json"));
    const auto demark_fixture = load_pivotpoint_golden_reference(
        source_path("tests/golden/demarkpivotpoint_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(pivot_fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());

    stratforge::PivotPoint pivot(feed.open(), feed.high(), feed.low(), feed.close());
    stratforge::FibonacciPivotPoint fib(feed.open(), feed.high(), feed.low(), feed.close());
    stratforge::DemarkPivotPoint demark(feed.open(), feed.high(), feed.low(), feed.close());

    for (std::size_t i = 0; i < pivot_fixture.bars; ++i) {
        pivot.next();
        fib.next();
        demark.next();
        if (i + 1 < pivot_fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(pivot.p(), pivot_fixture.p, "pivot.p");
    require_line_matches_fixture(pivot.s1(), pivot_fixture.s1, "pivot.s1");
    require_line_matches_fixture(pivot.s2(), pivot_fixture.s2, "pivot.s2");
    require_line_matches_fixture(pivot.r1(), pivot_fixture.r1, "pivot.r1");
    require_line_matches_fixture(pivot.r2(), pivot_fixture.r2, "pivot.r2");

    require_line_matches_fixture(fib.p(), fib_fixture.p, "fib.p");
    require_line_matches_fixture(fib.s1(), fib_fixture.s1, "fib.s1");
    require_line_matches_fixture(fib.s2(), fib_fixture.s2, "fib.s2");
    require_line_matches_fixture(fib.s3(), fib_fixture.s3, "fib.s3");
    require_line_matches_fixture(fib.r1(), fib_fixture.r1, "fib.r1");
    require_line_matches_fixture(fib.r2(), fib_fixture.r2, "fib.r2");
    require_line_matches_fixture(fib.r3(), fib_fixture.r3, "fib.r3");

    require_line_matches_fixture(demark.p(), demark_fixture.p, "demark.p");
    require_line_matches_fixture(demark.s1(), demark_fixture.s1, "demark.s1");
    require_line_matches_fixture(demark.r1(), demark_fixture.r1, "demark.r1");
}

TEST_CASE("LaguerreRSI and LaguerreFilter match backtrader golden reference", "[golden][indicator][laguerre]") {
    const auto lrsi_fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/lrsi_g0_5_2006_day_001.json"));
    const auto lagf_fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/lagf_g0_5_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(lrsi_fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());

    stratforge::LaguerreRSI lrsi(feed.close());
    stratforge::LaguerreFilter lagf(feed.close());

    for (std::size_t i = 0; i < lrsi_fixture.bars; ++i) {
        lrsi.next();
        lagf.next();
        if (i + 1 < lrsi_fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(lrsi.line(), lrsi_fixture.values, "lrsi");
    require_line_matches_fixture(lagf.line(), lagf_fixture.values, "lagf");
}

TEST_CASE("haDelta matches backtrader golden reference", "[golden][indicator][hadelta]") {
    const auto fixture = load_hadelta_golden_reference(
        source_path("tests/golden/hadelta_p3_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());

    stratforge::haDelta had(feed.open(), feed.high(), feed.low(), feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        had.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(had.ha_delta(), fixture.ha_delta, "haDelta");
    require_line_matches_fixture(had.smoothed(), fixture.smoothed, "smoothed");
}

TEST_CASE("OLS regression indicators match backtrader golden reference", "[golden][indicator][ols]") {
    const auto ols_fixture = load_ols_reference<OlsSlopeInterceptGoldenReference>(
        source_path("tests/golden/ols_slope_intercept_p10_2006_day_001.json"));
    const auto trans_fixture = load_ols_reference<OlsTransformationGoldenReference>(
        source_path("tests/golden/ols_transformation_p10_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(ols_fixture.data_file),
        .columns = {},
    });
    REQUIRE(feed.load());

    stratforge::OLS_Slope_InterceptN ols(feed.close(), feed.open(), ols_fixture.period);
    stratforge::OLS_TransformationN trans(feed.close(), feed.open(), trans_fixture.period);

    for (std::size_t i = 0; i < ols_fixture.bars; ++i) {
        ols.next();
        trans.next();
        if (i + 1 < ols_fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(ols.slope(), ols_fixture.slope, "ols.slope");
    require_line_matches_fixture(ols.intercept(), ols_fixture.intercept, "ols.intercept");
    require_line_matches_fixture(trans.spread(), trans_fixture.spread, "trans.spread");
    require_line_matches_fixture(trans.spread_mean(), trans_fixture.spread_mean, "trans.spread_mean");
    require_line_matches_fixture(trans.spread_std(), trans_fixture.spread_std, "trans.spread_std");
    require_line_matches_fixture(trans.zscore(), trans_fixture.zscore, "trans.zscore");
}

TEST_CASE("Hurst exponent matches backtrader golden reference", "[golden][indicator][hurst]") {
    const auto fixture = stratforge::test::load_indicator_golden_reference(
        source_path("tests/golden/hurst_p40_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });
    REQUIRE(feed.load());

    stratforge::HurstExponent hurst(feed.close(), fixture.period);
    for (std::size_t i = 0; i < fixture.bars; ++i) {
        hurst.next();
        if (i + 1 < fixture.bars) {
            feed.advance();
        }
    }

    require_line_matches_fixture(hurst.line(), fixture.values, "hurst");
}
