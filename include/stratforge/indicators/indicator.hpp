#pragma once

#include <stratforge/core/line.hpp>

#include <cstddef>

namespace stratforge {

/// Virtual base class for polymorphic indicator storage (MACDExt only).
/// All other indicators use CRTP via Indicator<Derived>.
class IndicatorBase {
public:
    IndicatorBase() = default;
    virtual ~IndicatorBase() = default;

    IndicatorBase(const IndicatorBase&) = delete;
    IndicatorBase& operator=(const IndicatorBase&) = delete;
    IndicatorBase(IndicatorBase&&) = default;
    IndicatorBase& operator=(IndicatorBase&&) = default;

    /// Compute the indicator value for the current bar
    virtual void next() = 0;

    /// Minimum number of bars required before indicator produces valid output
    [[nodiscard]] virtual std::size_t minimum_period() const noexcept = 0;

    /// Access the output line
    [[nodiscard]] const Line<double>& line() const noexcept { return line_; }
    [[nodiscard]] Line<double>& line() noexcept { return line_; }

    /// Convenience: current value
    [[nodiscard]] double operator[](int offset) const {
        return line_[offset];
    }

protected:
    /// Reserve output line capacity. Call once from first next() invocation.
    void reserve_output(std::size_t expected_bars) {
        line_.data().reserve(expected_bars);
    }

    Line<double> line_;
};

/// CRTP base for all concrete indicators.
/// Derived must implement next_impl() and minimum_period_impl().
template <typename Derived>
class Indicator : public IndicatorBase {
public:
    void next() final { static_cast<Derived*>(this)->next_impl(); }

    [[nodiscard]] std::size_t minimum_period() const noexcept final {
        return static_cast<const Derived*>(this)->minimum_period_impl();
    }
};

} // namespace stratforge
