#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/data/csv_data.hpp>

#include "golden_reference.hpp"

#include <filesystem>
#include <string>

using Catch::Matchers::WithinRel;

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

} // namespace

TEST_CASE("CsvData matches golden reference fixture", "[golden][data][csv]") {
    const auto fixture = stratforge::test::load_csv_golden_reference(
        source_path("tests/golden/csv_2006_day_001.json"));

    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(fixture.data_file),
        .columns = {},
    });

    REQUIRE(feed.load());
    REQUIRE(feed.size() == fixture.expected_rows);

    SECTION("first bar matches golden fixture") {
        REQUIRE_THAT(feed.open()[0], WithinRel(fixture.first_bar.open, 0.000001));
        REQUIRE_THAT(feed.high()[0], WithinRel(fixture.first_bar.high, 0.000001));
        REQUIRE_THAT(feed.low()[0], WithinRel(fixture.first_bar.low, 0.000001));
        REQUIRE_THAT(feed.close()[0], WithinRel(fixture.first_bar.close, 0.000001));
        REQUIRE_THAT(feed.volume()[0], WithinRel(fixture.first_bar.volume, 0.000001));
        REQUIRE_THAT(feed.openinterest()[0], WithinRel(fixture.first_bar.openinterest, 0.000001));
    }

    SECTION("last bar matches golden fixture") {
        while (feed.buflen() > 1) {
            feed.advance();
        }

        REQUIRE_THAT(feed.open()[0], WithinRel(fixture.last_bar.open, 0.000001));
        REQUIRE_THAT(feed.high()[0], WithinRel(fixture.last_bar.high, 0.000001));
        REQUIRE_THAT(feed.low()[0], WithinRel(fixture.last_bar.low, 0.000001));
        REQUIRE_THAT(feed.close()[0], WithinRel(fixture.last_bar.close, 0.000001));
        REQUIRE_THAT(feed.volume()[0], WithinRel(fixture.last_bar.volume, 0.000001));
        REQUIRE_THAT(feed.openinterest()[0], WithinRel(fixture.last_bar.openinterest, 0.000001));
    }
}
