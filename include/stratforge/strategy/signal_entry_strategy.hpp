#pragma once

#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/strategy.hpp>

namespace stratforge {

/// Base class for signal-based entry strategies (no regime, no warmup override).
///
/// Same open/close lifecycle as RegimeEntryStrategy but without
/// a mandatory warmup period override.
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators()
///   next()  -> check_close -> check_open -> buy/sell/close
class SignalEntryStrategy : public Strategy {
public:
    // -- Pure virtuals (must override) --

    /// Set up indicators during init phase.
    virtual void initialize_indicators() = 0;

    /// Check whether conditions to open a position are met.
    [[nodiscard]] virtual EntrySignal check_open_conditions() = 0;

    /// Check whether conditions to close a position are met.
    [[nodiscard]] virtual bool check_close_conditions() = 0;

protected:
    /// Default execution flow: close-then-open each bar.
    /// Subclasses may override for custom execution semantics.
    void next() override {
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

private:
    void init() final {
        initialize_indicators();
    }
};

} // namespace stratforge
