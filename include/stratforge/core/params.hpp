#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace stratforge {

/// Params - Compile-time parameter system.
/// Strategy and Indicator parameters are defined as constexpr structs.
///
/// Usage:
///   struct MyParams {
///       int period = 20;
///       double factor = 2.0;
///   };
///
///   class MyStrategy : public Strategy<MyStrategy, MyParams> { ... };
///
/// The Params concept constrains types usable as parameters.

template <typename T>
concept Params = std::is_aggregate_v<T> && std::is_default_constructible_v<T>;

using ParamValue = std::variant<std::int64_t, double, bool, std::string>;
using ParamMap = std::unordered_map<std::string, ParamValue>;

class ParamView {
public:
    ParamView() = default;
    explicit ParamView(const ParamMap* params) noexcept : params_(params) {}

    [[nodiscard]] bool empty() const noexcept {
        return params_ == nullptr || params_->empty();
    }

    [[nodiscard]] bool has(std::string_view key) const {
        return params_ != nullptr && params_->contains(std::string(key));
    }

    [[nodiscard]] const ParamValue& at(std::string_view key) const {
        if (params_ == nullptr) {
            throw std::out_of_range("parameter map is empty");
        }
        return params_->at(std::string(key));
    }

    template <typename T>
    [[nodiscard]] T get(std::string_view key) const {
        const auto& value = at(key);

        if constexpr (std::is_same_v<T, std::string>) {
            if (const auto* direct = std::get_if<std::string>(&value)) {
                return *direct;
            }
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            if (const auto* direct = std::get_if<std::string>(&value)) {
                return *direct;
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            if (const auto* direct = std::get_if<bool>(&value)) {
                return *direct;
            }
        } else if constexpr (std::is_integral_v<T>) {
            if (const auto* direct = std::get_if<std::int64_t>(&value)) {
                return static_cast<T>(*direct);
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            if (const auto* direct = std::get_if<double>(&value)) {
                return static_cast<T>(*direct);
            }
            if (const auto* integral = std::get_if<std::int64_t>(&value)) {
                return static_cast<T>(*integral);
            }
        }

        throw std::bad_variant_access();
    }

private:
    const ParamMap* params_ = nullptr;
};

[[nodiscard]] inline ParamMap merge_params(ParamMap defaults, const ParamMap& overrides) {
    for (const auto& [key, value] : overrides) {
        defaults[key] = value;
    }
    return defaults;
}

} // namespace stratforge
