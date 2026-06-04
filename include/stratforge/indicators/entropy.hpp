#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/simd/simd_ops.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Shannon Entropy of normalized values over a trailing window.
/// Normalizes by sum-of-absolutes, then computes -sum(p * ln(p)).
class Entropy : public Indicator<Entropy> {
public:
    explicit Entropy(const Line<double>& source, std::size_t period = 10uz)
        : source_(source), period_(period == 0 ? 1 : period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const std::size_t start = idx - period_ + 1;
        double sum_abs = 0.0;
        for (std::size_t i = 0; i < period_; ++i)
            sum_abs += std::abs(source_.data()[start + i]);

        if (sum_abs == 0.0) {
            line_.forward(0.0);
            return;
        }

        double entropy = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double p = std::abs(source_.data()[start + i]) / sum_abs;
            if (p > 0.0) {
                entropy -= p * std::log(p);
            }
        }
        line_.forward(entropy);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] std::size_t period() const noexcept { return period_; }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using ENTROPY = Entropy;
using ShannonEntropy = Entropy;

} // namespace stratforge
