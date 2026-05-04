#pragma once

#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/strategy.hpp>

namespace stratforge {

/// Base class for AI-augmented signal entry strategies.
///
/// Adds get_indicator_values() for AI model consumption alongside
/// standard open/close condition checks.
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators()
///   next()  -> get_indicator_values() + check_close -> check_open -> buy/sell/close
class AISignalEntryStrategy : public Strategy {
public:
    // -- Pure virtuals (must override) --

    /// Set up indicators during init phase.
    virtual void initialize_indicators() = 0;

    /// Collect current indicator values for AI model input.
    [[nodiscard]] virtual IndicatorValues get_indicator_values() = 0;

    /// Check whether conditions to open a position are met.
    [[nodiscard]] virtual EntrySignal check_open_conditions() = 0;

    /// Check whether conditions to close a position are met.
    [[nodiscard]] virtual bool check_close_conditions() = 0;

    /// Called each bar before business logic. Override to advance indicators.
    virtual void update_indicators() {}

private:
    void init() final {
        initialize_indicators();
    }

    void next() final {
        update_indicators();

        // Collect indicator snapshot (available to subclass for decision-making)
        [[maybe_unused]] auto values = get_indicator_values();

        // Close first
        if (position().size != 0.0 && check_close_conditions()) {
            (void)close();
        }

        // Then open
        auto open_sig = check_open_conditions();
        if (position().size == 0.0) {
            if (open_sig.long_signal) {
                (void)buy();
            } else if (open_sig.short_signal) {
                (void)sell();
            }
        }
    }
};

} // namespace stratforge
