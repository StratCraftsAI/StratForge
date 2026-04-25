#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/rsi.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Stochastic RSI: applies a Stochastic formula to RSI values.
/// %K = (RSI - LowestRSI) / (HighestRSI - LowestRSI) * 100
/// %D = SMA(%K, period_d)
class StochRSI : public Indicator<StochRSI> {
public:
    explicit StochRSI(const Line<double>& source,
                      std::size_t rsi_period = 14,
                      std::size_t stoch_period = 14,
                      std::size_t period_d = 3)
        : source_(source)
        , rsi_(source, rsi_period)
        , stoch_period_(stoch_period)
        , period_d_(period_d) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            perc_d_.data().reserve(n);
        }
        rsi_.next();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double rsi_val = rsi_.line().data().back();

        if (std::isnan(rsi_val)) {
            line_.forward(nan);
            perc_d_.forward(nan);
            return;
        }

        rsi_valid_count_++;
        if (rsi_valid_count_ < stoch_period_) {
            line_.forward(nan);
            perc_d_.forward(nan);
            return;
        }

        // Find highest and lowest RSI over stoch_period
        const auto& rsi_data = rsi_.line().data();
        const std::size_t rsi_size = rsi_data.size();
        double highest = rsi_data[rsi_size - 1];
        double lowest = rsi_data[rsi_size - 1];
        for (std::size_t i = 1; i < stoch_period_; ++i) {
            const double v = rsi_data[rsi_size - 1 - i];
            if (v > highest) highest = v;
            if (v < lowest) lowest = v;
        }

        const double range = highest - lowest;
        const double k = (range == 0.0) ? 50.0 : ((rsi_val - lowest) / range) * 100.0;
        line_.forward(k);

        k_valid_count_++;
        if (k_valid_count_ < period_d_) {
            perc_d_.forward(nan);
            return;
        }

        // SMA of %K for %D
        double sum = 0.0;
        const auto& k_data = line_.data();
        const std::size_t k_size = k_data.size();
        for (std::size_t i = 0; i < period_d_; ++i) {
            sum += k_data[k_size - 1 - i];
        }
        perc_d_.forward(sum / static_cast<double>(period_d_));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return rsi_.minimum_period() + stoch_period_ - 1;
    }

    [[nodiscard]] const Line<double>& percK() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& percD() const noexcept { return perc_d_; }

private:
    const Line<double>& source_;
    RSI rsi_;
    std::size_t stoch_period_;
    std::size_t period_d_;
    std::size_t rsi_valid_count_ = 0;
    std::size_t k_valid_count_ = 0;
    Line<double> perc_d_;
};

using STOCHRSI = StochRSI;
using StochasticRSI = StochRSI;

} // namespace stratforge
