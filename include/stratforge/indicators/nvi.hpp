#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Negative Volume Index: accumulates returns only on down-volume days.
class NVI : public Indicator<NVI> {
public:
    NVI(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();

        if (idx == 0) {
            nvi_ = 1000.0;
            line_.forward(nvi_);
            return;
        }

        const double prev_close = close_.data()[idx - 1];
        if (volume_.data()[idx] < volume_.data()[idx - 1] && prev_close != 0.0) {
            nvi_ += nvi_ * ((close_.data()[idx] - prev_close) / prev_close);
        }
        line_.forward(nvi_);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
    double nvi_ = 1000.0;
};

using NegativeVolumeIndex = NVI;

} // namespace stratforge
