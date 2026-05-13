// SPDX-License-Identifier: MIT
//
// include/stratforge/bar.hpp — OHLCV + timestamp POD.
//
// : Offline-snapshot counterpart to DataFeed's line-based view.
// Designed for batch-API consumers (e.g. stratforge::stats Signal Discovery
// hypothesis tests). Streaming backtest execution continues to use
// DataFeed/LineSeries.

#pragma once

#include <chrono>
#include <compare>
#include <type_traits>

namespace stratforge {

using DateTime = std::chrono::system_clock::time_point;

struct Bar {
    DateTime timestamp{};
    double open   = 0.0;
    double high   = 0.0;
    double low    = 0.0;
    double close  = 0.0;
    double volume = 0.0;

    [[nodiscard]] friend constexpr auto operator<=>(const Bar&, const Bar&) = default;
};

static_assert(std::is_standard_layout_v<Bar>);
static_assert(std::is_trivially_copyable_v<Bar>);

}  // namespace stratforge
