#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/resampler.hpp>
#include "golden_reference.hpp"

#include <filesystem>
#include <string>
#include <iomanip>
#include <sstream>

using Catch::Matchers::WithinRel;

namespace {

std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

std::string to_iso_string(stratforge::DateTime dt) {
    using namespace std::chrono;
    auto s = duration_cast<seconds>(dt.time_since_epoch()).count();
    std::time_t t = static_cast<std::time_t>(s);
    std::tm tm = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

} // namespace

TEST_CASE("Resampler matches backtrader golden reference (Daily to Weekly)", "[data][resample][golden]") {
    const auto fixture = stratforge::test::load_resample_golden_reference(
        source_path("tools/golden_extract/output/resample/resample_2006_day_to_week_001.json"));

    stratforge::CsvData::Params params;
    params.filename = source_path("tools/golden_extract/datas/" + fixture.source);
    if (fixture.source.find("min") != std::string::npos) {
        params.columns = {
            .datetime = 0,
            .time = 1,
            .open = 2,
            .high = 3,
            .low = 4,
            .close = 5,
            .volume = 6,
            .openinterest = 7
        };
    } else {
        params.columns = {
            .datetime = 0,
            .time = -1,
            .open = 1,
            .high = 2,
            .low = 3,
            .close = 4,
            .volume = 5,
            .openinterest = 6
        };
    }
    stratforge::CsvData source(params);
    stratforge::Resampler resampled(source, stratforge::TimeFrame::Weeks, fixture.target_compression);
    resampled.preload();

    REQUIRE(resampled.size() == fixture.outputs.close.size());

    for (std::size_t i = 0; i < resampled.size(); ++i) {
        // Compare timestamps (ignoring microseconds mismatch)
        REQUIRE(to_iso_string(resampled.datetime().data()[i]) == fixture.outputs.datetime[i].substr(0, 19));

        REQUIRE_THAT(resampled.open().data()[i], WithinRel(fixture.outputs.open[i], 1e-6));
        REQUIRE_THAT(resampled.high().data()[i], WithinRel(fixture.outputs.high[i], 1e-6));
        REQUIRE_THAT(resampled.low().data()[i], WithinRel(fixture.outputs.low[i], 1e-6));
        REQUIRE_THAT(resampled.close().data()[i], WithinRel(fixture.outputs.close[i], 1e-6));
        REQUIRE_THAT(resampled.volume().data()[i], WithinRel(fixture.outputs.volume[i], 1e-6));
    }
}

TEST_CASE("Resampler matches backtrader golden reference (Daily to Monthly)", "[data][resample][golden]") {
    const auto fixture = stratforge::test::load_resample_golden_reference(
        source_path("tools/golden_extract/output/resample/resample_2006_day_to_month_001.json"));

    stratforge::CsvData::Params params;
    params.filename = source_path("tools/golden_extract/datas/" + fixture.source);
    if (fixture.source.find("min") != std::string::npos) {
        params.columns = {
            .datetime = 0,
            .time = 1,
            .open = 2,
            .high = 3,
            .low = 4,
            .close = 5,
            .volume = 6,
            .openinterest = 7
        };
    } else {
        params.columns = {
            .datetime = 0,
            .time = -1,
            .open = 1,
            .high = 2,
            .low = 3,
            .close = 4,
            .volume = 5,
            .openinterest = 6
        };
    }
    stratforge::CsvData source(params);
    stratforge::Resampler resampled(source, stratforge::TimeFrame::Months, fixture.target_compression);
    resampled.preload();

    REQUIRE(resampled.size() == fixture.outputs.close.size());

    for (std::size_t i = 0; i < resampled.size(); ++i) {
        REQUIRE(to_iso_string(resampled.datetime().data()[i]) == fixture.outputs.datetime[i].substr(0, 19));

        REQUIRE_THAT(resampled.open().data()[i], WithinRel(fixture.outputs.open[i], 1e-6));
        REQUIRE_THAT(resampled.high().data()[i], WithinRel(fixture.outputs.high[i], 1e-6));
        REQUIRE_THAT(resampled.low().data()[i], WithinRel(fixture.outputs.low[i], 1e-6));
        REQUIRE_THAT(resampled.close().data()[i], WithinRel(fixture.outputs.close[i], 1e-6));
        REQUIRE_THAT(resampled.volume().data()[i], WithinRel(fixture.outputs.volume[i], 1e-6));
    }
}

TEST_CASE("Resampler matches backtrader golden reference (5-min to 15-min)", "[data][resample][golden]") {
    const auto fixture = stratforge::test::load_resample_golden_reference(
        source_path("tools/golden_extract/output/resample/resample_2006_min5_to_min15_001.json"));

    stratforge::CsvData::Params params;
    params.filename = source_path("tools/golden_extract/datas/" + fixture.source);
    if (fixture.source.find("min") != std::string::npos) {
        params.columns = {
            .datetime = 0,
            .time = 1,
            .open = 2,
            .high = 3,
            .low = 4,
            .close = 5,
            .volume = 6,
            .openinterest = 7
        };
    } else {
        params.columns = {
            .datetime = 0,
            .time = -1,
            .open = 1,
            .high = 2,
            .low = 3,
            .close = 4,
            .volume = 5,
            .openinterest = 6
        };
    }
    stratforge::CsvData source(params);
    stratforge::Resampler resampled(source, stratforge::TimeFrame::Minutes, fixture.target_compression);
    resampled.preload();

    REQUIRE(resampled.size() == fixture.outputs.close.size());

    for (std::size_t i = 0; i < resampled.size(); ++i) {
        REQUIRE(to_iso_string(resampled.datetime().data()[i]) == fixture.outputs.datetime[i].substr(0, 19));

        REQUIRE_THAT(resampled.open().data()[i], WithinRel(fixture.outputs.open[i], 1e-6));
        REQUIRE_THAT(resampled.high().data()[i], WithinRel(fixture.outputs.high[i], 1e-6));
        REQUIRE_THAT(resampled.low().data()[i], WithinRel(fixture.outputs.low[i], 1e-6));
        REQUIRE_THAT(resampled.close().data()[i], WithinRel(fixture.outputs.close[i], 1e-6));
        REQUIRE_THAT(resampled.volume().data()[i], WithinRel(fixture.outputs.volume[i], 1e-6));
    }
}
