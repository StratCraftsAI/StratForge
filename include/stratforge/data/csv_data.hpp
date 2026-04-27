#pragma once

#include <stratforge/data/data_feed.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <expected>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace stratforge {

/// Column mapping for CSV files
struct CsvColumnMap {
    int datetime = 0;
    int time = -1; // -1 means no separate time column
    int open = 1;
    int high = 2;
    int low = 3;
    int close = 4;
    int volume = 5;
    int openinterest = 6;
};

/// Diagnostic counters populated during CSV loading.
/// Zero overhead when not inspected — just plain counters.
///
/// Overflow safety: counters use std::size_t (64-bit unsigned). Silent wrap
/// at 2^64 is theoretically possible but impractical — a CSV file would need
/// ~18 exabytes of rows to overflow any single counter. These counters are
/// diagnostic-only and do not affect data loading correctness.
struct LoadDiagnostics {
    std::size_t rows_parsed = 0;
    std::size_t rows_skipped_truncated = 0;
    std::size_t rows_skipped_empty = 0;
    std::size_t rows_skipped_datefilter = 0;
    std::size_t fields_malformed_numeric = 0;
    std::size_t fields_malformed_datetime = 0;
    std::size_t duplicate_timestamps = 0;
    std::size_t out_of_order_timestamps = 0;
};

/// CsvData - CSV file data feed.
/// Reads OHLCV data from CSV files with configurable column mapping.
class CsvData : public DataFeed {
public:
    struct Params {
        std::string filename;
        CsvColumnMap columns;
        std::string date_format = "%Y-%m-%d";
        std::string time_format = "%H:%M:%S";
        char separator = ',';
        bool has_headers = true;
        std::optional<DateTime> fromdate;
        std::optional<DateTime> todate;
    };

    explicit CsvData(Params params)
        : params_(std::move(params)) {}

    /// Clone this data feed (creates a fresh CsvData from the same params)
    [[nodiscard]] std::unique_ptr<DataFeed> clone() const override {
        return std::make_unique<CsvData>(params_);
    }

    /// Access load diagnostics (meaningful after load() returns)
    [[nodiscard]] const LoadDiagnostics& diagnostics() const noexcept {
        return diag_;
    }

    /// Load all data from the CSV file with explicit error reporting.
    [[nodiscard]] std::expected<void, DataError> load_expected() {
        if (loaded_) return std::unexpected(DataError::AlreadyLoaded);
        diag_ = {}; // reset

        std::ifstream file(params_.filename);
        if (!file.is_open()) return std::unexpected(DataError::FileNotFound);

        loaded_ = true; // committed — file opened successfully

        std::string line_str;

        // Skip header if present
        if (params_.has_headers && !std::getline(file, line_str)) {
            return std::unexpected(DataError::HeaderOnly);
        }

        // Pre-count lines for reserve
        const auto start_pos = file.tellg();
        const auto line_count = static_cast<std::size_t>(
            std::count(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>(), '\n'));
        file.clear();
        file.seekg(start_pos);

        datetime_.data().reserve(line_count);
        open_.data().reserve(line_count);
        high_.data().reserve(line_count);
        low_.data().reserve(line_count);
        close_.data().reserve(line_count);
        volume_.data().reserve(line_count);
        openinterest_.data().reserve(line_count);

        while (std::getline(file, line_str)) {
            // Strip UTF-8 BOM if present on first data line
            if (diag_.rows_parsed == 0 && line_str.size() >= 3 &&
                line_str[0] == '\xEF' && line_str[1] == '\xBB' && line_str[2] == '\xBF') {
                line_str.erase(0, 3);
            }

            // Strip trailing \r (Windows line endings: \r\n where \n consumed by getline)
            if (!line_str.empty() && line_str.back() == '\r')
                line_str.pop_back();

            auto fields = split(line_str, params_.separator);
            if (fields.empty() || (fields.size() == 1 && fields[0].empty())) {
                ++diag_.rows_skipped_empty;
                continue;
            }

            ++diag_.rows_parsed;

            auto max_col = max_column();
            if (fields.size() <= static_cast<std::size_t>(max_col)) {
                ++diag_.rows_skipped_truncated;
                continue;
            }

            // Parse datetime
            bool dt_malformed = false;
            DateTime dt;
            if (params_.columns.time >= 0 &&
                static_cast<std::size_t>(params_.columns.time) < fields.size()) {
                dt = parse_datetime(fields[static_cast<std::size_t>(params_.columns.datetime)],
                                    dt_malformed,
                                    fields[static_cast<std::size_t>(params_.columns.time)]);
            } else {
                dt = parse_datetime(fields[static_cast<std::size_t>(params_.columns.datetime)],
                                    dt_malformed);
            }
            if (dt_malformed) ++diag_.fields_malformed_datetime;

            // Apply date filtering
            if (params_.fromdate && dt < *params_.fromdate) {
                ++diag_.rows_skipped_datefilter;
                continue;
            }
            if (params_.todate && dt > *params_.todate) {
                ++diag_.rows_skipped_datefilter;
                continue;
            }

            datetime_.forward(dt);
            open_.forward(parse_double(fields[static_cast<std::size_t>(params_.columns.open)]));
            high_.forward(parse_double(fields[static_cast<std::size_t>(params_.columns.high)]));
            low_.forward(parse_double(fields[static_cast<std::size_t>(params_.columns.low)]));
            close_.forward(parse_double(fields[static_cast<std::size_t>(params_.columns.close)]));

            if (params_.columns.volume >= 0 &&
                static_cast<std::size_t>(params_.columns.volume) < fields.size()) {
                volume_.forward(parse_double(fields[static_cast<std::size_t>(params_.columns.volume)]));
            } else {
                volume_.forward(0.0);
            }

            if (params_.columns.openinterest >= 0 &&
                static_cast<std::size_t>(params_.columns.openinterest) < fields.size()) {
                openinterest_.forward(parse_double(fields[static_cast<std::size_t>(params_.columns.openinterest)]));
            } else {
                openinterest_.forward(0.0);
            }
        }

        // Post-load: scan timestamps for duplicates and ordering issues
        scan_timestamps();

        // Reset to beginning after loading
        datetime_.home();
        open_.home();
        high_.home();
        low_.home();
        close_.home();
        volume_.home();
        openinterest_.home();

        if (size() == 0) {
            // HeaderOnly: header present and literally no data lines in the file.
            // EmptyFile: rows existed but all were filtered/truncated/skipped.
            if (params_.has_headers && diag_.rows_parsed == 0)
                return std::unexpected(DataError::HeaderOnly);
            return std::unexpected(DataError::EmptyFile);
        }
        return {};
    }

