#pragma once

#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/strategy.hpp>

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
    virtual void update_indicators() {}

private:
    void init() final {
        initialize_indicators();
    }

    void next() final {
        update_indicators();

        [[maybe_unused]] auto values = get_indicator_values();
    }
};

} // namespace stratforge
