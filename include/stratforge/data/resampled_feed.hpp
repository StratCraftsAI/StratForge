// SPDX-License-Identifier: MIT
//
// include/stratforge/data/resampled_feed.hpp -- Calendar-aligned OHLCV resampler.
//
//  P1: Constructs a coarser-timeframe DataFeed from a preloaded
// base feed using the period_start calendar bucketing from interval.hpp.
// After preload() the result is indistinguishable from a native DataFeed --
// the alignment scheduler in Cerebro treats it identically to file-loaded
// feeds.
//
// Aggregation rules per bucket:
//   open   = first bar's open
//   high   = max of all bars' highs
//   low    = min of all bars' lows
//   close  = last bar's close
//   volume = sum of all bars' volumes
//
// Bucket timestamps use the period_start of the bucket (the first instant of
// that period). This is NOT the backtrader convention (end-of-period); the
// alignment scheduler in Cerebro uses period_end on the bar's open timestamp
// to decide visibility, so the precise timestamp convention does not affect
// correctness -- what matters is that every bar within a bucket maps to the
// same period_start.

#pragma once

#include <stratforge/data/data_feed.hpp>
#include <stratforge/data/interval.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace stratforge {

class ResampledFeed final : public DataFeed {
public:
    /// Construct a resampled feed from a base feed and a target timeframe.
    /// The base feed MUST already be preloaded before this feed's preload()
    /// is called. The target TFC must be coarser than the base's timeframe.
    ResampledFeed(const DataFeed& base, TimeFrameCompression target)
        : base_(base), target_(target) {
        set_timeframe(target);
        set_name(base.name().empty()
                     ? "resampled"
                     : base.name() + "_resampled");
    }

    /// load() is a no-op: all data is populated during preload().
    [[nodiscard]] bool load() override { return false; }

    /// Aggregate the base feed's bars into calendar-aligned buckets.
    void preload() override {
        const std::size_t n = base_.size();
        if (n == 0) return;

        // Reserve estimated output size
        const std::size_t estimated = std::max(std::size_t{1}, n / 20);
        datetime().data().reserve(estimated);
        open().data().reserve(estimated);
        high().data().reserve(estimated);
        low().data().reserve(estimated);
        close().data().reserve(estimated);
        volume().data().reserve(estimated);
        openinterest().data().reserve(estimated);

        const auto& dt_data = base_.datetime().data();
        const auto& o_data  = base_.open().data();
        const auto& h_data  = base_.high().data();
        const auto& l_data  = base_.low().data();
        const auto& c_data  = base_.close().data();
        const auto& v_data  = base_.volume().data();

        DateTime current_bucket_start = period_start(dt_data[0], target_);
        double bucket_o = o_data[0];
        double bucket_h = h_data[0];
        double bucket_l = l_data[0];
        double bucket_c = c_data[0];
        double bucket_v = v_data[0];

        for (std::size_t i = 1; i < n; ++i) {
            DateTime bar_bucket = period_start(dt_data[i], target_);

            if (bar_bucket != current_bucket_start) {
                // Emit completed bucket
                emit_bar(current_bucket_start, bucket_o, bucket_h,
                         bucket_l, bucket_c, bucket_v);

                // Start new bucket
                current_bucket_start = bar_bucket;
                bucket_o = o_data[i];
                bucket_h = h_data[i];
                bucket_l = l_data[i];
                bucket_c = c_data[i];
                bucket_v = v_data[i];
            } else {
                // Accumulate into current bucket
                bucket_h = std::max(bucket_h, h_data[i]);
                bucket_l = std::min(bucket_l, l_data[i]);
                bucket_c = c_data[i];
                bucket_v += v_data[i];
            }
        }

        // Emit final bucket
        emit_bar(current_bucket_start, bucket_o, bucket_h,
                 bucket_l, bucket_c, bucket_v);

        // Reset cursors to home position
        datetime().home();
        open().home();
        high().home();
        low().home();
        close().home();
        volume().home();
        openinterest().home();
    }

private:
    void emit_bar(DateTime ts, double o, double h,
                  double l, double c, double v) {
        datetime().data().push_back(ts);
        open().data().push_back(o);
        high().data().push_back(h);
        low().data().push_back(l);
        close().data().push_back(c);
        volume().data().push_back(v);
        openinterest().data().push_back(0.0);
    }

    const DataFeed& base_;
    TimeFrameCompression target_;
};

} // namespace stratforge
