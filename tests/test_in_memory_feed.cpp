// SPDX-License-Identifier: MIT
//
// tests/test_in_memory_feed.cpp — stratforge::InMemoryFeed acceptance suite.
//
//  P1 acceptance tests. Tag form [data][in_memory][regression].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/bar.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/data/in_memory_feed.hpp>

#include "test_helpers.hpp"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <span>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

using stratforge::Bar;
using stratforge::DateTime;

inline DateTime epoch_plus_days(int days) {
    return DateTime{} + std::chrono::hours(24 * days);
}

inline std::vector<Bar> make_bars(std::size_t n) {
    std::vector<Bar> bars;
    bars.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        bars.push_back(Bar{
            epoch_plus_days(static_cast<int>(i)),
            100.0 + static_cast<double>(i),
            105.0 + static_cast<double>(i),
            99.0  + static_cast<double>(i),
            103.0 + static_cast<double>(i),
            1000.0 + static_cast<double>(i),
        });
    }
    return bars;
}

}  // namespace

TEST_CASE("InMemoryFeed preload populates lines bar-by-bar",
          "[data][in_memory][regression]") {
    const auto bars = make_bars(5);
    stratforge::InMemoryFeed feed(std::span<const Bar>{bars});

    feed.preload();

    REQUIRE(feed.size() == bars.size());
    REQUIRE(feed.index() == 0);

    for (std::size_t i = 0; i < bars.size(); ++i) {
        INFO("bar index: " << i);
        CHECK(feed.datetime().data()[i]    == bars[i].timestamp);
        CHECK_THAT(feed.open().data()[i],   WithinAbs(bars[i].open,   1e-12));
        CHECK_THAT(feed.high().data()[i],   WithinAbs(bars[i].high,   1e-12));
        CHECK_THAT(feed.low().data()[i],    WithinAbs(bars[i].low,    1e-12));
        CHECK_THAT(feed.close().data()[i],  WithinAbs(bars[i].close,  1e-12));
        CHECK_THAT(feed.volume().data()[i], WithinAbs(bars[i].volume, 1e-12));
        CHECK_THAT(feed.openinterest().data()[i], WithinAbs(0.0, 1e-12));
    }
}

TEST_CASE("InMemoryFeed empty span loads cleanly",
          "[data][in_memory][regression]") {
    stratforge::InMemoryFeed feed(std::span<const Bar>{});

    REQUIRE_FALSE(feed.load());
    REQUIRE(feed.size() == 0);

    feed.preload();
    REQUIRE(feed.size() == 0);
}

TEST_CASE("InMemoryFeed reset rebinds to a new span",
          "[data][in_memory][regression]") {
    const auto first  = make_bars(3);
    const auto second = make_bars(7);

    stratforge::InMemoryFeed feed(std::span<const Bar>{first});
    feed.preload();
    REQUIRE(feed.size() == first.size());

    feed.reset(std::span<const Bar>{second});
    REQUIRE(feed.size() == 0);

    feed.preload();
    REQUIRE(feed.size() == second.size());
    REQUIRE(feed.index() == 0);

    for (std::size_t i = 0; i < second.size(); ++i) {
        INFO("bar index: " << i);
        CHECK_THAT(feed.close().data()[i], WithinAbs(second[i].close, 1e-12));
    }
}

TEST_CASE("InMemoryFeed clone produces independent feed sharing the same span",
          "[data][in_memory][regression]") {
    const auto bars = make_bars(4);
    stratforge::InMemoryFeed feed(std::span<const Bar>{bars});
    feed.preload();

    auto cloned = feed.clone();
    REQUIRE(cloned != nullptr);
    REQUIRE(cloned->size() == 0);   // clone's Lines are empty until preload()

    cloned->preload();
    REQUIRE(cloned->size() == bars.size());

    // Advancing the clone must not move the original's cursor.
    const auto original_idx_before = feed.index();
    cloned->advance();
    CHECK(feed.index() == original_idx_before);
    CHECK(cloned->index() == 1);

    // Values match the same source span.
    for (std::size_t i = 0; i < bars.size(); ++i) {
        INFO("bar index: " << i);
        CHECK_THAT(cloned->close().data()[i], WithinAbs(bars[i].close, 1e-12));
    }
}

TEST_CASE("InMemoryFeed parity vs CsvData",
          "[data][in_memory][regression][integration]") {
    // Build the same OHLCV series via a tmp CSV and via InMemoryFeed; preload
    // both and verify per-bar OHLCV equality. Volume is intentionally
    // non-trivial so the assertion is non-degenerate.
    const std::vector<std::vector<double>> rows = {
        // date_index_days, o, h, l, c, v
        {0, 100.0, 105.0,  99.0, 103.0, 1000.0},
        {1, 103.5, 107.0, 102.0, 106.0, 1100.0},
        {2, 106.0, 108.5, 104.0, 105.5, 1050.0},
        {3, 105.5, 109.0, 103.5, 108.0, 1200.0},
    };

    std::string csv_content = "Date,Open,High,Low,Close,Volume\n";
    for (const auto& r : rows) {
        const auto dt = epoch_plus_days(static_cast<int>(r[0]));
        const auto tt = std::chrono::system_clock::to_time_t(dt);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        csv_content += buf;
        for (std::size_t i = 1; i < r.size(); ++i) {
            csv_content += ',';
            csv_content += std::to_string(r[i]);
        }
        csv_content += '\n';
    }

    const auto csv_path = stratforge::test::write_csv(
        "in_memory_parity.csv", csv_content, "stratforge_t193_");

    stratforge::CsvData::Params params;
    params.filename     = csv_path;
    params.has_headers  = true;
    params.date_format  = "%Y-%m-%d";
    stratforge::CsvData csv_feed(params);
    REQUIRE(csv_feed.load());

    std::vector<Bar> bars;
    bars.reserve(rows.size());
    for (const auto& r : rows) {
        bars.push_back(Bar{
            epoch_plus_days(static_cast<int>(r[0])),
            r[1], r[2], r[3], r[4], r[5],
        });
    }
    stratforge::InMemoryFeed mem_feed(std::span<const Bar>{bars});
    mem_feed.preload();

    REQUIRE(csv_feed.size() == mem_feed.size());
    REQUIRE(csv_feed.size() == rows.size());

    for (std::size_t i = 0; i < rows.size(); ++i) {
        INFO("bar index: " << i);
        CHECK(csv_feed.datetime().data()[i] == mem_feed.datetime().data()[i]);
        CHECK_THAT(csv_feed.open().data()[i],   WithinAbs(mem_feed.open().data()[i],   1e-6));
        CHECK_THAT(csv_feed.high().data()[i],   WithinAbs(mem_feed.high().data()[i],   1e-6));
        CHECK_THAT(csv_feed.low().data()[i],    WithinAbs(mem_feed.low().data()[i],    1e-6));
        CHECK_THAT(csv_feed.close().data()[i],  WithinAbs(mem_feed.close().data()[i],  1e-6));
        CHECK_THAT(csv_feed.volume().data()[i], WithinAbs(mem_feed.volume().data()[i], 1e-6));
    }
}
