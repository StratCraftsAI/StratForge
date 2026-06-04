#pragma once

#include <stratforge/indicators/directionalmovement.hpp>

namespace stratforge {

/// Directional Index (DX) as a standalone indicator.
/// Wraps DirectionalMovement and exposes the dx line.
class DX : public Indicator<DX> {
public:
    DX(const Line<double>& high,
       const Line<double>& low,
       const Line<double>& close,
       std::size_t period = 14uz)
        : high_(high), dm_(high, low, close, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        dm_.next();
        line_.forward(dm_.dx().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return dm_.minimum_period();
    }

    [[nodiscard]] const Line<double>& plus_di() const noexcept { return dm_.plus_di(); }
    [[nodiscard]] const Line<double>& minus_di() const noexcept { return dm_.minus_di(); }

private:
    const Line<double>& high_;
    DirectionalMovement dm_;
};

using DirectionalIndex = DX;

} // namespace stratforge
