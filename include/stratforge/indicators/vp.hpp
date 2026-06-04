#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Volume Profile (simplified): rolling sum of volume weighted by
/// price position within high-low range. Outputs a normalized
/// volume concentration score.
class VolumeProfile : public Indicator<VolumeProfile> {
public:
    VolumeProfile(const Line<double>& close,
                  const Line<double>& high,
                  const Line<double>& low,
                  const Line<double>& volume,
                  std::size_t period = 10uz)
        : close_(close), high_(high), low_(low), volume_(volume)
        , period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double weighted_sum = 0.0;
        double vol_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const std::size_t pos = idx - i;
            const double range = high_.data()[pos] - low_.data()[pos];
            const double weight = (range == 0.0) ? 0.5
                : (close_.data()[pos] - low_.data()[pos]) / range;
            weighted_sum += weight * volume_.data()[pos];
            vol_sum += volume_.data()[pos];
        }

        line_.forward(vol_sum == 0.0 ? 0.5 : weighted_sum / vol_sum);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& close_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& volume_;
    std::size_t period_;
};

using VP = VolumeProfile;

} // namespace stratforge
