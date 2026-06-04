#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <cstddef>
#include <limits>

namespace stratforge {

/// Tom DeMark Sequential: counts consecutive bars where
/// close > close[4] (buy setup) or close < close[4] (sell setup).
/// Outputs positive values for buy setup count, negative for sell.
class TDSequential : public Indicator<TDSequential> {
public:
    explicit TDSequential(const Line<double>& source, std::size_t lookback = 4uz)
        : source_(source), lookback_(lookback == 0 ? 4 : lookback) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();

        if (idx < lookback_) {
            line_.forward(0.0);
            return;
        }

        const double current = source_.data()[idx];
        const double compare = source_.data()[idx - lookback_];

        if (current > compare) {
            buy_count_ = (buy_count_ > 0) ? buy_count_ + 1 : 1;
            sell_count_ = 0;
        } else if (current < compare) {
            sell_count_ = (sell_count_ < 0) ? sell_count_ - 1 : -1;
            buy_count_ = 0;
        } else {
            buy_count_ = 0;
            sell_count_ = 0;
        }

        line_.forward(buy_count_ != 0 ? static_cast<double>(buy_count_)
                                       : static_cast<double>(sell_count_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return lookback_ + 1;
    }

private:
    const Line<double>& source_;
    std::size_t lookback_;
    int buy_count_ = 0;
    int sell_count_ = 0;
};

using TD_SEQ = TDSequential;
using TDSeq = TDSequential;

} // namespace stratforge
