#pragma once

#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <cstddef>

namespace stratforge {

/// Base class for AI Libero strategies (indicator collection, no built-in entry/exit).
///
/// Maps to Python signal_sources: aiLibero (LLMLiberoStrategyBase).
/// Collects indicator values each bar for an LLM agent to consume.
/// The subclass (or external agent) handles all trading decisions.
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators()
///   next()  -> get_indicator_values() (subclass handles trading logic)
class AILiberoStrategy : public Strategy {
public:
    // -- Pure virtuals (must override) --

    /// Set up indicators during init phase.
    virtual void initialize_indicators() = 0;

    /// Collect current indicator values for LLM agent input.
    [[nodiscard]] virtual IndicatorValues get_indicator_values() = 0;

    /// Called each bar before business logic. Override to advance indicators.
    /// Total bars required before indicator values are exposed to the agent.
    [[nodiscard]] virtual std::size_t get_indicator_history_warmup_period() const noexcept {
        return 1;
    }

    /// Advance indicators during warmup without exposing an AI snapshot.
    void prenext() override {
        advance_generated_indicators();
    }

private:
    void init() final {
        initialize_indicators();
        const auto declared_warmup = get_indicator_history_warmup_period();
        if (minimum_period() < declared_warmup) {
            set_minimum_period(declared_warmup);
        }
        apply_indicator_history_requirements();
    }

    void next() final {
        advance_generated_indicators();

        [[maybe_unused]] auto values = get_indicator_values();
    }
};

} // namespace stratforge
