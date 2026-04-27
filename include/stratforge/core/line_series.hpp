#pragma once

#include <stratforge/core/line.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace stratforge {

/// LineSeries - Named collection of Lines.
/// Used by DataFeed (OHLCV lines), Indicators (output lines), etc.
template <typename T = double>
class LineSeries {
public:
    LineSeries() = default;

    /// Add a named line
    void add_line(const std::string& name) {
        lines_.emplace(name, Line<T>{});
    }

    /// Get a line by name (const)
    [[nodiscard]] const Line<T>& line(const std::string& name) const {
        return lines_.at(name);
    }

    /// Get a line by name (mutable)
    [[nodiscard]] Line<T>& line(const std::string& name) {
        return lines_.at(name);
    }

    /// Check if a line exists
    [[nodiscard]] bool has_line(const std::string& name) const noexcept {
        return lines_.contains(name);
    }

    /// Number of lines
    [[nodiscard]] std::size_t num_lines() const noexcept {
        return lines_.size();
    }

    /// Advance all lines
    void advance() noexcept {
        for (auto& [name, line] : lines_) {
            line.advance();
        }
    }

    /// Reset all lines to home position
    void home() noexcept {
        for (auto& [name, line] : lines_) {
            line.home();
        }
    }

    /// Access all line names
    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> result;
        result.reserve(lines_.size());
        for (const auto& [name, _] : lines_) {
            result.push_back(name);
        }
        return result;
    }

private:
    std::unordered_map<std::string, Line<T>> lines_;
};

} // namespace stratforge
