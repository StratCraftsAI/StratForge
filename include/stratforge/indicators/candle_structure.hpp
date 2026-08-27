#pragma once

/// @file candle_structure.hpp
/// @brief Streaming candle-structure detectors for TICKET_SF012.

#include <stratforge/indicators/candle_structure_common.hpp>
#include <stratforge/indicators/indicator.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace stratforge {

enum class BoundaryMode {
    strict,
    inclusive,
};

namespace candle_structure {

[[nodiscard]] inline bool valid_high_low(double high, double low) noexcept {
    return std::isfinite(high) && std::isfinite(low) && low <= high;
}

} // namespace candle_structure

class OutsideBar : public Indicator<OutsideBar> {
public:
    OutsideBar(const Line<double>& high, const Line<double>& low,
               BoundaryMode boundary = BoundaryMode::inclusive)
        : high_(high), low_(low), boundary_(boundary) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        const auto idx = high_.index();
        double result = 0.0;
        if (idx > 0uz) {
            const double high = high_.data()[idx];
            const double low = low_.data()[idx];
            const double previous_high = high_.data()[idx - 1uz];
            const double previous_low = low_.data()[idx - 1uz];
            if (candle_structure::valid_high_low(high, low) &&
                candle_structure::valid_high_low(previous_high, previous_low)) {
                result = boundary_ == BoundaryMode::inclusive
                             ? (high >= previous_high && low <= previous_low)
                             : (high > previous_high && low < previous_low);
            }
        }
        line_.forward(result);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 2uz; }
    [[nodiscard]] BoundaryMode boundary() const noexcept { return boundary_; }

private:
    const Line<double>& high_;
    const Line<double>& low_;
    BoundaryMode boundary_;
};

class InsideBarDepth : public Indicator<InsideBarDepth> {
public:
    InsideBarDepth(const Line<double>& high, const Line<double>& low,
                   BoundaryMode boundary = BoundaryMode::inclusive)
        : high_(high), low_(low), boundary_(boundary) {}

    void next_impl() {
        reserve_if_needed();
        const auto idx = high_.index();
        const double high = high_.data()[idx];
        const double low = low_.data()[idx];
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (!candle_structure::valid_high_low(high, low)) {
            has_mother_ = false;
            depth_ = 0uz;
            forward_all(0.0, nan, 0.0);
            return;
        }

        if (!has_mother_) {
            set_mother(high, low);
            forward_all(0.0, high - low, 0.0);
            return;
        }

        const bool contained = boundary_ == BoundaryMode::inclusive
                                   ? (high <= mother_high_ && low >= mother_low_)
                                   : (high < mother_high_ && low > mother_low_);
        if (contained) {
            ++depth_;
            forward_all(static_cast<double>(depth_), mother_high_ - mother_low_, 0.0);
            return;
        }

        const bool above = boundary_ == BoundaryMode::inclusive
                               ? high > mother_high_
                               : high >= mother_high_;
        const bool below = boundary_ == BoundaryMode::inclusive
                               ? low < mother_low_
                               : low <= mother_low_;
        const double breakout = above == below ? 0.0 : (above ? 1.0 : -1.0);
        set_mother(high, low);
        forward_all(0.0, high - low, breakout);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return 1uz; }
    [[nodiscard]] std::size_t
    accessor_minimum_period_impl(std::string_view) const noexcept { return 1uz; }
    [[nodiscard]] const Line<double>& depth() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& mother_range() const noexcept { return mother_range_; }
    [[nodiscard]] const Line<double>& breakout() const noexcept { return breakout_; }
    [[nodiscard]] BoundaryMode boundary() const noexcept { return boundary_; }

private:
    void reserve_if_needed() {
        if (!line_.empty()) return;
        reserve_output(high_.size());
        mother_range_.data().reserve(high_.size());
        breakout_.data().reserve(high_.size());
    }

    void set_mother(double high, double low) noexcept {
        has_mother_ = true;
        mother_high_ = high;
        mother_low_ = low;
        depth_ = 0uz;
    }

    void forward_all(double depth, double mother_range, double breakout) {
        line_.forward(depth);
        mother_range_.forward(mother_range);
        breakout_.forward(breakout);
    }

    const Line<double>& high_;
    const Line<double>& low_;
    BoundaryMode boundary_;
    bool has_mother_ = false;
    double mother_high_ = 0.0;
    double mother_low_ = 0.0;
    std::size_t depth_ = 0uz;
    Line<double> mother_range_;
    Line<double> breakout_;
};

template <bool Narrow>
class RangeExtremaDetector : public Indicator<RangeExtremaDetector<Narrow>> {
public:
    RangeExtremaDetector(const Line<double>& open, const Line<double>& high,
                         const Line<double>& low, const Line<double>& close,
                         std::size_t period)
        : open_(open), high_(high), low_(low), close_(close), window_(period) {}

    void next_impl() {
        if (this->line_.empty()) [[unlikely]] { this->reserve_output(close_.size()); }
        const auto idx = close_.index();
        const double open = open_.data()[idx];
        const double high = high_.data()[idx];
        const double low = low_.data()[idx];
        const double close = close_.data()[idx];
        window_.push(candle_structure::valid_ohlc(open, high, low, close)
                         ? high - low
                         : std::numeric_limits<double>::quiet_NaN());
        const bool matched = Narrow ? window_.strict_minimum() : window_.strict_maximum();
        this->line_.forward(matched ? 1.0 : 0.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return window_.period();
    }
    [[nodiscard]] std::size_t period() const noexcept { return window_.period(); }

private:
    const Line<double>& open_;
    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    candle_structure::RollingRankExtrema window_;
};

using NarrowRange = RangeExtremaDetector<true>;
using WideRange = RangeExtremaDetector<false>;

} // namespace stratforge
