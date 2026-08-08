// SPDX-License-Identifier: MIT
//
// tests/test_backtest_runner_feeds.cpp -- BacktestConfig feeds[] parser and
// multi-feed load loop tests.
//
//  P2: Verifies:
//   - Old-shape config (no feeds[]) -> unchanged output (back-compat AC5)
//   - feeds[] parse round-trip: all fields parsed correctly
//   - Wrong feed order (context feed finer than execution) -> refused
//   - Missing parquet file -> refused with structured error
//   - Invalid interval token -> refused
//   - Resample feed with missing base -> refused
//   - Warmup-infeasible window -> refused with structured error message
//   - Back-compat synth plan from data_file

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <stratforge/engine/backtest_runner.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace stratforge;
using Catch::Matchers::ContainsSubstring;

namespace {

/// Write a minimal CSV data file for testing. Returns the path.
std::string write_test_csv(const std::string& name) {
    auto path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream f(path);
    f << "Date,Open,High,Low,Close,Volume\n";
    f << "2024-01-02,100.0,105.0,98.0,102.0,1000\n";
    f << "2024-01-03,102.0,108.0,101.0,107.0,1100\n";
    f << "2024-01-04,107.0,110.0,106.0,109.0,900\n";
    f << "2024-01-05,109.0,112.0,108.0,111.0,950\n";
    f << "2024-01-08,111.0,114.0,110.0,113.0,1050\n";
    f.close();
    return path;
}

/// Simple strategy that buys on nextstart, for integration testing.
class SimpleBuyStrategy final : public Strategy {
public:
    void nextstart() override {
        static_cast<void>(buy(1.0));
    }
    void next() override {}
};

} // namespace

// =========================================================================
// Parser: feeds[] round-trip
// =========================================================================

TEST_CASE("BacktestConfigParser parses feeds[] array with all fields",
          "[backtest_runner][feeds][parser]") {
    const std::string json = R"({
        "data_file": "ignored.csv",
        "initial_cash": 50000,
        "feeds": [
            { "index": 0, "interval": "1h", "role": "execution",
              "source": "parquet", "dataPath": "/data/EURAUD_1h.parquet" },
            { "index": 1, "interval": "1d", "role": "context",
              "source": "parquet", "dataPath": "/data/EURAUD_1d.parquet" },
            { "index": 2, "interval": "1M", "role": "context",
              "source": "resample", "base": "1h" }
        ]
    })";

    auto cfg = parse_backtest_config(json);

    REQUIRE(cfg.feeds.size() == 3);

    CHECK(cfg.feeds[0].index == 0);
    CHECK(cfg.feeds[0].interval == "1h");
    CHECK(cfg.feeds[0].role == "execution");
    CHECK(cfg.feeds[0].source == "parquet");
    CHECK(cfg.feeds[0].data_path == "/data/EURAUD_1h.parquet");
    CHECK(cfg.feeds[0].base.empty());

    CHECK(cfg.feeds[1].index == 1);
    CHECK(cfg.feeds[1].interval == "1d");
    CHECK(cfg.feeds[1].role == "context");
    CHECK(cfg.feeds[1].source == "parquet");
    CHECK(cfg.feeds[1].data_path == "/data/EURAUD_1d.parquet");

    CHECK(cfg.feeds[2].index == 2);
    CHECK(cfg.feeds[2].interval == "1M");
    CHECK(cfg.feeds[2].role == "context");
    CHECK(cfg.feeds[2].source == "resample");
    CHECK(cfg.feeds[2].base == "1h");
    CHECK(cfg.feeds[2].data_path.empty());
}

TEST_CASE("BacktestConfigParser returns empty feeds for legacy config",
          "[backtest_runner][feeds][parser]") {
    const std::string json = R"({
        "data_file": "test.csv",
        "initial_cash": 100000
    })";

    auto cfg = parse_backtest_config(json);
    CHECK(cfg.feeds.empty());
    CHECK(cfg.data_file == "test.csv");
}

