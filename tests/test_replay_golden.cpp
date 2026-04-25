#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/data/replay.hpp>

#include <chrono>
#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;

namespace {

class MinuteFeed final : public DataFeed {
public:
    struct Bar {
        int minutes_offset; // minutes from base time
        double open;
        double high;
        double low;
        double close;
        double volume;
    };

    explicit MinuteFeed(std::vector<Bar> bars) : bars_(std::move(bars)) {}

    [[nodiscard]] bool load() override {
        if (loaded_) return false;
        const auto base = std::chrono::sys_days{std::chrono::January / 2 / 2006};
        for (const auto& bar : bars_) {
            auto dt = DateTime(base + std::chrono::minutes(bar.minutes_offset));
            datetime().forward(dt);
            open().forward(bar.open);
            high().forward(bar.high);
            low().forward(bar.low);
            close().forward(bar.close);
            volume().forward(bar.volume);
            openinterest().forward(0.0);
        }
        datetime().home();
        open().home();
        high().home();
        low().home();
        close().home();
        volume().home();
        openinterest().home();
        loaded_ = true;
        return !bars_.empty();
    }

private:
    std::vector<Bar> bars_;
    bool loaded_ = false;
};

} // namespace

TEST_CASE("DataReplay produces one output per source bar", "[data][replay]") {
    // 6 x 5-min bars to be replayed into 15-min bars
    auto feed = std::make_unique<MinuteFeed>(std::vector<MinuteFeed::Bar>{
        {0,  100.0, 101.0, 99.0,  100.5, 1000.0},  // 00:00
        {5,  100.5, 102.0, 100.0, 101.0, 1100.0},  // 00:05
        {10, 101.0, 103.0, 100.5, 102.0, 900.0},   // 00:10
        {15, 102.0, 104.0, 101.5, 103.0, 1200.0},  // 00:15 (new period)
        {20, 103.0, 105.0, 102.5, 104.0, 800.0},   // 00:20
        {25, 104.0, 106.0, 103.5, 105.0, 1000.0},  // 00:25
    });

    DataReplay replay(*feed, TimeFrame::Minutes, 15);
    replay.preload();

    // Same number of output bars as source bars
    REQUIRE(replay.size() == 6);
}

TEST_CASE("DataReplay progressively builds higher-TF bars", "[data][replay]") {
    auto feed = std::make_unique<MinuteFeed>(std::vector<MinuteFeed::Bar>{
        {0,  100.0, 101.0, 99.0,  100.5, 1000.0},  // 00:00
        {5,  100.5, 102.0, 100.0, 101.0, 1100.0},  // 00:05
        {10, 101.0, 103.0, 100.5, 102.0, 900.0},   // 00:10
        {15, 102.0, 104.0, 101.5, 103.0, 1200.0},  // 00:15 (new period)
        {20, 103.0, 105.0, 102.5, 104.0, 800.0},   // 00:20
        {25, 104.0, 106.0, 103.5, 105.0, 1000.0},  // 00:25
    });

    DataReplay replay(*feed, TimeFrame::Minutes, 15);
    replay.preload();

    // First period (bars 0-2): open stays at first bar, high/low expand, close updates
    // Bar 0: o=100, h=101, l=99, c=100.5, v=1000
    REQUIRE_THAT(replay.open().data()[0], WithinRel(100.0, 1e-9));
    REQUIRE_THAT(replay.high().data()[0], WithinRel(101.0, 1e-9));
    REQUIRE_THAT(replay.low().data()[0], WithinRel(99.0, 1e-9));
    REQUIRE_THAT(replay.close().data()[0], WithinRel(100.5, 1e-9));
    REQUIRE_THAT(replay.volume().data()[0], WithinRel(1000.0, 1e-9));

    // Bar 1: o=100, h=max(101,102)=102, l=min(99,100)=99, c=101, v=1000+1100=2100
    REQUIRE_THAT(replay.open().data()[1], WithinRel(100.0, 1e-9));
    REQUIRE_THAT(replay.high().data()[1], WithinRel(102.0, 1e-9));
    REQUIRE_THAT(replay.low().data()[1], WithinRel(99.0, 1e-9));
    REQUIRE_THAT(replay.close().data()[1], WithinRel(101.0, 1e-9));
    REQUIRE_THAT(replay.volume().data()[1], WithinRel(2100.0, 1e-9));

    // Bar 2: o=100, h=max(102,103)=103, l=min(99,100.5)=99, c=102, v=3000
    REQUIRE_THAT(replay.open().data()[2], WithinRel(100.0, 1e-9));
    REQUIRE_THAT(replay.high().data()[2], WithinRel(103.0, 1e-9));
    REQUIRE_THAT(replay.low().data()[2], WithinRel(99.0, 1e-9));
    REQUIRE_THAT(replay.close().data()[2], WithinRel(102.0, 1e-9));
    REQUIRE_THAT(replay.volume().data()[2], WithinRel(3000.0, 1e-9));

    // Second period starts (bar 3): resets to new bar
    REQUIRE_THAT(replay.open().data()[3], WithinRel(102.0, 1e-9));
    REQUIRE_THAT(replay.high().data()[3], WithinRel(104.0, 1e-9));
    REQUIRE_THAT(replay.low().data()[3], WithinRel(101.5, 1e-9));
    REQUIRE_THAT(replay.close().data()[3], WithinRel(103.0, 1e-9));
    REQUIRE_THAT(replay.volume().data()[3], WithinRel(1200.0, 1e-9));

    // Bar 4: second period accumulates
    REQUIRE_THAT(replay.open().data()[4], WithinRel(102.0, 1e-9));
    REQUIRE_THAT(replay.high().data()[4], WithinRel(105.0, 1e-9));
    REQUIRE_THAT(replay.low().data()[4], WithinRel(101.5, 1e-9));
    REQUIRE_THAT(replay.close().data()[4], WithinRel(104.0, 1e-9));
    REQUIRE_THAT(replay.volume().data()[4], WithinRel(2000.0, 1e-9));
}

TEST_CASE("DataReplay is_period_end identifies boundaries", "[data][replay]") {
    auto feed = std::make_unique<MinuteFeed>(std::vector<MinuteFeed::Bar>{
        {0,  100.0, 101.0, 99.0, 100.0, 1000.0},
        {5,  100.0, 101.0, 99.0, 100.0, 1000.0},
        {10, 100.0, 101.0, 99.0, 100.0, 1000.0},
        {15, 100.0, 101.0, 99.0, 100.0, 1000.0},  // new period
        {20, 100.0, 101.0, 99.0, 100.0, 1000.0},
    });

    DataReplay replay(*feed, TimeFrame::Minutes, 15);
    replay.preload();

    REQUIRE(!replay.is_period_end(0));
    REQUIRE(!replay.is_period_end(1));
    REQUIRE(replay.is_period_end(2));   // Last bar of first 15-min period
    REQUIRE(!replay.is_period_end(3));
    REQUIRE(replay.is_period_end(4));   // Last bar (end of data)
}
