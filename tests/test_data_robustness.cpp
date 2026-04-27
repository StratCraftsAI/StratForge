#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/replay.hpp>
#include <stratforge/data/resampler.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

using namespace stratforge;
using Catch::Matchers::WithinRel;

namespace {

/// Write content to a temp CSV file and return the path
std::string write_csv(const std::string& name, const std::string& content) {
    std::string path = "/tmp/stratforge_robustness_" + name + ".csv";
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

/// Create a programmatic DataFeed with injected values (no file I/O)
class InMemoryFeed : public DataFeed {
public:
    struct Bar {
        DateTime dt;
        double o, h, l, c, v;
    };

    explicit InMemoryFeed(std::vector<Bar> bars) : bars_(std::move(bars)) {}

    [[nodiscard]] bool load() override {
        if (loaded_) return false;
        loaded_ = true;
        for (const auto& b : bars_) {
            datetime_.forward(b.dt);
            open_.forward(b.o);
            high_.forward(b.h);
            low_.forward(b.l);
            close_.forward(b.c);
            volume_.forward(b.v);
            openinterest_.forward(0.0);
        }
        datetime_.home();
        open_.home();
        high_.home();
        low_.home();
        close_.home();
        volume_.home();
        openinterest_.home();
        return !bars_.empty();
    }

    [[nodiscard]] std::unique_ptr<DataFeed> clone() const override { return nullptr; }

private:
    std::vector<Bar> bars_;
    bool loaded_ = false;
};

DateTime make_dt(int year, int mon, int day) {
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

DateTime make_dt_hm(int year, int mon, int day, int hour, int min) {
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // anonymous namespace

// ============================================================
// CSV Parser Diagnostic Tests
// ============================================================

TEST_CASE("CSV robustness: truncated rows are skipped and counted", "[data][robustness]") {
    auto path = write_csv("truncated", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0,105.0,99.0,103.0,1000
2024-01-03,103.0
2024-01-04,106.0,108.0,104.0,105.0,1200
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.rows_skipped_truncated == 1);
    REQUIRE(diag.rows_parsed == 3); // all 3 non-empty lines parsed
}

TEST_CASE("CSV robustness: extra fields are ignored", "[data][robustness]") {
    auto path = write_csv("extra_fields", R"(Date,Open,High,Low,Close,Volume,Extra1,Extra2
2024-01-02,100.0,105.0,99.0,103.0,1000,foo,bar
2024-01-03,103.0,107.0,102.0,106.0,1500,baz,qux
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);
    REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.rows_skipped_truncated == 0);
}

TEST_CASE("CSV robustness: malformed numbers become 0.0 and are counted", "[data][robustness]") {
    auto path = write_csv("malformed_num", R"(Date,Open,High,Low,Close,Volume
2024-01-02,N/A,105.0,99.0,103.0,1000
2024-01-03,103.0,,102.0,null,1500
2024-01-04,106.0,108.0,abc,105.0,1200
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 3);

    // N/A → 0.0
    REQUIRE(feed.open()[0] == 0.0);
    // empty string → 0.0
    REQUIRE(feed.high().data()[1] == 0.0);
    // "null" → 0.0
    REQUIRE(feed.close().data()[1] == 0.0);
    // "abc" → 0.0
    REQUIRE(feed.low().data()[2] == 0.0);

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.fields_malformed_numeric == 4); // N/A, empty, null, abc
}

TEST_CASE("CSV robustness: trailing junk in numeric fields is counted as malformed", "[data][robustness]") {
    auto path = write_csv("trailing_junk", R"(Date,Open,High,Low,Close,Volume
2024-01-02,123abc,105.0,99.0,103.0,1000
2024-01-03,103.0,1.2foo,102.0,106.0,1500
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);

    // Partial parses still return the numeric portion (backtrader compat)
    REQUIRE_THAT(feed.open()[0], WithinRel(123.0, 0.001));
    REQUIRE_THAT(feed.high().data()[1], WithinRel(1.2, 0.001));

    // But they ARE counted as malformed
    const auto& diag = feed.diagnostics();
    REQUIRE(diag.fields_malformed_numeric == 2);
}

TEST_CASE("CSV robustness: trailing whitespace in numeric fields is not malformed", "[data][robustness]") {
    auto path = write_csv("trailing_ws", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0 ,105.0	,99.0,103.0,1000
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 1);
    REQUIRE_THAT(feed.open()[0], WithinRel(100.0, 0.001));

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.fields_malformed_numeric == 0);
}

TEST_CASE("CSV robustness: trailing empty field is malformed not truncated", "[data][robustness]") {
    // "2024-01-03,103.0,107.0,102.0," has 5 fields (last is empty string)
    // The empty Close field should be malformed numeric (→ 0.0), NOT row-truncated
    auto path = write_csv("trailing_empty", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0,105.0,99.0,103.0,1000
2024-01-03,103.0,107.0,102.0,,1500
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2); // both rows loaded
    REQUIRE(feed.close().data()[1] == 0.0); // empty → 0.0

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.rows_skipped_truncated == 0); // NOT truncated
    REQUIRE(diag.fields_malformed_numeric == 1); // 1 malformed (the empty Close)
}