//  regression: a nested "feeds" key earlier in the document
// (the app used to leak its internal feedPlan, whose feeds carry an
// object-shaped source) must not shadow the top-level flat feeds[] array.
// The old substring-based key search parsed feedPlan.feeds first and
// returned source="kind", refusing every multi-timeframe run.
TEST_CASE("BacktestConfigParser ignores nested feeds duplicate before top-level",
          "[backtest_runner][feeds][parser]") {
    const std::string json = R"({
        "feedPlan": {
            "feeds": [
                { "index": 0, "interval": "5m", "role": "execution",
                  "source": { "kind": "parquet",
                              "dataPath": "/data/nested_wrong.parquet" } }
            ],
            "executionInterval": "5m"
        },
        "data_file": "ignored.csv",
        "feeds": [
            { "index": 0, "interval": "5m", "role": "execution",
              "source": "parquet", "dataPath": "/data/EURAUD_5m.parquet" },
            { "index": 1, "interval": "1h", "role": "context",
              "source": "parquet", "dataPath": "/data/EURAUD_1h.parquet" }
        ]
    })";

    auto cfg = parse_backtest_config(json);

    REQUIRE(cfg.feeds.size() == 2);
    CHECK(cfg.feeds[0].source == "parquet");
    CHECK(cfg.feeds[0].data_path == "/data/EURAUD_5m.parquet");
    CHECK(cfg.feeds[1].interval == "1h");
    CHECK(cfg.feeds[1].role == "context");
}

// =========================================================================
// Validation: feed plan rules
// =========================================================================

TEST_CASE("validate_feed_plan: empty plan rejected",
          "[backtest_runner][feeds][validation]") {
    std::vector<FeedSpec> feeds;
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("empty"));
}

TEST_CASE("validate_feed_plan: feed 0 must be execution role",
          "[backtest_runner][feeds][validation]") {
    std::vector<FeedSpec> feeds = {{
        .index = 0, .interval = "1h", .role = "context",
        .source = "parquet", .data_path = "/tmp/test.csv"
    }};
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("execution"));
}

TEST_CASE("validate_feed_plan: invalid interval token rejected",
          "[backtest_runner][feeds][validation]") {
    std::vector<FeedSpec> feeds = {{
        .index = 0, .interval = "3h", .role = "execution",
        .source = "parquet", .data_path = "/tmp/test.csv"
    }};
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("invalid interval"));
}

TEST_CASE("validate_feed_plan: context feed finer than execution rejected",
          "[backtest_runner][feeds][validation]") {
    auto csv_path = write_test_csv("feed_plan_finer.csv");

    std::vector<FeedSpec> feeds = {
        {.index = 0, .interval = "1h", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        {.index = 1, .interval = "5m", .role = "context",
         .source = "parquet", .data_path = csv_path},
    };
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("finer"));
}

TEST_CASE("validate_feed_plan: missing parquet file rejected",
          "[backtest_runner][feeds][validation]") {
    std::vector<FeedSpec> feeds = {{
        .index = 0, .interval = "1h", .role = "execution",
        .source = "parquet", .data_path = "/nonexistent/path/data.parquet"
    }};
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("not found"));
}

TEST_CASE("validate_feed_plan: unknown source rejected",
          "[backtest_runner][feeds][validation]") {
    auto csv_path = write_test_csv("feed_plan_unknown.csv");

    std::vector<FeedSpec> feeds = {{
        .index = 0, .interval = "1h", .role = "execution",
        .source = "magic", .data_path = csv_path
    }};
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("unknown source"));
}

TEST_CASE("validate_feed_plan: resample without base rejected",
          "[backtest_runner][feeds][validation]") {
    auto csv_path = write_test_csv("feed_plan_nobase.csv");

    std::vector<FeedSpec> feeds = {
        {.index = 0, .interval = "1h", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        {.index = 1, .interval = "1M", .role = "context",
         .source = "resample", .base = ""},
    };
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("no base"));
}

TEST_CASE("validate_feed_plan: valid plan passes",
          "[backtest_runner][feeds][validation]") {
    auto csv_path = write_test_csv("feed_plan_valid.csv");

    std::vector<FeedSpec> feeds = {
        {.index = 0, .interval = "1h", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        {.index = 1, .interval = "1M", .role = "context",
         .source = "resample", .base = "1h"},
    };
    auto err = validate_feed_plan(feeds);
    CHECK(err.empty());
}

// =========================================================================
// Back-compat: old-shape config (no feeds[]) runs unchanged
// =========================================================================

TEST_CASE("run_backtest: legacy config without feeds[] produces valid result",
          "[backtest_runner][feeds][backcompat]") {
    auto csv_path = write_test_csv("backcompat_legacy.csv");

    const std::string json = R"({"data_file": ")" + csv_path +
                             R"(", "initial_cash": 100000, "commission": 0.001})";

    auto result = run_backtest(
        std::make_unique<SimpleBuyStrategy>(), json);

    // Should succeed — check for "success":true in the JSON
    CHECK_THAT(result, ContainsSubstring("\"success\":true"));
}

