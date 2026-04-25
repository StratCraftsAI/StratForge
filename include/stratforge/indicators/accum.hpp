#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>

namespace stratforge {

/// Cumulative sum of source values with an optional seed.
class Accum : public Indicator<Accum> {
public:
    explicit Accum(const Line<double>& source, double seed = 0.0)
        : source_(source), seed_(seed) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx == 0) {
            line_.forward(seed_ + source_.data()[idx]);
            return;
        }

        line_.forward(line_.data().back() + source_.data()[idx]);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return 1;
    }

private:
    const Line<double>& source_;
    double seed_;
};

using CumSum = Accum;
using CumulativeSum = Accum;

} // namespace stratforge
