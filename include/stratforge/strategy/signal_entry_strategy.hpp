#pragma once

#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <cstddef>

namespace stratforge {

/// Base class for signal-based entry strategies without regime ownership.
///
/// Same open/close lifecycle as RegimeEntryStrategy with an optional explicit
/// indicator-history warmup requirement.
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators() + set indicator-history warmup
///   prenext() -> update_indicators() only
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

    /// Called each bar before business logic. Override to advance indicators.
    /// Total bars required before signal evaluation. A strategy that reads
    /// indicator output [-N] returns the indicator calculation period plus N.
    [[nodiscard]] virtual std::size_t get_indicator_history_warmup_period() const noexcept {
        return 1;
    }

    /// Warmup flow. Custom lifecycle implementations should call this base
    /// implementation so indicator history continues to advance.
    void prenext() override {
        advance_generated_indicators();
    }

protected:
    /// Default execution flow. Subclasses may override for custom execution
    /// semantics and call this implementation to retain the standard flow.
    void next() override {
        advance_generated_indicators();

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
        const auto declared_warmup = get_indicator_history_warmup_period();
        if (minimum_period() < declared_warmup) {
            set_minimum_period(declared_warmup);
        }
        apply_indicator_history_requirements();
    }
};

} // namespace stratforge