TEST_CASE("CSV robustness: leading plus sign accepted (backtrader compat)", "[data][robustness]") {
    // Python float("+1.5") == 1.5, float("+inf") == inf
    // from_chars rejects '+', so csv_data must handle it
    auto path = write_csv("plus_sign", R"(Date,Open,High,Low,Close,Volume
2024-01-02,+100.0,+105.0,99.0,103.0,+1000
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 1);
    REQUIRE_THAT(feed.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(feed.high().data()[0], WithinRel(105.0, 0.001));
    REQUIRE_THAT(feed.volume().data()[0], WithinRel(1000.0, 0.001));

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.fields_malformed_numeric == 0); // no malformed — '+' is valid
}

TEST_CASE("CSV robustness: empty lines in middle are skipped", "[data][robustness]") {
    auto path = write_csv("empty_lines", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0,105.0,99.0,103.0,1000

2024-01-03,103.0,107.0,102.0,106.0,1500

2024-01-04,106.0,108.0,104.0,105.0,1200
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 3);

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.rows_skipped_empty == 2);
}

TEST_CASE("CSV robustness: invalid dates become epoch and are counted", "[data][robustness]") {
    auto path = write_csv("bad_dates", R"(Date,Open,High,Low,Close,Volume
not-a-date,100.0,105.0,99.0,103.0,1000
2024-01-03,103.0,107.0,102.0,106.0,1500
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.fields_malformed_datetime == 1);
    // First row datetime should be epoch (DateTime{})
    // Epoch behavior: mktime of zeroed tm → some date near epoch
}

TEST_CASE("CSV robustness: malformed time column is counted as malformed datetime", "[data][robustness]") {
    auto path = write_csv("bad_time", R"(Date,Time,Open,High,Low,Close,Volume
2024-01-02,10:30:00,100.0,105.0,99.0,103.0,1000
2024-01-03,bad,103.0,107.0,102.0,106.0,1500
2024-01-04,99:99:99,106.0,108.0,104.0,105.0,1200
)");

    CsvColumnMap cols;
    cols.datetime = 0;
    cols.time = 1;
    cols.open = 2;
    cols.high = 3;
    cols.low = 4;
    cols.close = 5;
    cols.volume = 6;

    CsvData feed(CsvData::Params{.filename = path, .columns = cols});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 3); // all loaded (backtrader compat)

    const auto& diag = feed.diagnostics();
    // "bad" fails get_time, "99:99:99" also fails get_time
    REQUIRE(diag.fields_malformed_datetime == 2);
}

TEST_CASE("CSV robustness: UTF-8 BOM is stripped", "[data][robustness]") {
    auto path = write_csv("bom", "");
    // Write BOM + content manually
    {
        std::ofstream f(path);
        f << "\xEF\xBB\xBF" << "Date,Open,High,Low,Close,Volume\n"
          << "2024-01-02,100.0,105.0,99.0,103.0,1000\n";
        f.close();
    }

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 1);
    REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));

    // No malformed datetime — BOM was stripped before parsing
    REQUIRE(feed.diagnostics().fields_malformed_datetime == 0);
}

