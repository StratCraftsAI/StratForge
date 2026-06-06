#pragma once

#include <stratforge/indicators/indicator.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace stratforge {

class Ichimoku : public Indicator<Ichimoku> {
public:
    Ichimoku(const Line<double>& high,
             const Line<double>& low,
             const Line<double>& close,
             std::size_t tenkan = 9uz,
             std::size_t kijun = 26uz,
             std::size_t senkou = 52uz,
             std::size_t senkou_lead = 26uz,
             std::size_t chikou = 26uz)
        : high_(high)
        , low_(low)
        , close_(close)
        , tenkan_(tenkan == 0 ? 1 : tenkan)
        , kijun_(kijun == 0 ? 1 : kijun)
        , senkou_(senkou == 0 ? 1 : senkou)
        , senkou_lead_(senkou_lead)
        , chikou_(chikou)
        , senkou_a_buf_(senkou_lead, std::numeric_limits<double>::quiet_NaN())
        , senkou_b_buf_(senkou_lead, std::numeric_limits<double>::quiet_NaN()) {}

    void next_impl() {
        const std::size_t idx = bar_count_++;
        const double nan = std::numeric_limits<double>::quiet_NaN();

        // -- tenkan_sen (primary output line_) --
        double tenkan_value = nan;
        if (idx + 1 >= tenkan_) {
            tenkan_value = midpoint(idx, tenkan_);
        }
        line_.forward(tenkan_value);

        // -- kijun_sen --
        double kijun_value = nan;
        if (idx + 1 >= kijun_) {
            kijun_value = midpoint(idx, kijun_);
        }
        kijun_sen_.forward(kijun_value);

        // -- senkou_span_a (forward-shifted by senkou_lead_ bars) --
        // Compute the value that WILL appear senkou_lead_ bars from now,
        // push it into the ring buffer. Emit the oldest buffered value
        // as this bar's senkou_span_a output.
        double sa_pending = nan;
        if (!std::isnan(tenkan_value) && !std::isnan(kijun_value)) {
            sa_pending = (tenkan_value + kijun_value) / 2.0;
        }
        senkou_span_a_.forward(senkou_a_buf_[sa_cursor_]);
        senkou_a_buf_[sa_cursor_] = sa_pending;

        // -- senkou_span_b (forward-shifted by senkou_lead_ bars) --
        double sb_pending = nan;
        if (idx + 1 >= senkou_) {
            sb_pending = midpoint(idx, senkou_);
        }
        senkou_span_b_.forward(senkou_b_buf_[sb_cursor_]);
        senkou_b_buf_[sb_cursor_] = sb_pending;

        // Advance ring cursors (shared period = senkou_lead_)
        sa_cursor_ = (sa_cursor_ + 1) % senkou_lead_buf_size();
        sb_cursor_ = (sb_cursor_ + 1) % senkou_lead_buf_size();

        // -- chikou_span (back-shifted: current close appears chikou_ bars ago) --
        // For bar idx, chikou_span[idx] is the close from bar idx + chikou_
        // (which hasn't arrived yet). So at bar idx we can fill
        // chikou_span[idx - chikou_] = close[idx].
        // Bars 0..chikou_-1 emit NaN; from bar chikou_ onward we
        // retroactively fill the slot that is now chikou_ bars old.
        if (idx < chikou_) {
            chikou_span_.forward(nan);
        } else {
            // Overwrite the slot written chikou_ bars ago (was NaN),
            // then forward the current slot (which will be filled later
            // or remain NaN if this is one of the trailing bars).
            chikou_span_.data()[idx - chikou_] = close_.data()[idx];
            chikou_span_.forward(nan);
        }
    }

    [[nodiscard]] std::size_t minimum_period_impl() const noexcept {
        return tenkan_;
    }

    [[nodiscard]] const Line<double>& tenkan_sen() const noexcept { return line_; }
    [[nodiscard]] const Line<double>& kijun_sen() const noexcept { return kijun_sen_; }
    [[nodiscard]] const Line<double>& senkou_span_a() const noexcept { return senkou_span_a_; }
    [[nodiscard]] const Line<double>& senkou_span_b() const noexcept { return senkou_span_b_; }
    [[nodiscard]] const Line<double>& chikou_span() const noexcept { return chikou_span_; }

private:
    [[nodiscard]] std::size_t senkou_lead_buf_size() const noexcept {
        return senkou_lead_ == 0 ? 1 : senkou_lead_;
    }

    [[nodiscard]] double midpoint(std::size_t idx, std::size_t period) const noexcept {
        double highest = high_.data()[idx];
        double lowest = low_.data()[idx];
        for (std::size_t i = 1; i < period; ++i) {
            highest = std::max(highest, high_.data()[idx - i]);
            lowest = std::min(lowest, low_.data()[idx - i]);
        }
        return (highest + lowest) / 2.0;
    }

    const Line<double>& high_;
    const Line<double>& low_;
    const Line<double>& close_;
    std::size_t tenkan_;
    std::size_t kijun_;
    std::size_t senkou_;
    std::size_t senkou_lead_;
    std::size_t chikou_;

    std::size_t bar_count_ = 0uz;

    // Ring buffers for forward-shifted senkou outputs. Each holds
    // senkou_lead_ pending values. On each bar the oldest pending
    // value is emitted and replaced with this bar's computed value.
    std::vector<double> senkou_a_buf_;
    std::vector<double> senkou_b_buf_;
    std::size_t sa_cursor_ = 0uz;
    std::size_t sb_cursor_ = 0uz;

    Line<double> kijun_sen_;
    Line<double> senkou_span_a_;
    Line<double> senkou_span_b_;
    Line<double> chikou_span_;
};

} // namespace stratforge
