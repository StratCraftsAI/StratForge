#pragma once

#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/lowest.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace stratforge {

/// MinMax: outputs both the minimum and maximum over a rolling period.
class MinMax : public Indicator<MinMax> {
public:
    explicit MinMax(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), highest_(source, period), lowest_(source, period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            min_.data().reserve(n);
        }
        highest_.next();
        lowest_.next();

        line_.forward(highest_.line().data().back());
        min_.forward(lowest_.line().data().back());
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return highest_.minimum_period();
    }

    [[nodiscard]] const Line<double>& max() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& min() const noexcept { return min_; }

private:
    const Line<double>& source_;
    Highest highest_;
    Lowest lowest_;
    Line<double> min_;
};

using MINMAX = MinMax;

/// MaxIndex: index (bars ago) of the maximum over a rolling period.
class MaxIndex : public Indicator<MaxIndex> {
public:
    explicit MaxIndex(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double highest = source_.data()[idx];
        std::size_t best_ago = 0;
        for (std::size_t i = 1; i < period_; ++i) {
            const double val = source_.data()[idx - i];
            if (val > highest) {
                highest = val;
                best_ago = i;
            }
        }

        line_.forward(static_cast<double>(best_ago));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using MAXINDEX = MaxIndex;

/// MinIndex: index (bars ago) of the minimum over a rolling period.
class MinIndex : public Indicator<MinIndex> {
public:
    explicit MinIndex(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double lowest = source_.data()[idx];
        std::size_t best_ago = 0;
        for (std::size_t i = 1; i < period_; ++i) {
            const double val = source_.data()[idx - i];
            if (val < lowest) {
                lowest = val;
                best_ago = i;
            }
        }

        line_.forward(static_cast<double>(best_ago));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

private:
    const Line<double>& source_;
    std::size_t period_;
};

using MININDEX = MinIndex;

/// MinMaxIndex: indices of both min and max over a rolling period.
class MinMaxIndex : public Indicator<MinMaxIndex> {
public:
    explicit MinMaxIndex(const Line<double>& source, std::size_t period = 14uz)
        : source_(source), period_(period) {}

    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n);
            min_idx_.data().reserve(n);
        }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN());
            min_idx_.forward(std::numeric_limits<double>::quiet_NaN());
            return;
        }

        double highest = source_.data()[idx];
        double lowest = source_.data()[idx];
        std::size_t max_ago = 0;
        std::size_t min_ago = 0;

        for (std::size_t i = 1; i < period_; ++i) {
            const double val = source_.data()[idx - i];
            if (val > highest) {
                highest = val;
                max_ago = i;
            }
            if (val < lowest) {
                lowest = val;
                min_ago = i;
            }
        }

        line_.forward(static_cast<double>(max_ago));
        min_idx_.forward(static_cast<double>(min_ago));
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return period_;
    }

    [[nodiscard]] const Line<double>& max_index() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& min_index() const noexcept { return min_idx_; }

private:
    const Line<double>& source_;
    std::size_t period_;
    Line<double> min_idx_;
};

using MINMAXINDEX = MinMaxIndex;

} // namespace stratforge
