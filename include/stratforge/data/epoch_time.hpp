// SPDX-License-Identifier: MIT
// : Canonical epoch-millisecond conversion for market-data time.

#pragma once

#include <stratforge/bar.hpp>

#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace stratforge {

inline constexpr std::int64_t MIN_DATETIME_EPOCH_MILLISECONDS =
    std::chrono::duration_cast<std::chrono::milliseconds>(DateTime::duration::min()).count();
inline constexpr std::int64_t MAX_DATETIME_EPOCH_MILLISECONDS =
    std::chrono::duration_cast<std::chrono::milliseconds>(DateTime::duration::max()).count();

[[nodiscard]] inline DateTime datetime_from_epoch_milliseconds(std::int64_t epoch_ms) {
    if (epoch_ms < MIN_DATETIME_EPOCH_MILLISECONDS ||
        epoch_ms > MAX_DATETIME_EPOCH_MILLISECONDS) {
        throw std::out_of_range("epoch-millisecond timestamp exceeds the DateTime range");
    }

    return DateTime(std::chrono::duration_cast<DateTime::duration>(
        std::chrono::milliseconds{epoch_ms}));
}

[[nodiscard]] inline std::int64_t datetime_to_epoch_milliseconds(DateTime datetime) noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        datetime.time_since_epoch()).count();
}

} // namespace stratforge
