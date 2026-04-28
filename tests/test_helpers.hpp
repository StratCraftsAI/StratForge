// SPDX-License-Identifier: MIT
//
// tests/test_helpers.hpp — Shared test fixtures and helpers for stratforge_tests.
//
// This header centralizes fixtures that were previously duplicated across
// 14+ test files (StaticFeed, InMemoryFeed, make_line, run_indicator,
// run_indicator_hl/hlc, run_ohlc, tmp_path, write_csv, generate_*).
//
// Convention reference: CLAUDE.md `Test Case Convention`
// Ticket:  Phase A

#pragma once

#include <stratforge/core/line.hpp>
#include <stratforge/data/data_feed.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace stratforge::test {

// ============================================================================
// Line<double> construction
// ============================================================================

/// Build a `Line<double>` from a vector of values; resets to home() position.
inline ::stratforge::Line<double> make_line(const std::vector<double>& values) {
    ::stratforge::Line<double> line;
    for (double value : values) {
        line.forward(value);
    }
    line.home();
    return line;
}

// ============================================================================
// Indicator drive loops
// ============================================================================

/// Drive a single-source indicator over the entire `source` line.
template <typename IndicatorType>
void run_indicator(::stratforge::Line<double>& source, IndicatorType& indicator) {
    for (std::size_t i = 0; i < source.size(); ++i) {
        indicator.next();
        if (i + 1 < source.size()) {
            source.advance();
        }
    }
}

/// Drive a HL-source indicator (e.g. Aroon).
template <typename IndicatorType>
void run_indicator_hl(::stratforge::Line<double>& high, ::stratforge::Line<double>& low,
                      IndicatorType& indicator) {
    for (std::size_t i = 0; i < high.size(); ++i) {
        indicator.next();
        if (i + 1 < high.size()) {
            high.advance();
            low.advance();
        }
    }
}

/// Drive a HLC-source indicator (e.g. ATR, DirectionalMovement).
template <typename IndicatorType>
void run_indicator_hlc(::stratforge::Line<double>& high, ::stratforge::Line<double>& low,
                       ::stratforge::Line<double>& close, IndicatorType& indicator) {
    for (std::size_t i = 0; i < close.size(); ++i) {
        indicator.next();
        if (i + 1 < close.size()) {
            high.advance();
            low.advance();
            close.advance();
        }
    }
}

/// Drive an OHLC-source indicator (e.g. candlestick patterns).
template <typename IndicatorType>
void run_indicator_ohlc(::stratforge::Line<double>& o, ::stratforge::Line<double>& h,
                        ::stratforge::Line<double>& l, ::stratforge::Line<double>& c,
                        IndicatorType& indicator) {
    for (std::size_t i = 0; i < c.size(); ++i) {
        indicator.next();
        if (i + 1 < c.size()) {
            o.advance();
            h.advance();
            l.advance();
            c.advance();
        }
    }
}

// ============================================================================
// Programmatic data generators
// ============================================================================

/// base + sin(i * freq) * amplitude (deterministic, useful for SIMD parity tests).
inline std::vector<double> generate_sine_data(std::size_t count,
                                              double base = 100.0,
                                              double amplitude = 10.0,
                                              double freq = 0.3) {
    std::vector<double> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        values.push_back(base + std::sin(static_cast<double>(i) * freq) * amplitude);
    }
    return values;
}

/// Linear trend: start + i * step.
inline std::vector<double> generate_trend_data(std::size_t count,
                                               double start = 100.0,
                                               double step = 1.0) {
    std::vector<double> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        values.push_back(start + static_cast<double>(i) * step);
    }
    return values;
}

// ============================================================================
// Temp file helpers
// ============================================================================

/// Resolve a filename under the OS temp directory.
inline std::string tmp_path(const std::string& filename) {
    return (std::filesystem::temp_directory_path() / filename).string();
}

