#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/analytics/extended_metrics.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/stddev.hpp>

#include <fstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinRel;

namespace {

std::string create_test_csv(const std::string& filename, const std::vector<std::string>& rows) {
    const std::string path = "/tmp/" + filename;
    std::ofstream file(path);
    file << "Date,Open,High,Low,Close,Volume,OpenInterest\n";
    for (const auto& row : rows) {
        file << row << '\n';
    }
    return path;
}

stratforge::Line<double> make_line(const std::vector<double>& values) {
    stratforge::Line<double> line;
    for (double value : values) {
        line.forward(value);
    }
    line.home();
    return line;
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
}
