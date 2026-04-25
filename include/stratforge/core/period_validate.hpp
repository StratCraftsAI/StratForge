#pragma once

#include <cstddef>

namespace stratforge {

/// Compile-time period validation for LLM-generated strategies.
/// When an indicator is constructed with a literal period (the common case
/// in generated code), the compiler will reject period == 0 at compile time.
///
/// Usage:
///   SMA sma(source, validated_period(14));   // OK
///   SMA sma(source, validated_period(0));    // compile error
///
consteval std::size_t validated_period(std::size_t p) {
    if (p == 0) throw "period must be >= 1";
    return p;
}

} // namespace stratforge
