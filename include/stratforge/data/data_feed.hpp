#pragma once

#include <stratforge/core/error.hpp>
#include <stratforge/core/line.hpp>
#include <stratforge/data/timeframe.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>

namespace stratforge {

/// DateTime type alias using system_clock
using DateTime = std::chrono::system_clock::time_point;

/// Post-load validation result for data feed audit.
/// Read-only diagnostic — does NOT modify data.
struct ValidationResult {
    bool has_duplicates = false;
    bool is_monotonic = true;
    bool has_nan = false;
    bool has_negative_prices = false;
    std::size_t duplicate_count = 0;
    std::size_t nan_count = 0;
    std::size_t negative_price_count = 0;
};

/// DataFeed - Base data feed with standard OHLCV + datetime lines.
/// Derived classes implement load() to provide data from various sources.
class DataFeed {
public:
    DataFeed() = default;
    virtual ~DataFeed() = default;

    DataFeed(const DataFeed&) = delete;
    DataFeed& operator=(const DataFeed&) = delete;
    DataFeed(DataFeed&&) = default;
    DataFeed& operator=(DataFeed&&) = default;

    /// Load next bar into lines. Returns false when no more data.
    [[nodiscard]] virtual bool load() = 0;

    /// Load with explicit error reporting via std::expected.
    /// Default: delegates to load(). Returns success for both true and false —
    /// load() returning false is normal termination (no more data), not an error.
    /// Override in subclasses (e.g. CsvData) for richer error diagnostics.
    [[nodiscard]] virtual std::expected<void, DataError> load_with_error() {
        (void)load();
        return {};
    }

    /// Clone this data feed (creates a fresh, independent copy from the same source).
    /// Override in derived classes to enable use with the optimizer.
    [[nodiscard]] virtual std::unique_ptr<DataFeed> clone() const { return nullptr; }

    /// Preload all data into memory
    virtual void preload() {
        while (load()) {}
        datetime_.home();
        open_.home();
        high_.home();
        low_.home();
        close_.home();
        volume_.home();
        openinterest_.home();
    }

    /// Post-load validation: audit data for common issues.
    /// Does NOT modify data — read-only diagnostic.
    [[nodiscard]] ValidationResult validate() const noexcept {
        ValidationResult result;
        const auto& ts = datetime_.data();
        const auto sz = ts.size();

        // Timestamp checks
        for (std::size_t i = 1; i < sz; ++i) {
            if (ts[i] == ts[i - 1]) {
                result.has_duplicates = true;
                ++result.duplicate_count;
            } else if (ts[i] < ts[i - 1]) {
                result.is_monotonic = false;
            }
        }

        // OHLCV value checks
        auto check_line = [&](const Line<double>& line) {
            for (const auto& val : line.data()) {
                if (std::isnan(val) || std::isinf(val)) {
                    result.has_nan = true;
                    ++result.nan_count;
                }
            }
        };
        check_line(open_);
        check_line(high_);
        check_line(low_);
        check_line(close_);
        check_line(volume_);

        // Negative price checks (OHLC only, volume excluded)
        auto check_negative = [&](const Line<double>& line) {
            for (const auto& val : line.data()) {
                if (val < 0.0) ++result.negative_price_count;
            }
        };
        check_negative(open_);
        check_negative(high_);
        check_negative(low_);
        check_negative(close_);
        result.has_negative_prices = result.negative_price_count > 0;

        return result;
    }

    /// Advance all lines to next bar
    void advance() noexcept {
        datetime_.advance();
        open_.advance();
        high_.advance();
        low_.advance();
        close_.advance();
        volume_.advance();
        openinterest_.advance();
    }

    /// Number of bars loaded
    [[nodiscard]] std::size_t size() const noexcept {
        return close_.size();
    }

    /// Current bar index
    [[nodiscard]] std::size_t index() const noexcept {
        return close_.index();
    }

    /// Remaining bars from current position
    [[nodiscard]] std::size_t buflen() const noexcept {
        return close_.buflen();
    }

    // Standard OHLCV + datetime line accessors
    [[nodiscard]] const Line<DateTime>& datetime() const noexcept { return datetime_; }
    [[nodiscard]] const Line<double>& open() const noexcept { return open_; }
    [[nodiscard]] const Line<double>& high() const noexcept { return high_; }
    [[nodiscard]] const Line<double>& low() const noexcept { return low_; }
    [[nodiscard]] const Line<double>& close() const noexcept { return close_; }
    [[nodiscard]] const Line<double>& volume() const noexcept { return volume_; }
    [[nodiscard]] const Line<double>& openinterest() const noexcept { return openinterest_; }

    // Mutable accessors for derived classes
    [[nodiscard]] Line<DateTime>& datetime() noexcept { return datetime_; }
    [[nodiscard]] Line<double>& open() noexcept { return open_; }
    [[nodiscard]] Line<double>& high() noexcept { return high_; }
    [[nodiscard]] Line<double>& low() noexcept { return low_; }
    [[nodiscard]] Line<double>& close() noexcept { return close_; }
    [[nodiscard]] Line<double>& volume() noexcept { return volume_; }
    [[nodiscard]] Line<double>& openinterest() noexcept { return openinterest_; }

    /// Feed name (for identification)
    void set_name(std::string name) noexcept { name_ = std::move(name); }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    /// Timeframe
    void set_timeframe(TimeFrameCompression tf) noexcept { timeframe_ = tf; }
    [[nodiscard]] TimeFrameCompression timeframe() const noexcept { return timeframe_; }

protected:
    static constexpr std::size_t cache_line = 64;

    alignas(cache_line) Line<DateTime> datetime_;
    alignas(cache_line) Line<double> open_;
    alignas(cache_line) Line<double> high_;
    alignas(cache_line) Line<double> low_;
    alignas(cache_line) Line<double> close_;
    alignas(cache_line) Line<double> volume_;
    alignas(cache_line) Line<double> openinterest_;

private:
    std::string name_;
    TimeFrameCompression timeframe_;
};

} // namespace stratforge
