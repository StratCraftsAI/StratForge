#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace stratforge::indicator_history {

inline constexpr std::string_view contract_version = "1.0.0";

enum class ErrorCode {
    UnknownIndicator,
    UnknownAccessor,
    InvalidParameter,
    UnsupportedPeriodContract,
    Overflow,
};

struct Error {
    ErrorCode code;
    std::string message;
};

using Parameter = std::variant<std::int64_t, std::uint64_t, double, bool>;
using Parameters = std::map<std::string, Parameter, std::less<>>;
using Result = std::expected<std::size_t, Error>;

enum class ParameterType { Size, Int, Double, Bool };

struct ParameterDescriptor {
    std::string_view name;
    ParameterType type;
    Parameter default_value;
    std::string_view normalization;
    std::string_view constraints;
};

struct IndicatorDescriptor {
    std::string_view canonical_identity;
    std::vector<std::string_view> aliases;
    std::vector<ParameterDescriptor> parameters;
    std::vector<std::string_view> accessors;
};

namespace detail {

template <typename T>
using ParameterResult = std::expected<T, Error>;

inline Error invalid_parameter(std::string_view name, std::string_view reason) {
    return {ErrorCode::InvalidParameter,
            "invalid parameter '" + std::string(name) + "': " + std::string(reason)};
}

inline ParameterResult<std::size_t>
read_size(const Parameters& parameters, std::string_view name, std::size_t default_value) {
    const auto it = parameters.find(name);
    if (it == parameters.end()) return default_value;
    if (const auto* value = std::get_if<std::uint64_t>(&it->second)) {
        if (*value > std::numeric_limits<std::size_t>::max()) {
            return std::unexpected(invalid_parameter(name, "out of range"));
        }
        return static_cast<std::size_t>(*value);
    }
    if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
        if (*value < 0 || static_cast<std::uint64_t>(*value) >
                              std::numeric_limits<std::size_t>::max()) {
            return std::unexpected(invalid_parameter(name, "out of range"));
        }
        return static_cast<std::size_t>(*value);
    }
    return std::unexpected(invalid_parameter(name, "expected std::size_t"));
}

inline ParameterResult<int>
read_int(const Parameters& parameters, std::string_view name, int default_value) {
    const auto it = parameters.find(name);
    if (it == parameters.end()) return default_value;
    if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
        if (*value < std::numeric_limits<int>::min() ||
            *value > std::numeric_limits<int>::max()) {
            return std::unexpected(invalid_parameter(name, "out of range"));
        }
        return static_cast<int>(*value);
    }
    if (const auto* value = std::get_if<std::uint64_t>(&it->second)) {
        if (*value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return std::unexpected(invalid_parameter(name, "out of range"));
        }
        return static_cast<int>(*value);
    }
    return std::unexpected(invalid_parameter(name, "expected int"));
}

inline ParameterResult<double>
read_double(const Parameters& parameters, std::string_view name, double default_value) {
    const auto it = parameters.find(name);
    if (it == parameters.end()) return default_value;
    if (const auto* value = std::get_if<double>(&it->second)) return *value;
    if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
        return static_cast<double>(*value);
    }
    if (const auto* value = std::get_if<std::uint64_t>(&it->second)) {
        return static_cast<double>(*value);
    }
    return std::unexpected(invalid_parameter(name, "expected double"));
}

inline ParameterResult<bool>
read_bool(const Parameters& parameters, std::string_view name, bool default_value) {
    const auto it = parameters.find(name);
    if (it == parameters.end()) return default_value;
    if (const auto* value = std::get_if<bool>(&it->second)) return *value;
    return std::unexpected(invalid_parameter(name, "expected bool"));
}

template <typename IndicatorType>
[[nodiscard]] std::size_t accessor_period(const IndicatorType& indicator,
                                          std::string_view accessor) noexcept {
    if constexpr (requires { indicator.accessor_minimum_period(accessor); }) {
        return indicator.accessor_minimum_period(accessor);
    }
    return indicator.minimum_period();
}

Result resolve_generated(std::string_view identity,
                         const Parameters& parameters,
                         std::string_view accessor);
Result aggregate_generated(std::string_view identity,
                           const Parameters& parameters);
Result observed_readiness_generated(std::string_view identity,
                                    const Parameters& parameters,
                                    std::string_view accessor);
std::span<const IndicatorDescriptor> descriptors_generated();

} // namespace detail

inline Result effective_minimum_period(std::string_view identity,
                                       const Parameters& parameters,
                                       std::string_view accessor) {
    return detail::resolve_generated(identity, parameters, accessor);
}

/// Aggregate period reported by the constructed runtime indicator.
/// This is primarily useful for compatibility and drift verification.
inline Result aggregate_minimum_period(std::string_view identity,
                                       const Parameters& parameters) {
    return detail::aggregate_generated(identity, parameters);
}

inline Result required_warmup(std::size_t accessor_minimum_period,
                              std::size_t lookback_depth) {
    if (lookback_depth >
        std::numeric_limits<std::size_t>::max() - accessor_minimum_period) {
        return std::unexpected(Error{ErrorCode::Overflow,
                                     "indicator period plus lookback overflows size_t"});
    }
    return accessor_minimum_period + lookback_depth;
}

inline std::span<const IndicatorDescriptor> descriptors() {
    return detail::descriptors_generated();
}

} // namespace stratforge::indicator_history

#include <stratforge/indicators/history_contract_generated.hpp>
