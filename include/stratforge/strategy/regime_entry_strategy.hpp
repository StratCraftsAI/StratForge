#pragma once

#include <stratforge/strategy/entry_signal.hpp>
#include <stratforge/strategy/strategy.hpp>

#include <cstddef>

namespace stratforge {

/// Base class for regime-aware entry strategies.
///
/// Maps to Python signal_sources: indicator_entry_trend, indicator_entry_range,
/// indicator_entry_standalone (RegimeTrendEntryBase, RegimeRangeEntryBase,
/// RegimeStandaloneEntryBase).
/// Subclasses define indicators, warmup, and open/close condition logic.
/// The lifecycle wiring handles position management (close before open, buy/sell dispatch).
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators() + set_minimum_period(get_base_warmup_period())
///   next()  -> check_close -> check_open -> buy/sell/close
class RegimeEntryStrategy : public Strategy {
public:
    // -- Pure virtuals (must override) --

    /// Set up indicators during init phase.
    virtual void initialize_indicators() = 0;

    /// Return the warmup period (number of bars before next() fires).
    [[nodiscard]] virtual std::size_t get_base_warmup_period() const = 0;

    /// Check whether conditions to open a position are met.
    [[nodiscard]] virtual EntrySignal check_open_conditions() = 0;

    /// Check whether conditions to close a position are met.
    [[nodiscard]] virtual bool check_close_conditions() = 0;

    /// Called each bar before business logic. Override to advance indicators.
    virtual void update_indicators() {}

private:
    void init() final {
        initialize_indicators();
        set_minimum_period(get_base_warmup_period());
    }

    void next() final {
        update_indicators();

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
