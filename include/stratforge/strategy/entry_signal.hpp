#pragma once

#include <stratforge/core/transparent_hash.hpp>

#include <string>
#include <unordered_map>

namespace stratforge {

/// Entry signal returned by strategy open/close condition checks.
/// Aggregate type: supports designated initializers and constexpr construction.
struct EntrySignal {
    bool long_signal = false;
    bool short_signal = false;

    /// True if any signal is active.
    [[nodiscard]] constexpr bool any() const noexcept {
        return long_signal || short_signal;
    }

    /// True if no signal is active.
    [[nodiscard]] constexpr bool none() const noexcept {
        return !long_signal && !short_signal;
    }

    [[nodiscard]] constexpr bool operator==(const EntrySignal&) const noexcept = default;
};

/// Indicator name -> value map returned by AI-based strategies.
/// Uses transparent hash/equal for string_view lookup without allocation.
using IndicatorValues = std::unordered_map<std::string, double,
                                           TransparentStringHash,
                                           TransparentStringEqual>;

} // namespace stratforge
