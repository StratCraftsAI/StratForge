#pragma once

#include <stratforge/strategy/strategy.hpp>

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
    virtual void update_indicators() {}

private:
    void init() final {
        initialize_indicators();
    }

    void next() final {
        update_indicators();

        [[maybe_unused]] auto result = check_precondition();
    }
};

} // namespace stratforge
