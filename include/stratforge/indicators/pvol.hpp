#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>

namespace stratforge {

/// Price-Volume: close * volume (raw money flow).
class PriceVolume : public Indicator<PriceVolume> {
public:
    PriceVolume(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        line_.forward(close_.data()[idx] * volume_.data()[idx]);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

using PVOL = PriceVolume;

} // namespace stratforge
