#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/pattern_context.hpp>
#include <stratforge/strategy/signal_entry_strategy.hpp>

#include "test_helpers.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace {

using stratforge::DateTime;
using TestFeed = stratforge::test::InMemoryFeed;

DateTime at_hour(int hour) {
    return DateTime{} + std::chrono::hours(hour);
}

std::vector<TestFeed::Bar> hourly_bars(std::size_t count) {
    std::vector<TestFeed::Bar> bars;
    bars.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double price = 100.0 + static_cast<double>(i);
        bars.push_back({at_hour(static_cast<int>(i)), price, price + 1.0,
                        price - 1.0, price, 1000.0});
    }
    return bars;
}

class AlignedTrendStrategy final : public stratforge::SignalEntryStrategy {
public:
    struct Sample {
        DateTime execution_open;
        DateTime context_open;
        bool context_available;
        bool context_advanced;
        double aligned_trend;
        double alignment;
    };

    std::vector<Sample> samples;
    std::size_t execution_updates = 0;
    std::size_t context_updates = 0;
    std::size_t decisions = 0;

    void initialize_indicators() override {
        trend_context_ = std::make_unique<stratforge::TrendContext>(signal_, aligned_trend_);
    }

    [[nodiscard]] stratforge::IndicatorHistoryRequirements
    indicator_history_requirements() const override {
        return {{.feed_index = 0, .bars = 1}, {.feed_index = 1, .bars = 1}};
    }

    void update_indicators(std::size_t feed_index) override {
        if (feed_index == 1) {
            latest_trend_ = data(1).close()[0] >= 0.0 ? 1.0 : -1.0;
            latest_context_open_ = data(1).datetime()[0];
            context_available_ = true;
            ++context_updates;
            return;
        }
        if (feed_index != 0) return;

        // The wrapper owns the execution clock. It receives a held copy of the
        // latest context value; it never advances or indexes the context feed.
        signal_.forward(1.0);
        aligned_trend_.forward(context_available_
                                   ? latest_trend_
                                   : std::numeric_limits<double>::quiet_NaN());
        trend_context_->next();
        ++execution_updates;
        samples.push_back({data(0).datetime()[0], latest_context_open_,
                           context_available_, feed_advanced(1), aligned_trend_[0],
                           trend_context_->alignment()[0]});
    }

    [[nodiscard]] stratforge::EntrySignal check_open_conditions() override {
        ++decisions;
        return {};
    }

    [[nodiscard]] bool check_close_conditions() override { return false; }

private:
    stratforge::Line<double> signal_;
    stratforge::Line<double> aligned_trend_;
    std::unique_ptr<stratforge::TrendContext> trend_context_;
    double latest_trend_ = std::numeric_limits<double>::quiet_NaN();
    DateTime latest_context_open_{};
    bool context_available_ = false;
};

} // namespace

TEST_CASE("SF012 context wiring aligns sparse feed before execution wrappers",
          "[sf012][context][multiclock][no-lookahead]") {
    auto execution = std::make_unique<TestFeed>(hourly_bars(48));
    static_cast<void>(execution->load());
    execution->set_timeframe({stratforge::TimeFrame::Minutes, 60});

    std::vector<TestFeed::Bar> daily{
        {at_hour(0), -1.0, 1.0, -2.0, -1.0, 10.0},
        {at_hour(24), 1.0, 2.0, 0.0, 1.0, 20.0},
    };
    auto context = std::make_unique<TestFeed>(daily);
    static_cast<void>(context->load());
    context->set_timeframe({stratforge::TimeFrame::Days, 1});

    stratforge::Cerebro cerebro;
    auto& strategy = cerebro.add_strategy<AlignedTrendStrategy>();
    cerebro.add_data(std::move(execution), "hourly");
    cerebro.add_data(std::move(context), "daily-context");
    cerebro.run();

    REQUIRE(strategy.samples.size() == 48);
    CHECK(strategy.execution_updates == 48);
    CHECK(strategy.context_updates == 2);
    CHECK(strategy.decisions == 25); // hourly bars closing at/after the first daily close

    for (std::size_t i = 0; i < 23; ++i) {
        CHECK_FALSE(strategy.samples[i].context_available);
        CHECK(std::isnan(strategy.samples[i].aligned_trend));
        CHECK(strategy.samples[i].alignment == 0.0);
    }

    // The day-0 context bar closes at hour 24 and is therefore visible to the
    // execution bar opening at hour 23. Secondary ownership must run first on
    // this iteration, so the wrapper sees it without an extra one-bar lag.
    const auto& first_visible = strategy.samples[23];
    CHECK(first_visible.context_advanced);
    CHECK(first_visible.context_available);
    CHECK(first_visible.context_open == at_hour(0));
    CHECK(first_visible.execution_open == at_hour(23));
    CHECK(first_visible.aligned_trend == -1.0);
    CHECK(first_visible.alignment == -1.0);

    for (const auto& sample : strategy.samples) {
        if (!sample.context_available) continue;
        const auto execution_close = sample.execution_open + std::chrono::hours(1);
        const auto context_close = sample.context_open + std::chrono::days(1);
        CHECK(context_close <= execution_close);
    }

    const auto& second_visible = strategy.samples[47];
    CHECK(second_visible.context_advanced);
    CHECK(second_visible.context_open == at_hour(24));
    CHECK(second_visible.aligned_trend == 1.0);
    CHECK(second_visible.alignment == 1.0);
}
