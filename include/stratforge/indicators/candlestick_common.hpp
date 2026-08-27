#pragma once

/// @file candlestick_common.hpp
/// @brief TA-Lib 0.6.4-compatible candle measurements and settings.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <stdexcept>

namespace stratforge::candlestick {

/// The price range used to form a TA-Lib candle threshold.
enum class RangeType : std::uint8_t {
    RealBody,
    HighLow,
    Shadows,
};

/// The eleven configurable candle meanings in TA-Lib's pattern engine.
enum class SettingType : std::uint8_t {
    BodyLong,
    BodyVeryLong,
    BodyShort,
    BodyDoji,
    ShadowLong,
    ShadowVeryLong,
    ShadowShort,
    ShadowVeryShort,
    Near,
    Far,
    Equal,
    Count,
};

struct Candle {
    double open{};
    double high{};
    double low{};
    double close{};
};

struct Setting {
    RangeType range_type{};
    std::size_t average_period{};
    double factor{};

    friend constexpr bool operator==(const Setting&, const Setting&) = default;
};

inline constexpr std::size_t setting_count =
    static_cast<std::size_t>(SettingType::Count);

/// Defaults frozen from TA-Lib 0.6.4's TA_CandleDefaultSettings.
inline constexpr std::array<Setting, setting_count> default_settings{{
    {RangeType::RealBody, 10, 1.0}, // BodyLong
    {RangeType::RealBody, 10, 3.0}, // BodyVeryLong
    {RangeType::RealBody, 10, 1.0}, // BodyShort
    {RangeType::HighLow, 10, 0.1},  // BodyDoji
    {RangeType::RealBody, 0, 1.0},  // ShadowLong
    {RangeType::RealBody, 0, 2.0},  // ShadowVeryLong
    {RangeType::Shadows, 10, 1.0},  // ShadowShort
    {RangeType::HighLow, 10, 0.1},  // ShadowVeryShort
    {RangeType::HighLow, 5, 0.2},   // Near
    {RangeType::HighLow, 5, 0.6},   // Far
    {RangeType::HighLow, 5, 0.05},  // Equal
}};

[[nodiscard]] constexpr std::size_t index(SettingType type) {
    return static_cast<std::size_t>(type);
}

[[nodiscard]] constexpr double real_body(const Candle& candle) noexcept {
    return candle.close >= candle.open
        ? candle.close - candle.open
        : candle.open - candle.close;
}

[[nodiscard]] constexpr double upper_shadow(const Candle& candle) noexcept {
    return candle.high - std::max(candle.open, candle.close);
}

[[nodiscard]] constexpr double lower_shadow(const Candle& candle) noexcept {
    return std::min(candle.open, candle.close) - candle.low;
}

[[nodiscard]] constexpr double high_low_range(const Candle& candle) noexcept {
    return candle.high - candle.low;
}

/// TA-Lib treats a zero-body candle as white/bullish.
[[nodiscard]] constexpr int color(const Candle& candle) noexcept {
    return candle.close >= candle.open ? 1 : -1;
}

[[nodiscard]] constexpr double range(RangeType type,
                                     const Candle& candle) noexcept {
    switch (type) {
    case RangeType::RealBody:
        return real_body(candle);
    case RangeType::HighLow:
        return high_low_range(candle);
    case RangeType::Shadows:
        return upper_shadow(candle) + lower_shadow(candle);
    }
    return 0.0;
}

[[nodiscard]] constexpr bool real_body_gap_up(const Candle& current,
                                               const Candle& previous) noexcept {
    return std::min(current.open, current.close) >
           std::max(previous.open, previous.close);
}

[[nodiscard]] constexpr bool real_body_gap_down(const Candle& current,
                                                 const Candle& previous) noexcept {
    return std::max(current.open, current.close) <
           std::min(previous.open, previous.close);
}

[[nodiscard]] constexpr bool candle_gap_up(const Candle& current,
                                            const Candle& previous) noexcept {
    return current.low > previous.high;
}

[[nodiscard]] constexpr bool candle_gap_down(const Candle& current,
                                              const Candle& previous) noexcept {
    return current.high < previous.low;
}

/// Per-indicator settings avoid TA-Lib's process-global mutable configuration.
class Settings {
public:
    constexpr Settings() = default;

