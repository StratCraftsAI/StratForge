#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Positive Volume Index: accumulates returns only on up-volume days.
class PVI : public Indicator<PVI> {
public:
    PVI(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();

        if (idx == 0) {
            pvi_ = 1000.0;
            line_.forward(pvi_);
            return;
        }

        const double prev_close = close_.data()[idx - 1];
        if (volume_.data()[idx] > volume_.data()[idx - 1] && prev_close != 0.0) {
            pvi_ += pvi_ * ((close_.data()[idx] - prev_close) / prev_close);
        }
        line_.forward(pvi_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    double pvi_ = 1000.0;
};

using PositiveVolumeIndex = PVI;

} // namespace stratforge
