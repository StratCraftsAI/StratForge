// SPDX-License-Identifier: MIT
//
// include/stratforge/data/in_memory_feed.hpp — Minimal in-memory DataFeed.
//
// : Header-only, no IO. Built for fixture injection, in-process
// replay, and the stratforge::evaluation::SignalEvaluator harness. Streams
// one bar per load() call (matching the DataFeed::load() abstract contract);
// callers drive it via DataFeed::preload() exactly as they would CsvData.

#pragma once

#include <stratforge/bar.hpp>
#include <stratforge/data/data_feed.hpp>

#include <cstddef>
#include <memory>
#include <span>

namespace stratforge {

class InMemoryFeed final : public DataFeed {
public:
    explicit InMemoryFeed(std::span<const Bar> bars) noexcept
        : bars_(bars) {}

    /// Append bars_[cursor_] to the OHLCV+datetime lines and increment the
    /// cursor. Returns false when the input span is exhausted. Matches the
    /// streaming DataFeed::load() contract — preload() loops this to EOF
    /// and then calls home() on every Line.
    [[nodiscard]] bool load() override {
        if (cursor_ >= bars_.size()) return false;
        const auto& b = bars_[cursor_++];
        datetime_.forward(b.timestamp);
        open_.forward(b.open);
        high_.forward(b.high);
        low_.forward(b.low);
        close_.forward(b.close);
        volume_.forward(b.volume);
        openinterest_.forward(0.0);
        return true;
    }

    /// Independent copy bound to the SAME underlying span (the caller must
    /// keep the span valid). Cursor resets to 0; the clone's Lines are
    /// empty until preload() is invoked.
    [[nodiscard]] std::unique_ptr<DataFeed> clone() const override {
        return std::make_unique<InMemoryFeed>(bars_);
    }

    /// Re-bind to a fresh bar span and reset Lines + cursor for reuse.
    /// Postcondition: subsequent preload() reflects the new span starting
    /// at bar 0.
    void reset(std::span<const Bar> bars) noexcept {
        bars_   = bars;
        cursor_ = 0;
        datetime_     = {};
        open_         = {};
        high_         = {};
        low_          = {};
        close_        = {};
        volume_       = {};
        openinterest_ = {};
    }

private:
    std::span<const Bar> bars_;
    std::size_t cursor_ = 0;
};

}  // namespace stratforge
