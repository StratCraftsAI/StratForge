#pragma once

#include <cstdint>
#include <string_view>

namespace stratforge {

/// Error codes for data loading operations.
/// Lightweight enum — zero-alloc error reporting for non-hot paths.
enum class DataError : std::uint8_t {
    FileNotFound,   ///< File could not be opened
    EmptyFile,      ///< File contained no data rows (after filtering)
    HeaderOnly,     ///< File had only a header line, no data
    AlreadyLoaded   ///< load() called more than once
};

/// Zero-alloc human-readable message for DataError.
[[nodiscard]] constexpr std::string_view to_string(DataError e) noexcept {
    switch (e) {
    case DataError::FileNotFound:  return "file not found";
    case DataError::EmptyFile:     return "no data rows after filtering";
    case DataError::HeaderOnly:    return "header only, no data rows";
    case DataError::AlreadyLoaded: return "data already loaded";
    }
    return "unknown data error";
}

} // namespace stratforge