    /// Load all data from the CSV file (bool wrapper over load_expected).
    [[nodiscard]] bool load() override {
        return load_expected().has_value();
    }

    /// Load with explicit error reporting (DataFeed virtual override).
    [[nodiscard]] std::expected<void, DataError> load_with_error() override {
        return load_expected();
    }

private:
    /// Maximum required column index (excludes optional volume/openinterest
    /// which are bounds-checked individually during parsing)
    [[nodiscard]] int max_column() const noexcept {
        int m = params_.columns.datetime;
        m = std::max(m, params_.columns.open);
        m = std::max(m, params_.columns.high);
        m = std::max(m, params_.columns.low);
        m = std::max(m, params_.columns.close);
        return m;
    }

    /// Split string by delimiter, preserving trailing empty fields.
    /// std::getline-based splitting drops a trailing empty field when the
    /// string ends with the delimiter; manual scan does not.
    [[nodiscard]] static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> result;
        std::string::size_type start = 0;
        std::string::size_type pos = 0;
        while ((pos = s.find(delim, start)) != std::string::npos) {
            result.push_back(s.substr(start, pos - start));
            start = pos + 1;
        }
        result.push_back(s.substr(start));
        return result;
    }

    [[nodiscard]] double parse_double(const std::string& s) noexcept {
        const char* begin = s.data();
        const char* end = s.data() + s.size();

        // Skip leading whitespace (from_chars does not tolerate it)
        while (begin < end && (*begin == ' ' || *begin == '\t')) {
            ++begin;
        }

        // Trim trailing whitespace / \r (Windows line endings)
        while (end > begin && (*(end - 1) == '\r' || *(end - 1) == ' ' || *(end - 1) == '\t')) {
            --end;
        }

        if (begin == end) {
            ++diag_.fields_malformed_numeric;
            return 0.0;
        }

        // Skip leading '+' — from_chars rejects it, but Python float() accepts it
        // (backtrader compat: "+1.5", "+inf" are valid numeric inputs)
        if (*begin == '+') ++begin;
        if (begin == end) {
            ++diag_.fields_malformed_numeric;
            return 0.0;
        }

        double val = 0.0;
        auto [ptr, ec] = std::from_chars(begin, end, val);

        if (ec == std::errc::invalid_argument || ptr == begin) {
            ++diag_.fields_malformed_numeric;
            return 0.0;
        }
        if (ec == std::errc::result_out_of_range) {
            ++diag_.fields_malformed_numeric;
            return 0.0;
        }

        // Partial parse: trailing non-whitespace junk (e.g. "123abc", "1.2foo")
        // Still return the parsed value (backtrader compat), but count as malformed
        if (ptr != end) {
            bool only_whitespace = true;
            for (const char* p = ptr; p != end; ++p) {
                if (*p != ' ' && *p != '\t') {
                    only_whitespace = false;
                    break;
                }
            }
            if (!only_whitespace) ++diag_.fields_malformed_numeric;
        }
        return val;
    }

    [[nodiscard]] DateTime parse_datetime(const std::string& date_str,
                                          bool& malformed,
                                          const std::string& time_str = "") const {
        std::tm tm = {};
        std::istringstream ss(date_str);
        ss >> std::get_time(&tm, params_.date_format.c_str());
        if (ss.fail()) {
            malformed = true;
            return DateTime{};
        }

        if (!time_str.empty()) {
            std::istringstream ss_time(time_str);
            ss_time >> std::get_time(&tm, params_.time_format.c_str());
            if (ss_time.fail()) {
                malformed = true;
            }
        }

        auto time = std::mktime(&tm);
        return std::chrono::system_clock::from_time_t(time);
    }

    /// Scan loaded timestamps for duplicates and non-monotonic ordering
    void scan_timestamps() noexcept {
        const auto& ts = datetime_.data();
        if (ts.size() < 2) return;

        for (std::size_t i = 1; i < ts.size(); ++i) {
            if (ts[i] == ts[i - 1]) {
                ++diag_.duplicate_timestamps;
            } else if (ts[i] < ts[i - 1]) {
                ++diag_.out_of_order_timestamps;
            }
        }
    }

    Params params_;
    LoadDiagnostics diag_;
    bool loaded_ = false;
};

} // namespace stratforge