TEST_CASE("CSV robustness: date filter skips are counted", "[data][robustness]") {
    auto path = write_csv("datefilter", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0,105.0,99.0,103.0,1000
2024-01-03,103.0,107.0,102.0,106.0,1500
2024-01-04,106.0,108.0,104.0,105.0,1200
2024-01-05,105.0,110.0,104.0,109.0,2000
)");

    auto from = make_dt(2024, 1, 3);
    auto to = make_dt(2024, 1, 4);

    CsvData feed(CsvData::Params{
        .filename = path, .columns = {},
        .fromdate = from, .todate = to});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.rows_skipped_datefilter == 2);
}

TEST_CASE("CSV robustness: duplicate timestamps detected in diagnostics", "[data][robustness]") {
    auto path = write_csv("dup_ts", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0,105.0,99.0,103.0,1000
2024-01-02,101.0,106.0,100.0,104.0,1100
2024-01-03,103.0,107.0,102.0,106.0,1500
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 3); // all loaded (backtrader compat)

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.duplicate_timestamps == 1);
}

TEST_CASE("CSV robustness: out-of-order timestamps detected in diagnostics", "[data][robustness]") {
    auto path = write_csv("ooo_ts", R"(Date,Open,High,Low,Close,Volume
2024-01-03,103.0,107.0,102.0,106.0,1500
2024-01-02,100.0,105.0,99.0,103.0,1000
2024-01-04,106.0,108.0,104.0,105.0,1200
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 3); // all loaded (backtrader compat)

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.out_of_order_timestamps == 1);
}

TEST_CASE("CSV robustness: clean file has zero diagnostic issues", "[data][robustness]") {
    auto path = write_csv("clean", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0,105.0,99.0,103.0,1000
2024-01-03,103.0,107.0,102.0,106.0,1500
2024-01-04,106.0,108.0,104.0,105.0,1200
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 3);

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.rows_parsed == 3);
    REQUIRE(diag.rows_skipped_truncated == 0);
    REQUIRE(diag.rows_skipped_empty == 0);
    REQUIRE(diag.rows_skipped_datefilter == 0);
    REQUIRE(diag.fields_malformed_numeric == 0);
    REQUIRE(diag.fields_malformed_datetime == 0);
    REQUIRE(diag.duplicate_timestamps == 0);
    REQUIRE(diag.out_of_order_timestamps == 0);
}

// ============================================================
// DataFeed Validation Tests
// ============================================================

TEST_CASE("DataFeed validate: empty feed", "[data][validation]") {
    InMemoryFeed feed({});
    // load() returns false for empty, but validate() still works
    static_cast<void>(feed.load());
    auto result = feed.validate();
    REQUIRE_FALSE(result.has_duplicates);
    REQUIRE(result.is_monotonic);
    REQUIRE_FALSE(result.has_nan);
    REQUIRE_FALSE(result.has_negative_prices);
}

TEST_CASE("DataFeed validate: single bar is clean", "[data][validation]") {
    InMemoryFeed feed({{make_dt(2024, 1, 2), 100.0, 105.0, 99.0, 103.0, 1000.0}});
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE_FALSE(result.has_duplicates);
    REQUIRE(result.is_monotonic);
    REQUIRE_FALSE(result.has_nan);
    REQUIRE_FALSE(result.has_negative_prices);
}

