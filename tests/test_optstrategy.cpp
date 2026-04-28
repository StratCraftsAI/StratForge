#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/engine/optimizer.hpp>

#include "test_helpers.hpp"

#include <memory>
#include <vector>

using namespace stratforge;
using Catch::Matchers::WithinRel;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

class SimpleMAStrategy : public Strategy {
public:
    [[nodiscard]] ParamMap default_params() const override {
        return {{"period", std::int64_t{3}}};
    }

    void next() override {
        int period = param<int>("period");
        std::size_t p = static_cast<std::size_t>(period);

        if (data().index() + 1 < p) return;

        // Simple moving average
        double sum = 0.0;
        for (std::size_t i = 0; i < p; ++i) {
            sum += data().close()[-static_cast<int>(i)];
        }
        double ma = sum / static_cast<double>(p);

        if (position().is_flat() && data().close()[0] > ma) {
            (void)buy(1.0);
        } else if (!position().is_flat() && data().close()[0] < ma) {
            (void)close();
        }
    }
};

} // namespace

TEST_CASE("Parameter grid generation produces Cartesian product", "[optimizer][grid]") {
    std::vector<ParamRange> ranges = {
        {"period", {ParamValue{std::int64_t{2}}, ParamValue{std::int64_t{3}}, ParamValue{std::int64_t{4}}}},
        {"fast", {ParamValue{true}, ParamValue{false}}},
    };

    auto grid = generate_param_grid(ranges);
    REQUIRE(grid.size() == 6); // 3 * 2

    // Verify all combinations exist
    int count_period_2 = 0;
    for (const auto& m : grid) {
        if (std::get<std::int64_t>(m.at("period")) == 2) count_period_2++;
    }
    REQUIRE(count_period_2 == 2); // 2 values for "fast"
}

TEST_CASE("Optimizer runs strategy with different parameters", "[optimizer]") {
    // Create source data (trending up then down)
    auto feed = std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {101.0, 103.0, 100.0, 102.0},
        {102.0, 105.0, 101.0, 104.0},
        {104.0, 106.0, 103.0, 105.0},
        {105.0, 107.0, 104.0, 106.0},
        {106.0, 107.0, 104.0, 103.0},
        {103.0, 104.0, 100.0, 101.0},
        {101.0, 102.0, 98.0, 99.0},
    });

    std::vector<DataFeed*> feeds = {feed.get()};

    Optimizer::Config config;
    config.cash = 10000.0;
    config.max_threads = 2;

    Optimizer optimizer(config);

    std::vector<ParamRange> ranges = {
        {"period", {ParamValue{std::int64_t{2}}, ParamValue{std::int64_t{3}}, ParamValue{std::int64_t{4}}}},
    };

    auto factory = [](const ParamMap&) -> std::unique_ptr<Strategy> {
        return std::make_unique<SimpleMAStrategy>();
    };

    auto extractor = [](const Cerebro& cerebro, [[maybe_unused]] const ParamMap& params) -> OptResult {
        OptResult result;
        result.final_value = cerebro.broker().portfolio_value({});
        return result;
    };

    auto results = optimizer.run(feeds, factory, ranges, extractor);

    REQUIRE(results.size() == 3);

    // All results should have non-zero final value (at least the initial cash)
    for (const auto& r : results) {
        REQUIRE(r.final_value > 0.0);
    }

    // Verify params were set correctly
    for (const auto& r : results) {
        REQUIRE(r.params.contains("period"));
    }
}

TEST_CASE("Optimizer with single-threaded execution", "[optimizer]") {
    auto feed = std::make_unique<StaticFeed>(std::vector<StaticFeed::Bar>{
        {100.0, 101.0, 99.0, 100.0},
        {101.0, 102.0, 100.0, 101.0},
        {102.0, 103.0, 101.0, 102.0},
    });

    std::vector<DataFeed*> feeds = {feed.get()};

    Optimizer::Config config;
    config.cash = 5000.0;
    config.max_threads = 1;

    Optimizer optimizer(config);

    std::vector<ParamRange> ranges = {
        {"period", {ParamValue{std::int64_t{2}}, ParamValue{std::int64_t{3}}}},
    };

    auto factory = [](const ParamMap&) -> std::unique_ptr<Strategy> {
        return std::make_unique<SimpleMAStrategy>();
    };

    auto extractor = [](const Cerebro& cerebro, const ParamMap&) -> OptResult {
        OptResult result;
        result.final_value = cerebro.broker().cash();
        return result;
    };

    auto results = optimizer.run(feeds, factory, ranges, extractor);
    REQUIRE(results.size() == 2);
}
