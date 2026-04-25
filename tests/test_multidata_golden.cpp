#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/cerebro.hpp>

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

/// Simple pair trading strategy: when spread > threshold, go short asset0 / long asset1
class PairTradeStrategy : public Strategy {
public:
    double spread_threshold = 10.0;
    std::vector<double> spreads;
    int trades_opened = 0;
    int trades_closed = 0;

    void next() override {
        double price0 = data(0).close()[0];
        double price1 = data(1).close()[0];
        double spread = price0 - price1;
        spreads.push_back(spread);

        bool flat0 = position(0).is_flat();
        bool flat1 = position(1).is_flat();

        if (flat0 && flat1) {
            if (spread > spread_threshold) {
                // Spread too wide: short asset0, long asset1
                (void)sell(1.0, 0.0, OrderType::Market, 0);
                (void)buy(1.0, 0.0, OrderType::Market, 1);
                trades_opened++;
            }
        } else {
            if (spread <= 0.0) {
                // Spread collapsed: close both
                (void)close(0);
                (void)close(1);
                trades_closed++;
            }
        }
    }
};

} // namespace

TEST_CASE("Multi-data strategy accesses both feeds correctly", "[multidata]") {
    Cerebro cerebro;
    cerebro.set_cash(100000.0);

    // Asset 0: starts high, converges
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {120.0, 121.0, 119.0, 120.0},
        {118.0, 119.0, 117.0, 118.0},
        {115.0, 116.0, 114.0, 115.0},
        {110.0, 111.0, 109.0, 110.0},
        {105.0, 106.0, 104.0, 105.0},
    }), "ASSET_A");

    // Asset 1: starts lower, converges
    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {102.0, 103.0, 101.0, 102.0},
        {104.0, 105.0, 103.0, 104.0},
        {106.0, 107.0, 105.0, 106.0},
        {108.0, 109.0, 107.0, 108.0},
    }), "ASSET_B");

    auto& strategy = cerebro.add_strategy<PairTradeStrategy>();
    strategy.spread_threshold = 10.0;
    cerebro.run();

    // Verify spread computation
    REQUIRE(strategy.spreads.size() == 5);
    REQUIRE_THAT(strategy.spreads[0], WithinRel(20.0, 1e-9));  // 120-100
    REQUIRE_THAT(strategy.spreads[1], WithinRel(16.0, 1e-9));  // 118-102
    REQUIRE_THAT(strategy.spreads[4], WithinRel(-3.0, 1e-9));  // 105-108

    // Bar 0: spread=20 > 10 → open pair trade
    REQUIRE(strategy.trades_opened >= 1);
}

TEST_CASE("Multi-data strategy positions are independent per feed", "[multidata]") {
    Cerebro cerebro;
    cerebro.set_cash(100000.0);

    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {100.0, 101.0, 99.0, 100.0},
    }), "FEED_0");

    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {50.0, 51.0, 49.0, 50.0},
        {50.0, 51.0, 49.0, 50.0},
    }), "FEED_1");

    struct IndependentStrategy : public Strategy {
        void next() override {
            if (data().index() == 0) {
                (void)buy(5.0, 0.0, OrderType::Market, 0);
                (void)buy(10.0, 0.0, OrderType::Market, 1);
            }
        }
    };

    (void)cerebro.add_strategy<IndependentStrategy>();
    cerebro.run();

    // Positions should be tracked independently
    REQUIRE_THAT(cerebro.broker().position(0).size, WithinRel(5.0, 1e-9));
    REQUIRE_THAT(cerebro.broker().position(1).size, WithinRel(10.0, 1e-9));
}

TEST_CASE("Multi-data strategy can access feeds by name", "[multidata]") {
    Cerebro cerebro;
    cerebro.set_cash(100000.0);

    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
    }), "SPY");

    cerebro.add_data(std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {200.0, 201.0, 199.0, 200.0},
    }), "QQQ");

    struct NamedAccessStrategy : public Strategy {
        double spy_close = 0.0;
        double qqq_close = 0.0;
        bool has_spy = false;
        bool has_qqq = false;

        void next() override {
            has_spy = has_data("SPY");
            has_qqq = has_data("QQQ");
            spy_close = data("SPY").close()[0];
            qqq_close = data("QQQ").close()[0];
        }
    };

    auto& strategy = cerebro.add_strategy<NamedAccessStrategy>();
    cerebro.run();

    REQUIRE(strategy.has_spy);
    REQUIRE(strategy.has_qqq);
    REQUIRE_THAT(strategy.spy_close, WithinRel(100.0, 1e-9));
    REQUIRE_THAT(strategy.qqq_close, WithinRel(200.0, 1e-9));
}
