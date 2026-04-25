#pragma once

#include <stratforge/indicators/indicator.hpp>
#include <stratforge/indicators/ema.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/dema.hpp>
#include <stratforge/indicators/tema.hpp>
#include <stratforge/indicators/wma.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace stratforge {

/// MA type selector for MACDExt.
enum class MaType {
    SMA = 0,
    EMA = 1,
    WMA = 2,
    DEMA = 3,
    TEMA = 4,
};

/// MACD with configurable MA types for fast, slow, and signal lines.
class MACDExt : public Indicator<MACDExt> {
public:
    MACDExt(const Line<double>& source,
            std::size_t fast_period = 12,
            MaType fast_ma = MaType::EMA,
            std::size_t slow_period = 26,
            MaType slow_ma = MaType::EMA,
            std::size_t signal_period = 9,
            MaType signal_ma = MaType::EMA)
        : source_(source)
        , fast_period_(fast_period)
        , slow_period_(slow_period)
        , signal_period_(signal_period)
        , fast_(make_ma(source, fast_period, fast_ma))
        , slow_(make_ma(source, slow_period, slow_ma))
        , signal_ma_type_(signal_ma)
        , signal_ma_period_(signal_period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            macd_line_.data().reserve(n);
            signal_.data().reserve(n);
            histogram_.data().reserve(n);
        }
        fast_->next();
        slow_->next();

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double fast_val = fast_->line().data().back();
        const double slow_val = slow_->line().data().back();

        if (std::isnan(fast_val) || std::isnan(slow_val)) {
            line_.forward(nan);
            signal_.forward(nan);
            histogram_.forward(nan);
            return;
        }

        const double macd_val = fast_val - slow_val;
        line_.forward(macd_val);
        macd_line_.forward(macd_val);

        if (!signal_ind_) {
            signal_ind_ = make_ma(macd_line_, signal_ma_period_, signal_ma_type_);
            // Replay existing macd values
            macd_line_.home();
            for (std::size_t i = 0; i < macd_line_.size() - 1; ++i) {
                signal_ind_->next();
                macd_line_.advance();
            }
            signal_ind_->next();
        } else {
            signal_ind_->next();
        }

        const double sig = signal_ind_->line().data().back();
        signal_.forward(sig);

        if (std::isnan(sig)) {
            histogram_.forward(nan);
        } else {
            histogram_.forward(macd_val - sig);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return std::max(fast_period_, slow_period_) + signal_period_ - 1;
    }

    [[nodiscard]] const Line<double>& macd() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& signal() const noexcept { return signal_; }
    [[nodiscard]] const Line<double>& histogram() const noexcept { return histogram_; }

private:
    static std::unique_ptr<IndicatorBase> make_ma(const Line<double>& source,
                                               std::size_t period, MaType type) {
        switch (type) {
            case MaType::SMA:  return std::make_unique<SMA>(source, period);
            case MaType::EMA:  return std::make_unique<EMA>(source, period);
            case MaType::WMA:  return std::make_unique<WMA>(source, period);
            case MaType::DEMA: return std::make_unique<DEMA>(source, period);
            case MaType::TEMA: return std::make_unique<TEMA>(source, period);
        }
        return std::make_unique<EMA>(source, period);
    }

    const Line<double>& source_;
    std::size_t fast_period_;
    std::size_t slow_period_;
    std::size_t signal_period_;
    std::unique_ptr<IndicatorBase> fast_;
    std::unique_ptr<IndicatorBase> slow_;
    MaType signal_ma_type_;
    std::size_t signal_ma_period_;
    std::unique_ptr<IndicatorBase> signal_ind_;
    Line<double> macd_line_;
    Line<double> signal_;
    Line<double> histogram_;
};

using MACDEXT = MACDExt;

} // namespace stratforge
