#pragma once

#include <stratforge/data/data_feed.hpp>
#include <stratforge/live/market_connector.hpp>

#include <chrono>
#include <cstddef>
#include <string>

namespace stratforge {

/// In-memory live feed backed by streaming market data snapshots.
class LiveDataFeed final : public DataFeed {
public:
    LiveDataFeed() = default;

    /// Append a fresh market snapshot as the current live bar.
    void push_snapshot(const MarketDataSnapshot& snapshot,
                       DateTime timestamp = std::chrono::system_clock::now()) {
        if (bars_pushed_ == 0) {
            open().forward(snapshot.last_price);
            high().forward(snapshot.last_price);
            low().forward(snapshot.last_price);
            close().forward(snapshot.last_price);
            volume().forward(snapshot.last_size);
            openinterest().forward(0.0);
            datetime().forward(timestamp);
        } else {
            open().forward(snapshot.last_price);
            high().forward(snapshot.last_price);
            low().forward(snapshot.last_price);
            close().forward(snapshot.last_price);
            volume().forward(snapshot.last_size);
            openinterest().forward(0.0);
            datetime().forward(timestamp);
        }

        last_snapshot_ = snapshot;
        ++bars_pushed_;
    }

    [[nodiscard]] bool load() override {
        return false;
    }

    [[nodiscard]] const MarketDataSnapshot& last_snapshot() const noexcept {
        return last_snapshot_;
    }

    [[nodiscard]] std::size_t bars_pushed() const noexcept {
        return bars_pushed_;
    }

private:
    MarketDataSnapshot last_snapshot_{};
    std::size_t bars_pushed_ = 0;
};

} // namespace stratforge
