#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Price Volume Rank: classifies bar into 4 categories based on
/// price change and volume change vs previous bar.
/// +1 = price up + volume up, -1 = price down + volume down,
/// +2 = price up + volume down, -2 = price down + volume up.
class PVR : public Indicator<PVR> {
public:
    PVR(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();

        if (idx == 0) {
            line_.forward(0.0);
            return;
        }

        const bool price_up = close_.data()[idx] > close_.data()[idx - 1];
        const bool vol_up = volume_.data()[idx] > volume_.data()[idx - 1];

        if (price_up && vol_up)
            line_.forward(1.0);
        else if (!price_up && !vol_up)
            line_.forward(-1.0);
        else if (price_up && !vol_up)
            line_.forward(2.0);
        else
            line_.forward(-2.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

using PriceVolumeRank = PVR;

} // namespace stratforge
