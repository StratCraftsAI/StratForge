#pragma once

#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/lowest.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// Midpoint over period: (Highest + Lowest) / 2 of a single source line.
class MidPoint : public Indicator<MidPoint> {
public:
    explicit MidPoint(const Line<double>& source, std::size_t period = 14)
        : source_(source), highest_(source, period), lowest_(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        highest_.next();
        lowest_.next();

        const double h = highest_.line().data().back();
        const double l = lowest_.line().data().back();
        if (std::isnan(h) || std::isnan(l)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward((h + l) / 2.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return highest_.minimum_period();
    }

private:
    const Line<double>& source_;
    Highest highest_;
    Lowest lowest_;
};

using MIDPOINT = MidPoint;

/// Midpoint Price: (HighestHigh + LowestLow) / 2 over period.
class MidPrice : public Indicator<MidPrice> {
public:
    MidPrice(const Line<double>& high, const Line<double>& low, std::size_t period = 14)
        : high_(high), highest_(high, period), lowest_(low, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(high_.size()); }
        highest_.next();
        lowest_.next();

        const double h = highest_.line().data().back();
        const double l = lowest_.line().data().back();
        if (std::isnan(h) || std::isnan(l)) {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        line_.forward((h + l) / 2.0);
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return highest_.minimum_period();
    }

private:
    const Line<double>& high_;
    Highest highest_;
    Lowest lowest_;
};

using MIDPRICE = MidPrice;

} // namespace stratforge
