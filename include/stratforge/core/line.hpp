#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace stratforge {

/// Line<T> - Ring buffer with backtrader-style indexing.
/// operator[](0) = current value, operator[](-1) = previous value.
///
/// API surface lock (ratified 2026-05-18):
///   Indexing is provided EXCLUSIVELY through operator[](int). The template
///   form `at<N>()` (with static_assert(N <= 0) for compile-time forward-index
///   rejection) is intentionally NOT exposed and will not be added. Reasons:
///     (1) operator[] already covers the full [-N, +N] range with runtime
///         throw; `at<N>()` would only fire on literal-index callsites, so
///         its compile-time check is partial -- any variable-driven access
///         falls back to operator[] anyway.
///     (2) Dual-form (operator[] + `at<N>()`) doubles the API surface
///         that downstream prompt SSOTs and validators must handle for the
///         same observable behavior.
///     (3) Python backtrader exposes only `self.data.close[0]` style.
///         The backtrader-first principle requires not diverging from this.
///     (4) Vendored consumers pin to a specific SHA of this header. Adding
///         `at<N>()` would require a coordinated SHA bump + downstream
///         validator rewrite for no semantic gain.
///   This rejection is a permanent design decision; reintroducing `at<N>()`
///   requires deleting this paragraph AND a fresh SDK-owner ratify.
template <typename T>
class Line {
public:
    Line() = default;

    explicit Line(std::size_t reserve_size) {
        data_.reserve(reserve_size);
    }

    /// Add a new value and advance the cursor
    void forward(T value) noexcept {
        data_.push_back(std::move(value));
        idx_ = data_.size() - 1;
    }

    /// Set the current bar's value (overwrites the value at cursor)
    void set(T value) noexcept {
        if (idx_ < data_.size()) {
            data_[idx_] = std::move(value);
        } else {
            data_.push_back(std::move(value));
        }
    }

    /// Advance cursor without adding data (for pre-loaded lines)
    void advance() noexcept {
        if (idx_ + 1 < data_.size()) {
            ++idx_;
        }
    }

    /// Reset cursor to the beginning
    void home() noexcept {
        idx_ = 0;
    }

    /// Pre-extend the line with default values
    void extend(std::size_t n, T default_value = T{}) {
        data_.resize(data_.size() + n, default_value);
    }

    /// Backtrader-style indexing: [0] = current, [-1] = previous, [1] = next
    [[nodiscard]] const T& operator[](int offset) const {
        auto pos = static_cast<long long>(idx_) + offset;
        if (pos < 0 || static_cast<std::size_t>(pos) >= data_.size()) [[unlikely]] {
            throw std::out_of_range("Line index out of range");
        }
        return data_[static_cast<std::size_t>(pos)];
    }

    [[nodiscard]] T& operator[](int offset) {
        auto pos = static_cast<long long>(idx_) + offset;
        if (pos < 0 || static_cast<std::size_t>(pos) >= data_.size()) [[unlikely]] {
            throw std::out_of_range("Line index out of range");
        }
        return data_[static_cast<std::size_t>(pos)];
    }

    /// Number of data points loaded
    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    /// Current cursor position (bar index)
    [[nodiscard]] std::size_t index() const noexcept {
        return idx_;
    }

    /// Number of valid bars from current position to end
    [[nodiscard]] std::size_t buflen() const noexcept {
        if (data_.empty()) return 0;
        return data_.size() - idx_;
    }

    /// Whether the line has any data
    [[nodiscard]] bool empty() const noexcept {
        return data_.empty();
    }

    /// Access underlying data for bulk operations
    [[nodiscard]] const std::vector<T>& data() const noexcept {
        return data_;
    }

    /// Access underlying data (mutable)
    [[nodiscard]] std::vector<T>& data() noexcept {
        return data_;
    }

private:
    std::vector<T> data_;
    std::size_t idx_ = 0uz;
};

} // namespace stratforge