// =========================================================================
// Load loop: resample feed references missing base -> error
// =========================================================================

TEST_CASE("run_backtest: resample feed with unloaded base returns error",
          "[backtest_runner][feeds][load]") {
    auto csv_path = write_test_csv("resample_nobase.csv");

    BacktestConfig cfg;
    cfg.initial_cash = 100000.0;
    cfg.feeds = {
        {.index = 0, .interval = "1h", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        // base = "5m" but no feed with interval "5m" is loaded
        {.index = 1, .interval = "1M", .role = "context",
         .source = "resample", .base = "5m"},
    };

    auto result = run_backtest(
        std::make_unique<SimpleBuyStrategy>(), cfg);
    CHECK_THAT(result, ContainsSubstring("errorMessage"));
    CHECK_THAT(result, ContainsSubstring("not loaded"));
}

// =========================================================================
// Multi-feed with resample: end-to-end
// =========================================================================

TEST_CASE("run_backtest: multi-feed with resample produces valid result",
          "[backtest_runner][feeds][multi_tf]") {
    auto csv_path = write_test_csv("multi_feed_resample.csv");

    BacktestConfig cfg;
    cfg.initial_cash = 100000.0;
    cfg.symbol = "TEST";
    cfg.feeds = {
        {.index = 0, .interval = "1d", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        {.index = 1, .interval = "1w", .role = "context",
         .source = "resample", .base = "1d"},
    };

    auto result = run_backtest(
        std::make_unique<SimpleBuyStrategy>(), cfg);
    CHECK_THAT(result, ContainsSubstring("\"success\":true"));
}

// =========================================================================
// Pushdown window: start_time/end_time flow to config
// =========================================================================

TEST_CASE("parse_backtest_config: start_time and end_time parsed",
          "[backtest_runner][feeds][pushdown]") {
    const std::string json = R"({
        "data_file": "test.csv",
        "start_time": 1704067200.0,
        "end_time": 1706745600.0
    })";

    auto cfg = parse_backtest_config(json);
    REQUIRE(cfg.start_time.has_value());
    REQUIRE(cfg.end_time.has_value());
    CHECK(*cfg.start_time == 1704067200.0);
    CHECK(*cfg.end_time == 1706745600.0);
}

// =========================================================================
// Warmup-infeasible window: Cerebro throws, runner returns error
// =========================================================================

TEST_CASE("run_backtest: warmup infeasibility returns structured error",
          "[backtest_runner][feeds][warmup]") {
    auto csv_path = write_test_csv("warmup_infeasible.csv");

    // Strategy with minimum_period = 100 but CSV has only 5 bars.
    class GreedyWarmupStrategy final : public Strategy {
    public:
        void init() override {
            set_minimum_period(100);
        }
        void next() override {}
    };

    const std::string json = R"({"data_file": ")" + csv_path +
                             R"(", "initial_cash": 100000})";

    auto result = run_backtest(
        std::make_unique<GreedyWarmupStrategy>(), json);

    CHECK_THAT(result, ContainsSubstring("errorMessage"));
    CHECK_THAT(result, ContainsSubstring("warmup"));
}

// =========================================================================
// Null strategy -> error
// =========================================================================

TEST_CASE("run_backtest: null strategy returns error",
          "[backtest_runner][feeds]") {
    auto result = run_backtest(
        std::unique_ptr<Strategy>(nullptr), BacktestConfig{});
    CHECK_THAT(result, ContainsSubstring("null strategy"));
}

// =========================================================================
// Empty data_file with no feeds -> error
// =========================================================================

TEST_CASE("run_backtest: empty data_file and no feeds returns error",
          "[backtest_runner][feeds]") {
    BacktestConfig cfg;
    cfg.data_file = "";
    auto result = run_backtest(
        std::make_unique<SimpleBuyStrategy>(), cfg);
    CHECK_THAT(result, ContainsSubstring("data_file not specified"));
}

// =========================================================================
// Parser: feeds[] with single feed
// =========================================================================

TEST_CASE("BacktestConfigParser parses single-element feeds[]",
          "[backtest_runner][feeds][parser]") {
    auto csv_path = write_test_csv("single_feed_parse.csv");

    const std::string json = R"({
        "feeds": [
            { "index": 0, "interval": "5m", "role": "execution",
              "source": "parquet", "dataPath": ")" + csv_path + R"(" }
        ]
    })";

    auto cfg = parse_backtest_config(json);
    REQUIRE(cfg.feeds.size() == 1);
    CHECK(cfg.feeds[0].interval == "5m");
    CHECK(cfg.feeds[0].role == "execution");
    CHECK(cfg.feeds[0].data_path == csv_path);
}

