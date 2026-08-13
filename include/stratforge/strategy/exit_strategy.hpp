#pragma once

#include <stratforge/strategy/strategy.hpp>

#include <cstddef>

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

    /// Called each bar before business logic. Override to advance indicators.
    /// Total bars required before exit business logic may inspect indicators.
    [[nodiscard]] virtual std::size_t get_indicator_history_warmup_period() const noexcept {
        return 1;
    }

    /// Advance indicators during warmup without evaluating an exit.
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

        if (position().size != 0.0 && check_exit_signal()) {
            (void)close();
        }
    }
};

} // namespace stratforge