TEST_CASE("DataFeed validate: duplicate timestamps detected", "[data][validation]") {
    auto dt = make_dt(2024, 1, 2);
    InMemoryFeed feed({
        {dt, 100.0, 105.0, 99.0, 103.0, 1000.0},
        {dt, 101.0, 106.0, 100.0, 104.0, 1100.0},
        {make_dt(2024, 1, 3), 103.0, 107.0, 102.0, 106.0, 1500.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE(result.has_duplicates);
    REQUIRE(result.duplicate_count == 1);
    REQUIRE(result.is_monotonic); // duplicates don't break monotonicity check
}

TEST_CASE("DataFeed validate: out-of-order timestamps detected", "[data][validation]") {
    InMemoryFeed feed({
        {make_dt(2024, 1, 3), 103.0, 107.0, 102.0, 106.0, 1500.0},
        {make_dt(2024, 1, 2), 100.0, 105.0, 99.0, 103.0, 1000.0},
        {make_dt(2024, 1, 4), 106.0, 108.0, 104.0, 105.0, 1200.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE_FALSE(result.is_monotonic);
}

TEST_CASE("DataFeed validate: NaN values detected", "[data][validation]") {
    constexpr auto nan = std::numeric_limits<double>::quiet_NaN();
    InMemoryFeed feed({
        {make_dt(2024, 1, 2), nan, 105.0, 99.0, 103.0, 1000.0},
        {make_dt(2024, 1, 3), 103.0, 107.0, nan, 106.0, 1500.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE(result.has_nan);
    REQUIRE(result.nan_count == 2);
}

TEST_CASE("DataFeed validate: Inf values detected as NaN", "[data][validation]") {
    constexpr auto inf = std::numeric_limits<double>::infinity();
    InMemoryFeed feed({
        {make_dt(2024, 1, 2), inf, 105.0, 99.0, 103.0, 1000.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE(result.has_nan);
    REQUIRE(result.nan_count == 1);
}

TEST_CASE("DataFeed validate: negative prices detected", "[data][validation]") {
    InMemoryFeed feed({
        {make_dt(2024, 1, 2), -100.0, 105.0, 99.0, 103.0, 1000.0},
        {make_dt(2024, 1, 3), 103.0, 107.0, -1.0, 106.0, 1500.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE(result.has_negative_prices);
    REQUIRE(result.negative_price_count == 2);
}

TEST_CASE("DataFeed validate: zero volume is allowed", "[data][validation]") {
    InMemoryFeed feed({
        {make_dt(2024, 1, 2), 100.0, 105.0, 99.0, 103.0, 0.0},
        {make_dt(2024, 1, 3), 103.0, 107.0, 102.0, 106.0, 0.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE_FALSE(result.has_nan);
    REQUIRE_FALSE(result.has_negative_prices);
}

TEST_CASE("DataFeed validate: clean data passes all checks", "[data][validation]") {
    InMemoryFeed feed({
        {make_dt(2024, 1, 2), 100.0, 105.0, 99.0, 103.0, 1000.0},
        {make_dt(2024, 1, 3), 103.0, 107.0, 102.0, 106.0, 1500.0},
        {make_dt(2024, 1, 4), 106.0, 108.0, 104.0, 105.0, 1200.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    REQUIRE_FALSE(result.has_duplicates);
    REQUIRE(result.is_monotonic);
    REQUIRE_FALSE(result.has_nan);
    REQUIRE_FALSE(result.has_negative_prices);
    REQUIRE(result.duplicate_count == 0);
    REQUIRE(result.nan_count == 0);
    REQUIRE(result.negative_price_count == 0);
}

TEST_CASE("DataFeed validate: negative volume not flagged as negative price", "[data][validation]") {
    InMemoryFeed feed({
        {make_dt(2024, 1, 2), 100.0, 105.0, 99.0, 103.0, -500.0},
    });
    REQUIRE(feed.load());
    auto result = feed.validate();
    // Volume is excluded from negative price check
    REQUIRE_FALSE(result.has_negative_prices);
    REQUIRE(result.negative_price_count == 0);
}

// ============================================================
// CSV Parser Edge Cases — Header Variations
// ============================================================

TEST_CASE("CSV robustness: has_headers=false loads first line as data", "[data][robustness]") {
    // No header row — first line IS data
    auto path = write_csv("no_header", R"(2024-01-02,100.0,105.0,99.0,103.0,1000
2024-01-03,103.0,107.0,102.0,106.0,1500
)");

    CsvData feed(CsvData::Params{
        .filename = path, .columns = {}, .has_headers = false});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);
    REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));
    REQUIRE_THAT(feed.close().data()[1], WithinRel(106.0, 0.001));
}

TEST_CASE("CSV robustness: custom CsvColumnMap with non-default column order", "[data][robustness]") {
    // Columns: Date, Close, Volume, Open, High, Low  (non-standard order)
    auto path = write_csv("custom_colmap", R"(Date,Close,Volume,Open,High,Low
2024-01-02,103.0,1000,100.0,105.0,99.0
2024-01-03,106.0,1500,103.0,107.0,102.0
)");

    CsvColumnMap cols;
    cols.datetime = 0;
    cols.close = 1;
    cols.volume = 2;
    cols.open = 3;
    cols.high = 4;
    cols.low = 5;

    CsvData feed(CsvData::Params{.filename = path, .columns = cols});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);
    REQUIRE_THAT(feed.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(feed.high().data()[0], WithinRel(105.0, 0.001));
    REQUIRE_THAT(feed.low().data()[0], WithinRel(99.0, 0.001));
    REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));
    REQUIRE_THAT(feed.volume().data()[0], WithinRel(1000.0, 0.001));
}

TEST_CASE("CSV robustness: header names are not validated (positional only)", "[data][robustness]") {
    // Header names are arbitrary — parsing is purely positional (backtrader compat)
    auto path = write_csv("wrong_headers", R"(Foo,Bar,Baz,Qux,Quux,Corge
2024-01-02,100.0,105.0,99.0,103.0,1000
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 1);
    REQUIRE_THAT(feed.close()[0], WithinRel(103.0, 0.001));
}

// ============================================================
// CSV Parser Edge Cases — Date Formats
// ============================================================

TEST_CASE("CSV robustness: date-only format defaults time to 00:00:00", "[data][robustness]") {
    auto path = write_csv("date_only", R"(Date,Open,High,Low,Close,Volume
2024-01-02,100.0,105.0,99.0,103.0,1000
)");

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 1);

    // Date-only: time component should be midnight (00:00:00)
    auto dt = feed.datetime()[0];
    auto days = std::chrono::floor<std::chrono::days>(dt);
    auto time_of_day = dt - DateTime(days);
    REQUIRE(time_of_day == std::chrono::seconds(0));
}

TEST_CASE("CSV robustness: separate time column with midnight boundary", "[data][robustness]") {
    auto path = write_csv("midnight_boundary", R"(Date,Time,Open,High,Low,Close,Volume
2024-01-02,23:59:00,100.0,105.0,99.0,103.0,1000
2024-01-03,00:00:00,103.0,107.0,102.0,106.0,1500
)");

    CsvColumnMap cols;
    cols.datetime = 0;
    cols.time = 1;
    cols.open = 2;
    cols.high = 3;
    cols.low = 4;
    cols.close = 5;
    cols.volume = 6;

    CsvData feed(CsvData::Params{.filename = path, .columns = cols});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 2);

    // First bar: 2024-01-02 23:59, second: 2024-01-03 00:00
    // They should be on different calendar days
    auto dt0 = feed.datetime()[0];
    auto dt1 = feed.datetime().data()[1];
    auto day0 = std::chrono::floor<std::chrono::days>(dt0);
    auto day1 = std::chrono::floor<std::chrono::days>(dt1);
    REQUIRE(day1 > day0);

    // Verify no malformed datetimes
    REQUIRE(feed.diagnostics().fields_malformed_datetime == 0);
}

// ============================================================
// CSV Parser Edge Cases — Large Files
// ============================================================

TEST_CASE("CSV robustness: synthetic 100K row file loads correctly", "[data][robustness]") {
    // Generate a synthetic 100K row CSV programmatically
    std::string path = "/tmp/stratforge_robustness_100k.csv";
    {
        std::ofstream f(path);
        f << "Date,Open,High,Low,Close,Volume\n";
        // Start from 2020-01-01 and increment daily
        auto base = std::chrono::sys_days{std::chrono::year{2020}/std::chrono::January/1};
        for (int i = 0; i < 100000; ++i) {
            auto day = base + std::chrono::days(i);
            std::chrono::year_month_day ymd{day};
            f << static_cast<int>(ymd.year()) << '-'
              << std::setfill('0') << std::setw(2) << static_cast<unsigned>(ymd.month()) << '-'
              << std::setfill('0') << std::setw(2) << static_cast<unsigned>(ymd.day()) << ','
              << (100.0 + i * 0.01) << ','
              << (101.0 + i * 0.01) << ','
              << (99.0 + i * 0.01) << ','
              << (100.5 + i * 0.01) << ','
              << (1000 + i) << '\n';
        }
        f.close();
    }

    CsvData feed(CsvData::Params{.filename = path, .columns = {}});
    REQUIRE(feed.load());
    REQUIRE(feed.size() == 100000);

    const auto& diag = feed.diagnostics();
    REQUIRE(diag.rows_parsed == 100000);
    REQUIRE(diag.rows_skipped_truncated == 0);
    REQUIRE(diag.rows_skipped_empty == 0);
    REQUIRE(diag.fields_malformed_numeric == 0);
    REQUIRE(diag.fields_malformed_datetime == 0);
}

// ============================================================
// Replay / Resampler — Boundary Conditions
// ============================================================

TEST_CASE("Replay robustness: empty source feed produces 0 bars", "[data][robustness]") {
    InMemoryFeed source({});
    static_cast<void>(source.load());

    DataReplay replay(source, TimeFrame::Days, 1);
    replay.preload();
    REQUIRE(replay.size() == 0);
}

TEST_CASE("Resampler robustness: empty source feed produces 0 bars", "[data][robustness]") {
    InMemoryFeed source({});
    static_cast<void>(source.load());

    Resampler resampler(source, TimeFrame::Weeks, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 0);
}

TEST_CASE("Replay robustness: single-bar source produces 1 output bar", "[data][robustness]") {
    InMemoryFeed source({{make_dt(2024, 1, 2), 100.0, 105.0, 99.0, 103.0, 1000.0}});
    REQUIRE(source.load());

    DataReplay replay(source, TimeFrame::Days, 1);
    replay.preload();
    REQUIRE(replay.size() == 1);
    REQUIRE_THAT(replay.close()[0], WithinRel(103.0, 0.001));
}

TEST_CASE("Resampler robustness: single-bar source produces 1 output bar", "[data][robustness]") {
    InMemoryFeed source({{make_dt(2024, 1, 2), 100.0, 105.0, 99.0, 103.0, 1000.0}});
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Weeks, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 1);
    REQUIRE_THAT(resampler.close()[0], WithinRel(103.0, 0.001));
}

TEST_CASE("Resampler robustness: incomplete final period emits 1 bar", "[data][robustness]") {
    // 2 bars (Wed, Thu) in a partial week → should emit 1 weekly bar
    InMemoryFeed source({
        {make_dt(2024, 1, 3), 100.0, 105.0, 99.0, 103.0, 1000.0},  // Wed
        {make_dt(2024, 1, 4), 103.0, 108.0, 101.0, 107.0, 1200.0},  // Thu
    });
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Weeks, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 1);
    // OHLCV should aggregate: O=100, H=max(105,108)=108, L=min(99,101)=99, C=107, V=2200
    REQUIRE_THAT(resampler.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(resampler.high()[0], WithinRel(108.0, 0.001));
    REQUIRE_THAT(resampler.low()[0], WithinRel(99.0, 0.001));
    REQUIRE_THAT(resampler.close()[0], WithinRel(107.0, 0.001));
    REQUIRE_THAT(resampler.volume()[0], WithinRel(2200.0, 0.001));
}

// ============================================================
// Replay / Resampler — Gap Handling
// ============================================================

TEST_CASE("Resampler robustness: weekend gap in daily data resampled to weekly", "[data][robustness]") {
    // Mon-Fri week, then Mon next week — weekend gap should not split the first week
    InMemoryFeed source({
        {make_dt(2024, 1, 1), 100.0, 102.0, 99.0, 101.0, 1000.0},  // Mon
        {make_dt(2024, 1, 2), 101.0, 103.0, 100.0, 102.0, 1100.0},  // Tue
        {make_dt(2024, 1, 3), 102.0, 104.0, 101.0, 103.0, 1200.0},  // Wed
        {make_dt(2024, 1, 4), 103.0, 105.0, 102.0, 104.0, 1300.0},  // Thu
        {make_dt(2024, 1, 5), 104.0, 106.0, 103.0, 105.0, 1400.0},  // Fri
        // Weekend gap
        {make_dt(2024, 1, 8), 105.0, 108.0, 104.0, 107.0, 1500.0},  // Mon next week
    });
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Weeks, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 2); // 2 week bars

    // First week: O=100, H=106, L=99, C=105, V=6000
    REQUIRE_THAT(resampler.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(resampler.high()[0], WithinRel(106.0, 0.001));
    REQUIRE_THAT(resampler.low()[0], WithinRel(99.0, 0.001));
    REQUIRE_THAT(resampler.close()[0], WithinRel(105.0, 0.001));
    REQUIRE_THAT(resampler.volume()[0], WithinRel(6000.0, 0.001));
}

TEST_CASE("Resampler robustness: multi-day holiday gap resampled to weekly", "[data][robustness]") {
    // Mon, Tue then gap (Wed-Thu holiday), Fri — all in same week
    InMemoryFeed source({
        {make_dt(2024, 1, 1), 100.0, 102.0, 99.0, 101.0, 1000.0},  // Mon
        {make_dt(2024, 1, 2), 101.0, 103.0, 100.0, 102.0, 1100.0},  // Tue
        // Wed-Thu holiday gap
        {make_dt(2024, 1, 5), 104.0, 106.0, 103.0, 105.0, 1400.0},  // Fri
    });
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Weeks, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 1); // all in 1 week bar despite gap

    // Aggregated: O=100, H=106, L=99, C=105, V=3500
    REQUIRE_THAT(resampler.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(resampler.high()[0], WithinRel(106.0, 0.001));
    REQUIRE_THAT(resampler.low()[0], WithinRel(99.0, 0.001));
    REQUIRE_THAT(resampler.close()[0], WithinRel(105.0, 0.001));
    REQUIRE_THAT(resampler.volume()[0], WithinRel(3500.0, 0.001));
}

TEST_CASE("Resampler robustness: monthly resampling across month boundary with gap", "[data][robustness]") {
    // Jan bars, then gap, then Feb bars
    InMemoryFeed source({
        {make_dt(2024, 1, 29), 100.0, 105.0, 99.0, 103.0, 1000.0},
        {make_dt(2024, 1, 30), 103.0, 106.0, 102.0, 104.0, 1100.0},
        // Jan 31 missing (gap)
        {make_dt(2024, 2, 1), 104.0, 108.0, 103.0, 107.0, 1200.0},
        {make_dt(2024, 2, 2), 107.0, 110.0, 106.0, 109.0, 1300.0},
    });
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Months, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 2); // Jan bar + Feb bar

    // Jan: O=100, H=106, L=99, C=104
    REQUIRE_THAT(resampler.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(resampler.high()[0], WithinRel(106.0, 0.001));
    REQUIRE_THAT(resampler.close()[0], WithinRel(104.0, 0.001));

    // Feb: O=104, H=110, L=103, C=109
    REQUIRE_THAT(resampler.open().data()[1], WithinRel(104.0, 0.001));
    REQUIRE_THAT(resampler.high().data()[1], WithinRel(110.0, 0.001));
    REQUIRE_THAT(resampler.close().data()[1], WithinRel(109.0, 0.001));
}

// ============================================================
// Replay / Resampler — Timeframe Transitions
// ============================================================

TEST_CASE("Resampler robustness: minute data crossing midnight emits end-of-day bar", "[data][robustness]") {
    // Minute bars: 23:58, 23:59 on day 1, then 00:00, 00:01 on day 2
    // Resampled to daily → should produce 2 day bars
    InMemoryFeed source({
        {make_dt_hm(2024, 1, 2, 23, 58), 100.0, 101.0, 99.0, 100.5, 500.0},
        {make_dt_hm(2024, 1, 2, 23, 59), 100.5, 102.0, 100.0, 101.0, 600.0},
        {make_dt_hm(2024, 1, 3, 0, 0),   101.0, 103.0, 100.5, 102.0, 700.0},
        {make_dt_hm(2024, 1, 3, 0, 1),   102.0, 104.0, 101.0, 103.0, 800.0},
    });
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Days, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 2);

    // Day 1: O=100, H=102, L=99, C=101
    REQUIRE_THAT(resampler.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(resampler.high()[0], WithinRel(102.0, 0.001));
    REQUIRE_THAT(resampler.low()[0], WithinRel(99.0, 0.001));
    REQUIRE_THAT(resampler.close()[0], WithinRel(101.0, 0.001));

    // Day 2: O=101, H=104, L=100.5, C=103
    REQUIRE_THAT(resampler.open().data()[1], WithinRel(101.0, 0.001));
    REQUIRE_THAT(resampler.high().data()[1], WithinRel(104.0, 0.001));
    REQUIRE_THAT(resampler.close().data()[1], WithinRel(103.0, 0.001));
}

TEST_CASE("Resampler robustness: daily data starting mid-week gives partial first week", "[data][robustness]") {
    // Start on Wednesday — first week bar only covers Wed-Fri (3 bars)
    InMemoryFeed source({
        {make_dt(2024, 1, 3), 100.0, 102.0, 99.0, 101.0, 1000.0},  // Wed
        {make_dt(2024, 1, 4), 101.0, 103.0, 100.0, 102.0, 1100.0},  // Thu
        {make_dt(2024, 1, 5), 102.0, 104.0, 101.0, 103.0, 1200.0},  // Fri
        // Next week
        {make_dt(2024, 1, 8), 103.0, 106.0, 102.0, 105.0, 1300.0},  // Mon
        {make_dt(2024, 1, 9), 105.0, 107.0, 104.0, 106.0, 1400.0},  // Tue
    });
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Weeks, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 2);

    // First (partial) week: O=100, H=104, L=99, C=103, V=3300
    REQUIRE_THAT(resampler.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(resampler.high()[0], WithinRel(104.0, 0.001));
    REQUIRE_THAT(resampler.low()[0], WithinRel(99.0, 0.001));
    REQUIRE_THAT(resampler.close()[0], WithinRel(103.0, 0.001));
    REQUIRE_THAT(resampler.volume()[0], WithinRel(3300.0, 0.001));
}

TEST_CASE("Resampler robustness: monthly resampling Feb to Mar (28-day boundary)", "[data][robustness]") {
    // 2024 is a leap year (29 days in Feb), but we test the month boundary
    InMemoryFeed source({
        {make_dt(2024, 2, 28), 100.0, 105.0, 99.0, 103.0, 1000.0},
        {make_dt(2024, 2, 29), 103.0, 106.0, 102.0, 104.0, 1100.0},  // leap day
        {make_dt(2024, 3, 1),  104.0, 108.0, 103.0, 107.0, 1200.0},
        {make_dt(2024, 3, 4),  107.0, 110.0, 106.0, 109.0, 1300.0},
    });
    REQUIRE(source.load());

    Resampler resampler(source, TimeFrame::Months, 1);
    resampler.preload();
    REQUIRE(resampler.size() == 2); // Feb bar + Mar bar

    // Feb: O=100, H=106, C=104
    REQUIRE_THAT(resampler.open()[0], WithinRel(100.0, 0.001));
    REQUIRE_THAT(resampler.high()[0], WithinRel(106.0, 0.001));
    REQUIRE_THAT(resampler.close()[0], WithinRel(104.0, 0.001));

    // Mar: O=104, H=110, C=109
    REQUIRE_THAT(resampler.open().data()[1], WithinRel(104.0, 0.001));
    REQUIRE_THAT(resampler.high().data()[1], WithinRel(110.0, 0.001));
    REQUIRE_THAT(resampler.close().data()[1], WithinRel(109.0, 0.001));
}
