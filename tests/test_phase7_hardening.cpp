#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/analytics/extended_metrics.hpp>
#include <stratforge/broker/broker.hpp>
#include <stratforge/core/line.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/resampler.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/lowest.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/stddev.hpp>

#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using Catch::Matchers::WithinRel;
using stratforge::test::make_line;
using stratforge::test::tmp_path;

namespace {

// Phase7-specific CSV builder: writes OHLCV header automatically.
// Distinct from the generic write_csv() in test_helpers.hpp.
std::string create_test_csv(const std::string& filename, const std::vector<std::string>& rows) {
    const std::string path = tmp_path(filename);
    std::ofstream file(path);
    file << "Date,Open,High,Low,Close,Volume,OpenInterest\n";
    for (const auto& row : rows) {
        file << row << '\n';
    }
    return path;
}

} // namespace

TEST_CASE("Phase 7 edge-case hardening covers date filtering and zero-length analytics",
          "[phase7][hardening][data]") {
    const auto path = create_test_csv(
        "phase7_filter.csv",
        {
            "2024-01-02,100,101,99,100,10,0",
            "2024-01-03,101,102,100,101,11,0",
            "2024-01-04,102,103,101,102,12,0",
        });

    SECTION("CsvData respects fromdate/todate filters") {
        using namespace std::chrono;
        const auto from = system_clock::from_time_t(1704240000); // 2024-01-03 00:00:00 UTC
        const auto to = system_clock::from_time_t(1704326400);   // 2024-01-04 00:00:00 UTC

        stratforge::CsvData feed(stratforge::CsvData::Params{
            .filename = path,
            .columns = {},
            .fromdate = from,
            .todate = to,
        });

        REQUIRE(feed.load());
        REQUIRE(feed.size() == 2);
        REQUIRE_THAT(feed.close()[0], WithinRel(101.0, 1e-12));
        feed.advance();
        REQUIRE_THAT(feed.close()[0], WithinRel(102.0, 1e-12));
    }

    SECTION("Header-only CSV loads as empty") {
        const auto empty_path = create_test_csv("phase7_empty.csv", {});
        stratforge::CsvData feed(stratforge::CsvData::Params{
            .filename = empty_path,
            .columns = {},
        });

        REQUIRE_FALSE(feed.load());
        REQUIRE(feed.size() == 0);
    }

    SECTION("Extended metrics return zeros on empty inputs") {
        const auto metrics = stratforge::ExtendedMetricsCalculator::compute(
            std::vector<double>{},
            std::vector<double>{});

        REQUIRE(metrics.total_trades == 0);
        REQUIRE(metrics.volatility == 0.0);
        REQUIRE(metrics.max_drawdown == 0.0);
        REQUIRE(metrics.value_at_risk95 == 0.0);
    }
}

TEST_CASE("Phase 7 hardening clamps zero periods to single-bar semantics",
          "[phase7][hardening][indicator]") {
    auto line = make_line({10.0, 20.0, 30.0});

    SECTION("SMA period zero behaves like period one") {
        stratforge::SMA sma(line, 0);
        for (std::size_t i = 0; i < line.size(); ++i) {
            sma.next();
            if (i + 1 < line.size()) {
                line.advance();
            }
        }

        REQUIRE(sma.minimum_period() == 1);
        REQUIRE(sma.line().size() == 3);
        REQUIRE_THAT(sma.line().data()[0], WithinRel(10.0, 1e-12));
        REQUIRE_THAT(sma.line().data()[1], WithinRel(20.0, 1e-12));
        REQUIRE_THAT(sma.line().data()[2], WithinRel(30.0, 1e-12));
    }

    SECTION("EMA period zero behaves like period one") {
        line.home();
        stratforge::EMA ema(line, 0);
        for (std::size_t i = 0; i < line.size(); ++i) {
            ema.next();
            if (i + 1 < line.size()) {
                line.advance();
            }
        }

        REQUIRE(ema.minimum_period() == 1);
        REQUIRE(ema.line().size() == 3);
        REQUIRE_THAT(ema.line().data()[0], WithinRel(10.0, 1e-12));
        REQUIRE_THAT(ema.line().data()[1], WithinRel(20.0, 1e-12));
        REQUIRE_THAT(ema.line().data()[2], WithinRel(30.0, 1e-12));
    }

    SECTION("StdDev period zero behaves like period one") {
        line.home();
        stratforge::StdDev stddev(line, 0);
        for (std::size_t i = 0; i < line.size(); ++i) {
            stddev.next();
            if (i + 1 < line.size()) {
                line.advance();
            }
        }

        REQUIRE(stddev.minimum_period() == 1);
        REQUIRE(stddev.line().size() == 3);
        REQUIRE_THAT(stddev.line().data()[0], WithinRel(0.0, 1e-12));
        REQUIRE_THAT(stddev.line().data()[1], WithinRel(0.0, 1e-12));
        REQUIRE_THAT(stddev.line().data()[2], WithinRel(0.0, 1e-12));
    }

    SECTION("Highest period zero behaves like period one") {
        line.home();
        stratforge::Highest highest(line, 0);
        for (std::size_t i = 0; i < line.size(); ++i) {
            highest.next();
            if (i + 1 < line.size()) {
                line.advance();
            }
        }

        REQUIRE(highest.minimum_period() == 1);
        REQUIRE(highest.line().size() == 3);
        REQUIRE_THAT(highest.line().data()[0], WithinRel(10.0, 1e-12));
        REQUIRE_THAT(highest.line().data()[1], WithinRel(20.0, 1e-12));
        REQUIRE_THAT(highest.line().data()[2], WithinRel(30.0, 1e-12));
    }

    SECTION("Lowest period zero behaves like period one") {
        line.home();
        stratforge::Lowest lowest(line, 0);
        for (std::size_t i = 0; i < line.size(); ++i) {
            lowest.next();
            if (i + 1 < line.size()) {
                line.advance();
            }
        }

        REQUIRE(lowest.minimum_period() == 1);
        REQUIRE(lowest.line().size() == 3);
        REQUIRE_THAT(lowest.line().data()[0], WithinRel(10.0, 1e-12));
        REQUIRE_THAT(lowest.line().data()[1], WithinRel(20.0, 1e-12));
        REQUIRE_THAT(lowest.line().data()[2], WithinRel(30.0, 1e-12));
    }

    SECTION("BollingerBands period zero behaves like period one") {
        line.home();
        stratforge::BollingerBands bb(line, 0, 2.0);
        for (std::size_t i = 0; i < line.size(); ++i) {
            bb.next();
            if (i + 1 < line.size()) {
                line.advance();
            }
        }

        REQUIRE(bb.minimum_period() == 1);
        REQUIRE(bb.mid().size() == 3);
        // period=1 → mean == bar value, stddev == 0
        REQUIRE_THAT(bb.mid().data()[0], WithinRel(10.0, 1e-12));
        REQUIRE_THAT(bb.top().data()[0], WithinRel(10.0, 1e-12));
        REQUIRE_THAT(bb.bottom().data()[0], WithinRel(10.0, 1e-12));
        REQUIRE_THAT(bb.mid().data()[2], WithinRel(30.0, 1e-12));
    }
}

