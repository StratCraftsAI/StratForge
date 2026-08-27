#pragma once

/// @file candle_structure_common.hpp
/// @brief Shared candle-geometry primitives for TICKET_SF012.

#include <stratforge/indicators/atr.hpp>
#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace stratforge {

namespace candle_structure {

struct GeometryValues {
    double body;
    double range;
    double upper_wick;
    double lower_wick;
};

[[nodiscard]] inline bool valid_ohlc(double open, double high, double low,
                                     double close) noexcept {
    return std::isfinite(open) && std::isfinite(high) && std::isfinite(low) &&
           std::isfinite(close) && low <= std::min(open, close) &&
           std::max(open, close) <= high;
}

[[nodiscard]] inline GeometryValues geometry(double open, double high,
                                             double low, double close) noexcept {
    return {
        std::abs(close - open),
        high - low,
        high - std::max(open, close),
        std::min(open, close) - low,
    };
}

/// Shared finite-value rolling window for SF012 percentile and strict-extrema
/// detectors. A non-finite sample starts a new consecutive-valid run.
class RollingRankExtrema {
public:
    explicit RollingRankExtrema(std::size_t period) : period_(period) {
        if (period == 0uz) {
            throw std::invalid_argument("rolling period must be positive");
        }
    }

    void push(double value) {
        if (!std::isfinite(value)) {
            values_.clear();
            return;
        }
        values_.push_back(value);
        if (values_.size() > period_) values_.pop_front();
    }

    [[nodiscard]] bool ready() const noexcept { return values_.size() == period_; }
    [[nodiscard]] std::size_t period() const noexcept { return period_; }

    [[nodiscard]] double percent_rank() const noexcept {
        if (!ready()) return std::numeric_limits<double>::quiet_NaN();
        const double current = values_.back();
        const auto below = std::count_if(values_.begin(), values_.end(),
                                         [current](double value) { return value < current; });
        return static_cast<double>(below) / static_cast<double>(period_);
    }

    [[nodiscard]] bool strict_minimum() const noexcept {
        if (!ready()) return false;
        return std::all_of(values_.begin(), std::prev(values_.end()),
                           [current = values_.back()](double value) {
                               return current < value;
                           });
    }

    [[nodiscard]] bool strict_maximum() const noexcept {
        if (!ready()) return false;
        return std::all_of(values_.begin(), std::prev(values_.end()),
                           [current = values_.back()](double value) {
                               return current > value;
                           });
    }

private:
    std::size_t period_;
    std::deque<double> values_;
};

} // namespace candle_structure

enum class CandleMeasurement {
    body,
    range,
    upper_wick,
    lower_wick,
    gap,
};

/// Current-bar candle measurements. line() and body() are the real body.
class CandleGeometry : public Indicator<CandleGeometry> {
public:
    CandleGeometry(const Line<double>& open, const Line<double>& high,
                   const Line<double>& low, const Line<double>& close)
        : open_(open), high_(high), low_(low), close_(close) {}

    void next_impl() {
        reserve_if_needed();
        const auto idx = close_.index();
        const double open = open_.data()[idx];
        const double high = high_.data()[idx];
        const double low = low_.data()[idx];
        const double close = close_.data()[idx];
        const double nan = std::numeric_limits<double>::quiet_NaN();

        if (!candle_structure::valid_ohlc(open, high, low, close)) {
            forward_all(nan, nan, nan, nan, nan, nan);
            return;
        }

        const auto values = candle_structure::geometry(open, high, low, close);
        double signed_gap = nan;
        double gap = nan;
        if (idx > 0) {
            const double previous_open = open_.data()[idx - 1];
            const double previous_high = high_.data()[idx - 1];
            const double previous_low = low_.data()[idx - 1];
            const double previous_close = close_.data()[idx - 1];
            if (candle_structure::valid_ohlc(previous_open, previous_high,
                                             previous_low, previous_close)) {
                signed_gap = open - previous_close;
                gap = std::abs(signed_gap);
            }
        }
        forward_all(values.body, values.range, values.upper_wick,
                    values.lower_wick, signed_gap, gap);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2uz; }

    [[nodiscard]] std::size_t
    accessor_minimum_period_impl(std::string_view accessor) const noexcept {
        return accessor == "signed_gap" || accessor == "gap" ? 2uz : 1uz;
    }

    [[nodiscard]] const Line<double>& body() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& range() const noexcept { return range_; }
    [[nodiscard]] const Line<double>& upper_wick() const noexcept { return upper_wick_; }
    [[nodiscard]] const Line<double>& lower_wick() const noexcept { return lower_wick_; }
    [[nodiscard]] const Line<double>& signed_gap() const noexcept { return signed_gap_; }
    [[nodiscard]] const Line<double>& gap() const noexcept { return gap_; }

private:
    void reserve_if_needed() {
        if (!line_.empty()) return;
        const auto size = close_.size();
        reserve_output(size);
        range_.data().reserve(size);
        upper_wick_.data().reserve(size);
        lower_wick_.data().reserve(size);
        signed_gap_.data().reserve(size);
        gap_.data().reserve(size);
    }

    void forward_all(double body, double range, double upper_wick,
                     double lower_wick, double signed_gap, double gap) {
        line_.forward(body);
        range_.forward(range);
        upper_wick_.forward(upper_wick);
        lower_wick_.forward(lower_wick);
        signed_gap_.forward(signed_gap);
        gap_.forward(gap);
    }

    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    Line<double> range_;
    Line<double> upper_wick_;
    Line<double> lower_wick_;
    Line<double> signed_gap_;
    Line<double> gap_;
};

