#pragma once

#include <stratforge/strategy/strategy.hpp>

namespace stratforge {

/// Base class for exit-only strategies.
///
/// Maps to Python signal_sources: exit, risk_override (ExitSignalBase).
/// Monitors an existing position and closes it when exit conditions are met.
/// Does not open positions.
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators()
///   next()  -> if in position && check_exit_signal() -> close()
class ExitStrategy : public Strategy {
public:
    // -- Pure virtuals (must override) --

    /// Set up indicators during init phase.
    virtual void initialize_indicators() = 0;

    /// Check whether exit conditions are met. Returns true to trigger close.
    [[nodiscard]] virtual bool check_exit_signal() = 0;

private:
    void init() final {
        initialize_indicators();
    }

    void next() final {
        if (position().size != 0.0 && check_exit_signal()) {
            (void)close();
        }
    }
};

} // namespace stratforge