// ============================================================================
// : Buffer Capacity Boundary Audit
// ============================================================================

TEST_CASE(": Line<T> bounds checking rejects out-of-range access",
          "[ticket025][boundary][line]") {
    stratforge::Line<double> line;
    line.forward(1.0);
    line.forward(2.0);
    line.forward(3.0);
    line.home();

    SECTION("valid access at index 0") {
        REQUIRE(line[0] == 1.0);
    }

    SECTION("negative offset throws when out of range") {
        // At home position (idx=0), offset -1 is out of range
        REQUIRE_THROWS_AS(line[-1], std::out_of_range);
    }

    SECTION("forward offset beyond size throws") {
        REQUIRE_THROWS_AS(line[3], std::out_of_range);
    }

    SECTION("empty line throws on any access") {
        stratforge::Line<double> empty;
        REQUIRE_THROWS_AS(empty[0], std::out_of_range);
    }
}

TEST_CASE(": LoadDiagnostics counters accumulate correctly",
          "[ticket025][boundary][data]") {
    const std::string path = tmp_path("ticket025_diag.csv");
    {
        std::ofstream file(path);
        file << "Date,Open,High,Low,Close,Volume,OI\n";
        file << "\n";                                          // empty row
        file << "bad\n";                                       // truncated row (too few fields)
        file << "2024-01-02,100,101,99,100,10,0\n";           // valid
        file << "2024-01-03,abc,102,100,101,11,0\n";          // malformed numeric
    }

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = path,
        .columns = {},
    });

    [[maybe_unused]] auto ok = feed.load();
    const auto& d = feed.diagnostics();
    REQUIRE(d.rows_skipped_empty == 1);
    REQUIRE(d.rows_skipped_truncated == 1);
    REQUIRE(d.rows_parsed >= 2);
    REQUIRE(d.fields_malformed_numeric >= 1);
}

TEST_CASE(": Broker order vectors auto-grow past initial reserve",
          "[ticket025][boundary][broker]") {
    stratforge::BackBroker broker(100000.0);

    // Submit more orders than the initial reserve (64 for pending, 1024 for all).
    // Vectors should auto-grow without error or silent truncation.
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] auto id = broker.buy(1.0);
    }

    // Verify all orders recorded (no silent truncation)
    REQUIRE(broker.orders().size() == 100);
}

TEST_CASE(": Resampler reserve uses estimated output size",
          "[ticket025][boundary][resampler]") {
    // Create a minute-level CSV with 100 bars
    const std::string path = tmp_path("ticket025_resample.csv");
    {
        std::ofstream file(path);
        file << "Date,Open,High,Low,Close,Volume,OI\n";
        // 100 one-minute bars on 2024-01-02
        for (int m = 0; m < 100; ++m) {
            int hour = 9 + m / 60;
            int min = m % 60;
            file << "2024-01-02 " << (hour < 10 ? "0" : "") << hour
                 << ":" << (min < 10 ? "0" : "") << min
                 << ":00,100,101,99,100,10,0\n";
        }
    }

    stratforge::CsvData source(stratforge::CsvData::Params{
        .filename = path,
        .columns = {},
        .date_format = "%Y-%m-%d %H:%M:%S",
    });
    REQUIRE(source.load());
    REQUIRE(source.size() == 100);

    // Resample 1-min → 15-min: expect ~7 output bars (100/15)
    stratforge::Resampler resampler(source, stratforge::TimeFrame::Minutes, 15);
    resampler.preload();

    // Output should be reasonable (not zero, not source size)
    REQUIRE(resampler.size() > 0);
    REQUIRE(resampler.size() <= source.size());
}
