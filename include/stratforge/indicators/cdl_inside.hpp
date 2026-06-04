#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Inside Bar: 1.0 when current bar's range is entirely within prior bar's range.
class CdlInside : public Indicator<CdlInside> {
public:
    CdlInside(const Line<double>& high, const Line<double>& low)
        : high_(high), low_(low) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();

        if (idx == 0) {
            line_.forward(0.0);
            return;
        }

        const bool inside = high_.data()[idx] <= high_.data()[idx - 1] &&
                            low_.data()[idx] >= low_.data()[idx - 1];
        line_.forward(inside ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 2;
    }

private:
    const Line<double>& high_;
    const Line<double>& low_;
};

using CDL_INSIDE = CdlInside;
using InsideBar = CdlInside;

} // namespace stratforge