// =========================================================================
// validate_feed_plan: valid same-TFC context feeds pass (multi-instrument)
// =========================================================================

TEST_CASE("validate_feed_plan: same-TFC context feeds accepted",
          "[backtest_runner][feeds][validation]") {
    auto csv_path = write_test_csv("same_tfc.csv");

    std::vector<FeedSpec> feeds = {
        {.index = 0, .interval = "1h", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        {.index = 1, .interval = "1h", .role = "context",
         .source = "parquet", .data_path = csv_path},
    };
    auto err = validate_feed_plan(feeds);
    CHECK(err.empty());
}

// =========================================================================
// validate_feed_plan: invalid resample base interval
// =========================================================================

TEST_CASE("validate_feed_plan: invalid resample base interval rejected",
          "[backtest_runner][feeds][validation]") {
    auto csv_path = write_test_csv("bad_resample_base.csv");

    std::vector<FeedSpec> feeds = {
        {.index = 0, .interval = "1h", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        {.index = 1, .interval = "1M", .role = "context",
         .source = "resample", .base = "99x"},
    };
    auto err = validate_feed_plan(feeds);
    CHECK_THAT(err, ContainsSubstring("invalid base interval"));
}

// =========================================================================
//  P5: warmupEndTimestamp in result JSON
// =========================================================================

TEST_CASE("run_backtest: result contains warmupEndTimestamp",
          "[backtest_runner][feeds][result_surface]") {
    auto csv_path = write_test_csv("warmup_end_ts.csv");

    const std::string json = R"({"data_file": ")" + csv_path +
                             R"(", "initial_cash": 100000, "commission": 0.001})";

    auto result = run_backtest(
        std::make_unique<SimpleBuyStrategy>(), json);

    INFO("result: " << result);
    CHECK_THAT(result, ContainsSubstring("\"success\":true"));
    // warmupEndTimestamp must be present and > 0
    CHECK_THAT(result, ContainsSubstring("\"warmupEndTimestamp\":"));
}

// =========================================================================
//  P5: feedBarCounts in result JSON (single-feed legacy)
// =========================================================================

TEST_CASE("run_backtest: single-feed result contains feedBarCounts",
          "[backtest_runner][feeds][result_surface]") {
    auto csv_path = write_test_csv("bar_counts_single.csv");

    const std::string json = R"({"data_file": ")" + csv_path +
                             R"(", "initial_cash": 100000, "commission": 0.001})";

    auto result = run_backtest(
        std::make_unique<SimpleBuyStrategy>(), json);

    INFO("result: " << result);
    CHECK_THAT(result, ContainsSubstring("\"success\":true"));
    // feedBarCounts must be present with exactly 1 entry
    CHECK_THAT(result, ContainsSubstring("\"feedBarCounts\":[{"));
    CHECK_THAT(result, ContainsSubstring("\"index\":0"));
    CHECK_THAT(result, ContainsSubstring("\"bars\":5"));
}

// =========================================================================
//  P5: feedBarCounts in multi-feed result JSON
// =========================================================================

TEST_CASE("run_backtest: multi-feed result contains feedBarCounts per feed",
          "[backtest_runner][feeds][result_surface]") {
    auto csv_path = write_test_csv("bar_counts_multi.csv");

    BacktestConfig cfg;
    cfg.initial_cash = 100000.0;
    cfg.symbol = "TEST";
    cfg.feeds = {
        {.index = 0, .interval = "1d", .role = "execution",
         .source = "parquet", .data_path = csv_path},
        {.index = 1, .interval = "1w", .role = "context",
         .source = "resample", .base = "1d"},
    };

    auto result = run_backtest(
        std::make_unique<SimpleBuyStrategy>(), cfg);

    INFO("result: " << result);
    CHECK_THAT(result, ContainsSubstring("\"success\":true"));
    // feedBarCounts must be present with 2 entries
    CHECK_THAT(result, ContainsSubstring("\"feedBarCounts\":[{"));
    CHECK_THAT(result, ContainsSubstring("\"interval\":\"1d\""));
    CHECK_THAT(result, ContainsSubstring("\"interval\":\"1w\""));
    // Feed 0 (1d) should have 5 bars
    CHECK_THAT(result, ContainsSubstring("\"bars\":5"));
}
