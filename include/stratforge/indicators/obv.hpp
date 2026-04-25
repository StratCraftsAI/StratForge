#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>

namespace stratforge {

/// On-Balance Volume.
class OBV : public Indicator<OBV> {
public:
    OBV(const Line<double>& close, const Line<double>& volume)
        : close_(close), volume_(volume) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        if (idx == 0) {
            line_.forward(volume_.data()[0]);
            return;
        }

        const double prev = line_.data()[idx - 1];
        if (close_.data()[idx] > close_.data()[idx - 1]) {
            line_.forward(prev + volume_.data()[idx]);
        } else if (close_.data()[idx] < close_.data()[idx - 1]) {
            line_.forward(prev - volume_.data()[idx]);
        } else {
            line_.forward(prev);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& close_;
    const Line<double>& volume_;
};

} // namespace stratforge
