#include <catch2/catch_test_macros.hpp>

#include <stratforge/engine/cerebro.hpp>
#include <stratforge/indicators/williams.hpp>
#include <stratforge/strategy/ai_libero_strategy.hpp>
#include <stratforge/strategy/ai_signal_entry_strategy.hpp>
#include <stratforge/strategy/exit_strategy.hpp>
#include <stratforge/strategy/observer_strategy.hpp>
#include <stratforge/strategy/regime_detector_strategy.hpp>
#include <stratforge/strategy/regime_entry_strategy.hpp>

#include "test_helpers.hpp"

#include <cstddef>
#include <memory>
#include <vector>

using namespace stratforge;
using StaticFeed = stratforge::test::StaticFeed;

namespace {

std::unique_ptr<StaticFeed> history_bars() {
    std::vector<StaticFeed::Bar> bars;
    for (int i = 0; i < 17; ++i) {
        const double base = 100.0 + static_cast<double>(i);
        bars.push_back({base, base + 2.0, base - 2.0, base + 0.5});
    }
    return std::make_unique<StaticFeed>(std::move(bars));
}

struct RegimeDetectorWarmup final : RegimeDetectorStrategy {
    int updates = 0;
    int decisions = 0;
    std::unique_ptr<WilliamsR> indicator;
    void initialize_indicators() override {
        indicator = std::make_unique<WilliamsR>(data().high(), data().low(), data().close(), 14);
        set_minimum_period(16);
    }
    [[nodiscard]] std::size_t get_base_warmup_period() const override { return 15; }
    void update_indicators() override { indicator->next(); ++updates; }
    [[nodiscard]] double calculate_trend_strength() override {
        ++decisions;
        static_cast<void>(indicator->line()[-1]);
        return 0.0;
    }
};

struct RegimeEntryWarmup final : RegimeEntryStrategy {
    int updates = 0;
    int decisions = 0;
    std::unique_ptr<WilliamsR> indicator;
    void initialize_indicators() override {
        indicator = std::make_unique<WilliamsR>(data().high(), data().low(), data().close(), 14);
        set_minimum_period(16);
    }
    [[nodiscard]] std::size_t get_base_warmup_period() const override { return 15; }
    void update_indicators() override { indicator->next(); ++updates; }
    [[nodiscard]] EntrySignal check_open_conditions() override {
        ++decisions;
        static_cast<void>(indicator->line()[-1]);
        return {};
    }
    [[nodiscard]] bool check_close_conditions() override { return false; }
};

struct AISignalWarmup final : AISignalEntryStrategy {
    int updates = 0;
    int decisions = 0;
    std::unique_ptr<WilliamsR> indicator;
    void initialize_indicators() override {
        indicator = std::make_unique<WilliamsR>(data().high(), data().low(), data().close(), 14);
        set_minimum_period(16);
    }
    [[nodiscard]] std::size_t get_indicator_history_warmup_period() const noexcept override {
        return 15;
    }
    void update_indicators() override { indicator->next(); ++updates; }
    [[nodiscard]] IndicatorValues get_indicator_values() override {
        return {{"previous", indicator->line()[-1]}};
    }
    [[nodiscard]] EntrySignal check_open_conditions() override {
        ++decisions;
        static_cast<void>(indicator->line()[-1]);
        return {};
    }
    [[nodiscard]] bool check_close_conditions() override { return false; }
};

struct AILiberoWarmup final : AILiberoStrategy {
    int updates = 0;
    int decisions = 0;
    std::unique_ptr<WilliamsR> indicator;
    void initialize_indicators() override {
        indicator = std::make_unique<WilliamsR>(data().high(), data().low(), data().close(), 14);
        set_minimum_period(16);
    }
    [[nodiscard]] std::size_t get_indicator_history_warmup_period() const noexcept override {
        return 15;
    }
    void update_indicators() override { indicator->next(); ++updates; }
    [[nodiscard]] IndicatorValues get_indicator_values() override {
        ++decisions;
        return {{"previous", indicator->line()[-1]}};
    }
};

struct ExitWarmup final : ExitStrategy {
    int updates = 0;
    int decisions = 0;
    std::unique_ptr<WilliamsR> indicator;
    void initialize_indicators() override {
        indicator = std::make_unique<WilliamsR>(data().high(), data().low(), data().close(), 14);
        set_minimum_period(16);
    }
    [[nodiscard]] std::size_t get_indicator_history_warmup_period() const noexcept override {
        return 15;
    }
    void start() override { static_cast<void>(buy(1.0)); }
    void update_indicators() override { indicator->next(); ++updates; }
    [[nodiscard]] bool check_exit_signal() override {
        ++decisions;
        static_cast<void>(indicator->line()[-1]);
        return false;
    }
};

struct ObserverWarmup final : ObserverStrategy {
    int updates = 0;
    int decisions = 0;
    std::unique_ptr<WilliamsR> indicator;
    void initialize_indicators() override {
        indicator = std::make_unique<WilliamsR>(data().high(), data().low(), data().close(), 14);
        set_minimum_period(16);
    }
    [[nodiscard]] std::size_t get_indicator_history_warmup_period() const noexcept override {
        return 15;
    }
    void update_indicators() override { indicator->next(); ++updates; }
    [[nodiscard]] bool check_precondition() override {
        ++decisions;
        static_cast<void>(indicator->line()[-1]);
        return false;
    }
};

template <typename StrategyType>
StrategyType& run_warmup(Cerebro& cerebro) {
    cerebro.add_data(history_bars(), "main");
    auto& strategy = cerebro.add_strategy<StrategyType>();
    cerebro.run();
    return strategy;
}

} // namespace

TEST_CASE("all generated strategy bases advance indicators throughout warmup",
          "[strategy][history_warmup]") {
    SECTION("regime detector") {
        Cerebro cerebro;
        auto& strategy = run_warmup<RegimeDetectorWarmup>(cerebro);
        CHECK(strategy.minimum_period() == 16);
        CHECK(strategy.updates == 17);
        CHECK(strategy.decisions == 2);
    }
    SECTION("regime entry") {
        Cerebro cerebro;
        auto& strategy = run_warmup<RegimeEntryWarmup>(cerebro);
        CHECK(strategy.minimum_period() == 16);
        CHECK(strategy.updates == 17);
        CHECK(strategy.decisions == 2);
    }
    SECTION("AI signal entry") {
        Cerebro cerebro;
        auto& strategy = run_warmup<AISignalWarmup>(cerebro);
        CHECK(strategy.minimum_period() == 16);
        CHECK(strategy.updates == 17);
        CHECK(strategy.decisions == 2);
    }
    SECTION("AI libero") {
        Cerebro cerebro;
        auto& strategy = run_warmup<AILiberoWarmup>(cerebro);
        CHECK(strategy.minimum_period() == 16);
        CHECK(strategy.updates == 17);
        CHECK(strategy.decisions == 2);
    }
    SECTION("exit") {
        Cerebro cerebro;
        auto& strategy = run_warmup<ExitWarmup>(cerebro);
        CHECK(strategy.minimum_period() == 16);
        CHECK(strategy.updates == 17);
        CHECK(strategy.decisions == 2);
    }
    SECTION("observer") {
        Cerebro cerebro;
        auto& strategy = run_warmup<ObserverWarmup>(cerebro);
        CHECK(strategy.minimum_period() == 16);
        CHECK(strategy.updates == 17);
        CHECK(strategy.decisions == 2);
    }
}
