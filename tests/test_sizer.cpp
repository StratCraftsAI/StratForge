#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/broker/sizer.hpp>

#include <chrono>
#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;

namespace {

class StaticFeed final : public DataFeed {
public:
    struct Bar {
        double open;
        double high;
        double low;
        double close;
    };

    explicit StaticFeed(std::vector<Bar> bars) : bars_(std::move(bars)) {}

    [[nodiscard]] bool load() override {
        if (loaded_) return false;
        const auto base = std::chrono::system_clock::time_point{};
        for (std::size_t i = 0; i < bars_.size(); ++i) {
            const auto& bar = bars_[i];
            datetime().forward(base + std::chrono::hours(24 * static_cast<int>(i)));
            open().forward(bar.open);
            high().forward(bar.high);
            low().forward(bar.low);
            close().forward(bar.close);
            volume().forward(1000.0);
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

class SizerStrategy : public Strategy {
public:
    std::vector<double> sizes;

    void next() override {
        // buy() without size uses sizer
        auto order_id = buy();
        for (const auto& order : broker().orders()) {
            if (order.id == order_id) {
                sizes.push_back(order.size);
                break;
            }
        }
    }
};

class SellSizerStrategy : public Strategy {
public:
    std::vector<double> sizes;

    void next() override {
        if (position().is_flat()) {
            (void)buy(10.0);
        } else {
            auto order_id = sell();
            for (const auto& order : broker().orders()) {
                if (order.id == order_id) {
                    sizes.push_back(order.size);
                    break;
                }
            }
        }
    }
};

} // namespace

TEST_CASE("FixedSize sizer returns constant size", "[sizer][fixed]") {
    Cerebro cerebro;
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SizerStrategy>();
    strategy.setsizer(std::make_unique<FixedSize>(5.0));

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(5.0, 1e-12));
}

TEST_CASE("PercentSizer returns size based on portfolio value", "[sizer][percent]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SizerStrategy>();
    strategy.setsizer(std::make_unique<PercentSizer>(10.0)); // 10%

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    // Value = 10000. 10% = 1000. Price = 100. Size = 10.
    REQUIRE_THAT(strategy.sizes[0], WithinRel(10.0, 1e-12));
}

TEST_CASE("AllInSizer uses all available cash for buy", "[sizer][allin]") {
    Cerebro cerebro;
    cerebro.set_cash(1000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SizerStrategy>();
    strategy.setsizer(std::make_unique<AllInSizer>());

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(10.0, 1e-12)); // 1000 / 100 = 10
}

TEST_CASE("AllInSizer uses position size for sell", "[sizer][allin]") {
    Cerebro cerebro;
    cerebro.set_cash(2000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<SellSizerStrategy>();
    strategy.setsizer(std::make_unique<AllInSizer>());

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(10.0, 1e-12));
}

namespace {

class ReverserStrategy : public Strategy {
public:
    std::vector<double> sizes;

    void next() override {
        // First bar: buy (flat -> base_size)
        // Second bar: sell to reverse (position + base_size)
        if (position().is_flat()) {
            auto order_id = buy();
            for (const auto& order : broker().orders()) {
                if (order.id == order_id) {
                    sizes.push_back(order.size);
                    break;
                }
            }
        } else {
            auto order_id = sell();
            for (const auto& order : broker().orders()) {
                if (order.id == order_id) {
                    sizes.push_back(order.size);
                    break;
                }
            }
        }
    }
};

} // namespace

TEST_CASE("FixedReverser returns base_size when flat", "[sizer][reverser]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<ReverserStrategy>();
    strategy.setsizer(std::make_unique<FixedReverser>(5.0));

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 1);
    REQUIRE_THAT(strategy.sizes[0], WithinRel(5.0, 1e-12));
}

TEST_CASE("FixedReverser returns position + base_size when reversing", "[sizer][reverser]") {
    Cerebro cerebro;
    cerebro.set_cash(10000.0);
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }));

    auto& strategy = cerebro.add_strategy<ReverserStrategy>();
    strategy.setsizer(std::make_unique<FixedReverser>(5.0));

    cerebro.run();

    REQUIRE(strategy.sizes.size() == 2);
    // First bar: flat, returns base_size = 5
    REQUIRE_THAT(strategy.sizes[0], WithinRel(5.0, 1e-12));
    // Second bar: position is 5, returns 5 + 5 = 10
    REQUIRE_THAT(strategy.sizes[1], WithinRel(10.0, 1e-12));
}
