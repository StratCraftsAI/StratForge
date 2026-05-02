#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace stratforge {

/// Transparent hash functor for std::unordered_map.
/// Enables heterogeneous lookup with std::string_view keys
/// without allocating a temporary std::string.
struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string>{}(s);
    }
    [[nodiscard]] std::size_t operator()(const char* s) const noexcept {
        return std::hash<std::string_view>{}(std::string_view{s});
    }
};

/// Transparent equality functor for std::unordered_map.
/// Companion to TransparentStringHash for heterogeneous lookup.
struct TransparentStringEqual {
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
};

} // namespace stratforge