/// Write `content` to a temp CSV file and return the absolute path.
/// `prefix` is prepended to the filename (default empty) for namespacing.
inline std::string write_csv(const std::string& name,
                             const std::string& content,
                             const std::string& prefix = "") {
    const std::string path = tmp_path(prefix + name);
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

// ============================================================================
// StaticFeed — in-memory OHLCV DataFeed for strategy/broker/cerebro tests
// ============================================================================

/// Programmatic in-memory `DataFeed` with a fixed bar list.
///
/// - bars are emitted on daily timestamps starting from epoch
/// - volume defaults to 1000.0 + i*1.0 (matches majority of legacy tests).
///   Pass `volume_step = 0.0` for constant volume.
/// - clone() returns a deep copy preserving feed name (required by Optimizer)
class StaticFeed final : public ::stratforge::DataFeed {
public:
    struct Bar {
        double open = 0.0;
        double high = 0.0;
        double low = 0.0;
        double close = 0.0;
    };

    explicit StaticFeed(std::vector<Bar> bars,
                        std::string feed_name = "",
                        double volume_base = 1000.0,
                        double volume_step = 1.0)
        : bars_(std::move(bars))
        , volume_base_(volume_base)
        , volume_step_(volume_step) {
        if (!feed_name.empty()) {
            set_name(feed_name);
        }
    }

    [[nodiscard]] bool load() override {
        if (loaded_) {
            return false;
        }
        const auto base = std::chrono::system_clock::time_point{};
        for (std::size_t i = 0; i < bars_.size(); ++i) {
            const auto& bar = bars_[i];
            datetime().forward(base + std::chrono::hours(24 * static_cast<int>(i)));
            open().forward(bar.open);
            high().forward(bar.high);
            low().forward(bar.low);
            close().forward(bar.close);
            volume().forward(volume_base_ + volume_step_ * static_cast<double>(i));
            openinterest().forward(0.0);
        }
        datetime().home();
        open().home();
        high().home();
        low().home();
        close().home();
        volume().home();
        openinterest().home();
        loaded_ = true;
        return !bars_.empty();
    }

    [[nodiscard]] std::unique_ptr<::stratforge::DataFeed> clone() const override {
        auto cloned = std::make_unique<StaticFeed>(bars_, name(), volume_base_, volume_step_);
        return cloned;
    }

private:
    std::vector<Bar> bars_;
    double volume_base_ = 1000.0;
    double volume_step_ = 0.0;
    bool loaded_ = false;
};

// ============================================================================
// InMemoryFeed — in-memory feed with explicit DateTime + OHLCV per bar
// ============================================================================

/// Programmatic in-memory `DataFeed` with caller-supplied DateTime per bar.
/// Used by data robustness tests where exact timestamps matter.
class InMemoryFeed final : public ::stratforge::DataFeed {
public:
    struct Bar {
        ::stratforge::DateTime dt;
        double o = 0.0;
        double h = 0.0;
        double l = 0.0;
        double c = 0.0;
        double v = 0.0;
    };

    explicit InMemoryFeed(std::vector<Bar> bars) : bars_(std::move(bars)) {}

    [[nodiscard]] bool load() override {
        if (loaded_) {
            return false;
        }
        loaded_ = true;
        for (const auto& b : bars_) {
            datetime().forward(b.dt);
            open().forward(b.o);
            high().forward(b.h);
            low().forward(b.l);
            close().forward(b.c);
            volume().forward(b.v);
            openinterest().forward(0.0);
        }
        datetime().home();
        open().home();
        high().home();
        low().home();
        close().home();
        volume().home();
        openinterest().home();
        return !bars_.empty();
    }

    [[nodiscard]] std::unique_ptr<::stratforge::DataFeed> clone() const override {
        return std::make_unique<InMemoryFeed>(bars_);
    }

private:
    std::vector<Bar> bars_;
    bool loaded_ = false;
};

}  // namespace stratforge::test