/// CandleGeometry divided by one shared Wilder ATR sample.
class NormalizedCandleGeometry : public Indicator<NormalizedCandleGeometry> {
public:
    NormalizedCandleGeometry(const Line<double>& open, const Line<double>& high,
                             const Line<double>& low, const Line<double>& close,
                             std::size_t atr_period = 14uz)
        : open_(open), high_(high), low_(low), close_(close),
          atr_(high, low, close, checked_period(atr_period)) {}

    void next_impl() {
        reserve_if_needed();
        atr_.next();

        const auto idx = close_.index();
        const double open = open_.data()[idx];
        const double high = high_.data()[idx];
        const double low = low_.data()[idx];
        const double close = close_.data()[idx];
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double atr = atr_.line().data().back();
        if (!candle_structure::valid_ohlc(open, high, low, close) ||
            !std::isfinite(atr) || atr == 0.0) {
            forward_all(nan, nan, nan, nan, nan, nan);
            return;
        }

        const auto values = candle_structure::geometry(open, high, low, close);
        double signed_gap = nan;
        double gap = nan;
        if (idx > 0) {
            const double previous_open = open_.data()[idx - 1];
            const double previous_high = high_.data()[idx - 1];
            const double previous_low = low_.data()[idx - 1];
            const double previous_close = close_.data()[idx - 1];
            if (candle_structure::valid_ohlc(previous_open, previous_high,
                                             previous_low, previous_close)) {
                signed_gap = (open - previous_close) / atr;
                gap = std::abs(signed_gap);
            }
        }
        forward_all(values.body / atr, values.range / atr,
                    values.upper_wick / atr, values.lower_wick / atr,
                    signed_gap, gap);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return atr_.minimum_period();
    }

    [[nodiscard]] std::size_t
    accessor_minimum_period_impl(std::string_view) const noexcept {
        return atr_.minimum_period();
    }

    [[nodiscard]] const Line<double>& body() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& range() const noexcept { return range_; }
    [[nodiscard]] const Line<double>& upper_wick() const noexcept { return upper_wick_; }
    [[nodiscard]] const Line<double>& lower_wick() const noexcept { return lower_wick_; }
    [[nodiscard]] const Line<double>& signed_gap() const noexcept { return signed_gap_; }
    [[nodiscard]] const Line<double>& gap() const noexcept { return gap_; }
    [[nodiscard]] std::size_t atr_period() const noexcept { return atr_.period(); }

private:
    [[nodiscard]] static std::size_t checked_period(std::size_t period) {
        if (period == 0uz) {
            throw std::invalid_argument("ATR period must be positive");
        }
        return period;
    }

    void reserve_if_needed() {
        if (!line_.empty()) return;
        const auto size = close_.size();
        reserve_output(size);
        range_.data().reserve(size);
        upper_wick_.data().reserve(size);
        lower_wick_.data().reserve(size);
        signed_gap_.data().reserve(size);
        gap_.data().reserve(size);
    }

    void forward_all(double body, double range, double upper_wick,
                     double lower_wick, double signed_gap, double gap) {
        line_.forward(body);
        range_.forward(range);
        upper_wick_.forward(upper_wick);
        lower_wick_.forward(lower_wick);
        signed_gap_.forward(signed_gap);
        gap_.forward(gap);
    }

    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    ATR atr_;
    Line<double> range_;
    Line<double> upper_wick_;
    Line<double> lower_wick_;
    Line<double> signed_gap_;
    Line<double> gap_;
};

/// Percent rank of a selected non-negative candle magnitude.
class CandlePercentile : public Indicator<CandlePercentile> {
public:
    CandlePercentile(const Line<double>& open, const Line<double>& high,
                     const Line<double>& low, const Line<double>& close,
                     CandleMeasurement measurement, std::size_t period = 50uz)
        : open_(open), high_(high), low_(low), close_(close),
          measurement_(measurement), window_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double open = open_.data()[idx];
        const double high = high_.data()[idx];
        const double low = low_.data()[idx];
        const double close = close_.data()[idx];
        const double nan = std::numeric_limits<double>::quiet_NaN();

        double value = nan;
        if (candle_structure::valid_ohlc(open, high, low, close)) {
            const auto values = candle_structure::geometry(open, high, low, close);
            switch (measurement_) {
            case CandleMeasurement::body: value = values.body; break;
            case CandleMeasurement::range: value = values.range; break;
            case CandleMeasurement::upper_wick: value = values.upper_wick; break;
            case CandleMeasurement::lower_wick: value = values.lower_wick; break;
            case CandleMeasurement::gap:
                if (idx > 0uz && candle_structure::valid_ohlc(
                        open_.data()[idx - 1uz], high_.data()[idx - 1uz],
                        low_.data()[idx - 1uz], close_.data()[idx - 1uz])) {
                    value = std::abs(open - close_.data()[idx - 1uz]);
                }
                break;
            }
        }

        window_.push(value);
        line_.forward(window_.percent_rank());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return window_.period() + (measurement_ == CandleMeasurement::gap ? 1uz : 0uz);
    }

    [[nodiscard]] CandleMeasurement measurement() const noexcept { return measurement_; }
    [[nodiscard]] std::size_t period() const noexcept { return window_.period(); }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    CandleMeasurement measurement_;
    candle_structure::RollingRankExtrema window_;
};

} // namespace stratforge