    [[nodiscard]] constexpr const Setting& get(SettingType type) const {
        const auto position = index(type);
        if (position >= settings_.size()) {
            throw std::out_of_range("invalid candle setting type");
        }
        return settings_[position];
    }

    void set(SettingType type, Setting setting) {
        if (!std::isfinite(setting.factor) || setting.factor < 0.0) {
            throw std::invalid_argument(
                "candle setting factor must be finite and non-negative");
        }
        const auto position = index(type);
        if (position >= settings_.size()) {
            throw std::out_of_range("invalid candle setting type");
        }
        settings_[position] = setting;
    }

    constexpr void restore(SettingType type) {
        const auto position = index(type);
        if (position >= settings_.size()) {
            throw std::out_of_range("invalid candle setting type");
        }
        settings_[position] = default_settings[position];
    }

    constexpr void restore_all() noexcept { settings_ = default_settings; }

private:
    std::array<Setting, setting_count> settings_{default_settings};
};

/// Convert the rolling sum maintained by a pattern into TA-Lib's threshold.
/// For a zero-period setting TA-Lib uses the current candle's range. Shadow
/// ranges are divided by two because they contain both shadows.
[[nodiscard]] constexpr double average(const Setting& setting,
                                       double period_total,
                                       const Candle& current) noexcept {
    const double base = setting.average_period == 0
        ? range(setting.range_type, current)
        : period_total / static_cast<double>(setting.average_period);
    const double divisor = setting.range_type == RangeType::Shadows ? 2.0 : 1.0;
    return setting.factor * base / divisor;
}

/// Streaming window whose threshold excludes the current candle, matching the
/// period-total update order used by TA-Lib's generated CDL implementations.
class AverageWindow {
public:
    explicit AverageWindow(Setting setting) : setting_(setting) {
        if (!std::isfinite(setting_.factor) || setting_.factor < 0.0) {
            throw std::invalid_argument(
                "candle setting factor must be finite and non-negative");
        }
    }

    [[nodiscard]] bool ready() const noexcept {
        return setting_.average_period == 0 ||
               values_.size() == setting_.average_period;
    }

    [[nodiscard]] double threshold(const Candle& current) const {
        if (!ready()) {
            throw std::logic_error("candle average window is not ready");
        }
        return average(setting_, total_, current);
    }

    void advance(const Candle& candle) {
        if (setting_.average_period == 0) {
            return;
        }
        const double value = range(setting_.range_type, candle);
        values_.push_back(value);
        total_ += value;
        if (values_.size() > setting_.average_period) {
            total_ -= values_.front();
            values_.pop_front();
        }
    }

    void reset() noexcept {
        values_.clear();
        total_ = 0.0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    [[nodiscard]] double total() const noexcept { return total_; }

private:
    Setting setting_;
    std::deque<double> values_;
    double total_{0.0};
};

/// TA-Lib lookback is max(setting periods) plus the pattern's prior-bar count.
[[nodiscard]] constexpr std::size_t lookback(
    const Settings& settings,
    std::size_t prior_bars,
    std::initializer_list<SettingType> types) {
    std::size_t result = prior_bars;
    for (const auto type : types) {
        result = std::max(result,
                          prior_bars + settings.get(type).average_period);
    }
    return result;
}

[[nodiscard]] constexpr std::size_t minimum_period(
    const Settings& settings,
    std::size_t pattern_bars,
    std::initializer_list<SettingType> types) {
    if (pattern_bars == 0) {
        throw std::invalid_argument("candlestick pattern must use at least one bar");
    }
    return lookback(settings, pattern_bars - 1, types) + 1;
}

[[nodiscard]] inline double validate_penetration(double penetration) {
    if (!std::isfinite(penetration) || penetration < 0.0) {
        throw std::invalid_argument(
            "candlestick penetration must be finite and non-negative");
    }
    return penetration;
}

[[nodiscard]] constexpr double signal(const Candle& candle,
                                      int magnitude = 100) noexcept {
    return static_cast<double>(color(candle) * magnitude);
}

[[nodiscard]] constexpr double signal(int direction,
                                      int magnitude = 100) noexcept {
    return direction > 0 ? static_cast<double>(magnitude)
                         : direction < 0 ? -static_cast<double>(magnitude)
                                         : 0.0;
}

} // namespace stratforge::candlestick
