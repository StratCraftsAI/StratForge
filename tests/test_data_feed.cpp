#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/core/error.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/timeframe.hpp>

#include <fstream>
#include <string>

using namespace stratforge;
using Catch::Matchers::WithinRel;

namespace {

// Helper: create a temporary CSV file for testing
std::string create_test_csv(const std::string& filename) {
    std::string path = "/tmp/" + filename;
    std::ofstream file(path);
    file << "Date,Open,High,Low,Close,Volume\n"
         << "2024-01-02,100.0,105.0,99.0,103.0,1000\n"
         << "2024-01-03,103.0,107.0,102.0,106.0,1500\n"
         << "2024-01-04,106.0,108.0,104.0,105.0,1200\n"
         << "2024-01-05,105.0,110.0,104.0,109.0,2000\n"
         << "2024-01-08,109.0,112.0,108.0,111.0,1800\n";
    file.close();
    return path;
}

} // anonymous namespace

TEST_CASE("TimeFrame enum", "[data][timeframe]") {
    SECTION("TimeFrameCompression defaults") {
        TimeFrameCompression tfc;
        REQUIRE(tfc.timeframe == TimeFrame::Days);
        REQUIRE(tfc.compression == 1);
    }

    SECTION("Custom timeframe compression") {
        TimeFrameCompression tfc{TimeFrame::Minutes, 5};
        REQUIRE(tfc.timeframe == TimeFrame::Minutes);
        REQUIRE(tfc.compression == 5);
    }
}

TEST_CASE("CsvData loading", "[data][csv]") {
    auto path = create_test_csv("test_ohlcv.csv");

    SECTION("Load all data") {
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        REQUIRE(feed.load());
        REQUIRE(feed.size() == 5);
    }

    SECTION("OHLCV values are correct") {
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        static_cast<void>(feed.load());

        // First bar
        REQUIRE_THAT(feed.open()[0], WithinRel(100.0, 0.001));
        REQUIRE_THAT(feed.high()[0], WithinRel(105.0, 0.001));
        REQUIRE_THAT(feed.low()[0], WithinRel(99.0, 0.001));
        REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));
        REQUIRE_THAT(feed.volume()[0], WithinRel(1000.0, 0.001));
    }

    SECTION("Advance through bars") {
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        static_cast<void>(feed.load());

        REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));

        feed.advance();
        REQUIRE_THAT(feed.close()[0], WithinRel(106.0, 0.001));
        REQUIRE_THAT(feed.close()[-1], WithinRel(103.0, 0.001));

        feed.advance();
        REQUIRE_THAT(feed.close()[0], WithinRel(105.0, 0.001));
    }

    SECTION("Buflen decreases on advance") {
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        static_cast<void>(feed.load());

        REQUIRE(feed.buflen() == 5);
        feed.advance();
        REQUIRE(feed.buflen() == 4);
    }

    SECTION("Missing file returns false") {
        CsvData feed(CsvData::Params{.filename = "/tmp/nonexistent_file.csv", .columns = {}});
        REQUIRE_FALSE(feed.load());
    }

    SECTION("Load only called once") {
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        REQUIRE(feed.load());
        REQUIRE_FALSE(feed.load()); // Second call returns false
    }

    SECTION("Custom separator") {
        std::string tsv_path = "/tmp/test_tsv.csv";
        std::ofstream file(tsv_path);
        file << "Date\tOpen\tHigh\tLow\tClose\tVolume\n"
             << "2024-01-02\t100.0\t105.0\t99.0\t103.0\t1000\n";
        file.close();

        CsvData feed(CsvData::Params{.filename = tsv_path, .columns = {}, .separator = '\t'});
        REQUIRE(feed.load());
        REQUIRE(feed.size() == 1);
        REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));
    }

    SECTION("Feed name and timeframe") {
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        feed.set_name("TEST");
        feed.set_timeframe({TimeFrame::Days, 1});

        REQUIRE(feed.name() == "TEST");
        REQUIRE(feed.timeframe().timeframe == TimeFrame::Days);
    }
}

TEST_CASE("CsvData load_expected error reporting", "[data][csv][expected]") {
    SECTION("FileNotFound — nonexistent path") {
        CsvData feed(CsvData::Params{.filename = "/tmp/stratforge_no_such_file.csv", .columns = {}});
        auto result = feed.load_expected();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == DataError::FileNotFound);
    }

    SECTION("AlreadyLoaded — double load") {
        auto path = create_test_csv("test_expected_double.csv");
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        auto first = feed.load_expected();
        REQUIRE(first.has_value());

        auto second = feed.load_expected();
        REQUIRE_FALSE(second.has_value());
        REQUIRE(second.error() == DataError::AlreadyLoaded);
    }

    SECTION("HeaderOnly — header-only CSV") {
        std::string path = "/tmp/stratforge_header_only.csv";
        {
            std::ofstream f(path);
            f << "Date,Open,High,Low,Close,Volume\n";
        }
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        auto result = feed.load_expected();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == DataError::HeaderOnly);
    }

    SECTION("EmptyFile — empty file with has_headers=false") {
        std::string path = "/tmp/stratforge_empty.csv";
        {
            std::ofstream f(path);
            // write nothing
        }
        CsvData feed(CsvData::Params{.filename = path, .columns = {}, .has_headers = false});
        auto result = feed.load_expected();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == DataError::EmptyFile);
    }

    SECTION("Success — normal CSV") {
        auto path = create_test_csv("test_expected_ok.csv");
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        auto result = feed.load_expected();
        REQUIRE(result.has_value());
        REQUIRE(feed.size() == 5);
    }

    SECTION("Bool load() wrapper still works identically") {
        auto path = create_test_csv("test_expected_bool.csv");
        CsvData feed(CsvData::Params{.filename = path, .columns = {}});
        REQUIRE(feed.load());
        REQUIRE(feed.size() == 5);
        REQUIRE_FALSE(feed.load()); // second call
    }

    SECTION("load_with_error() returns same result as load_expected()") {
        CsvData feed(CsvData::Params{.filename = "/tmp/stratforge_no_such_file2.csv", .columns = {}});
        auto result = feed.load_with_error();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == DataError::FileNotFound);
    }

    SECTION("to_string covers all DataError values") {
        REQUIRE(to_string(DataError::FileNotFound) == "file not found");
        REQUIRE(to_string(DataError::EmptyFile) == "no data rows after filtering");
        REQUIRE(to_string(DataError::HeaderOnly) == "header only, no data rows");
        REQUIRE(to_string(DataError::AlreadyLoaded) == "data already loaded");
    }
}
