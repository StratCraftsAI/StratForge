#pragma once

#include <stratforge/strategy/strategy.hpp>

#include <cstddef>

namespace stratforge {

/// Base class for observation-only strategies (no orders).
///
/// Maps to Python signal_sources: watchlist (TraderObserverBase).
/// Evaluates a precondition each bar for monitoring/logging purposes.
/// Never submits orders.
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators()
///   next()  -> check_precondition() (observation only, no orders)
class ObserverStrategy : public Strategy {
public:
    // -- Pure virtuals (must override) --

    /// Set up indicators during init phase.
    virtual void initialize_indicators() = 0;

    /// Evaluate the precondition. Return true if the condition is met.
    /// This is for observation only; no orders are submitted.
    [[nodiscard]] virtual bool check_precondition() = 0;

    /// Called each bar before business logic. Override to advance indicators.
    /// Total bars required before observation business logic may run.
    [[nodiscard]] virtual std::size_t get_indicator_history_warmup_period() const noexcept {
        return 1;
    }

    /// Advance indicators during warmup without evaluating the precondition.
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

        [[maybe_unused]] auto result = check_precondition();
    }
};

} // namespace stratforge
