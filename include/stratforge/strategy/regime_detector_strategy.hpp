#pragma once

#include <stratforge/strategy/strategy.hpp>

#include <cstddef>

namespace stratforge {

/// Regime state detected by RegimeDetectorStrategy.
enum class RegimeState {
    Unknown,
    Trending,
    RangeBound,
    HighVolatility,
    LowVolatility,
};

/// Base class for regime/market-state detector strategies.
///
/// Maps to Python signal_sources: indicator_detector_* (RegimeStateBase).
/// Subclasses implement indicator setup, warmup period, and optionally
/// the three regime detection methods (trend, range, volatility).
///
/// Lifecycle wiring:
///   init()  -> initialize_indicators() + set_minimum_period(get_base_warmup_period())
///   next()  -> compute regime state from trend/range strength + volatility state
class RegimeDetectorStrategy : public Strategy {
public:
    // -- Pure virtuals (must override) --

    /// Set up indicators during init phase.
    virtual void initialize_indicators() = 0;

    /// Return the warmup period (number of bars before next() fires).
    [[nodiscard]] virtual std::size_t get_base_warmup_period() const = 0;

    // -- Optional overrides with defaults --

    /// Calculate trend strength (0.0 = no trend, 1.0 = strong trend).
    [[nodiscard]] virtual double calculate_trend_strength() { return 0.0; }

    /// Calculate range-bound strength (0.0 = no range, 1.0 = strong range).
    [[nodiscard]] virtual double calculate_range_strength() { return 0.0; }

    /// Detect high-volatility state. Returns true if volatility is elevated.
    [[nodiscard]] virtual bool get_volatility_state() { return false; }

    /// Current regime state (updated each bar by next()).
    [[nodiscard]] RegimeState current_state() const noexcept { return current_state_; }

    /// Called each bar before business logic. Override to advance indicators.
    virtual void update_indicators() {}

protected:
    RegimeState current_state_ = RegimeState::Unknown;

private:
    void init() final {
        initialize_indicators();
        set_minimum_period(get_base_warmup_period());
    }

    void next() final {
        update_indicators();

        auto trend = calculate_trend_strength();
        auto range = calculate_range_strength();
        bool high_vol = get_volatility_state();

        if (high_vol) {
            current_state_ = RegimeState::HighVolatility;
        } else if (trend > range) {
            current_state_ = RegimeState::Trending;
        } else if (range > trend) {
            current_state_ = RegimeState::RangeBound;
        } else {
            current_state_ = RegimeState::LowVolatility;
        }
    }
};

} // namespace stratforge
