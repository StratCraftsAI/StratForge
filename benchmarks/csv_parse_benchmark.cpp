#include "benchmark_utils.hpp"

#include <stratforge/data/csv_data.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

namespace {

using namespace stratforge::bench;

// Generate a synthetic CSV file for benchmarking parse throughput.
// Returns the file path and the number of rows generated.
std::pair<std::string, std::size_t> generate_csv(std::size_t num_rows) {
    const std::string path = source_path("build/bench_data/csv_parse_bench_" +
                                         std::to_string(num_rows) + ".csv");
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::ofstream out(path);
    out << "Date,Open,High,Low,Close,Volume,OpenInterest\n";

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(90.0, 110.0);
    std::uniform_int_distribution<int> vol_dist(100000, 5000000);

    // Start date: 2000-01-03
    int year = 2000, month = 1, day = 3;
    for (std::size_t i = 0; i < num_rows; ++i) {
        double open = price_dist(rng);
        double high = open + std::abs(price_dist(rng) - 100.0) * 0.5;
        double low = open - std::abs(price_dist(rng) - 100.0) * 0.5;
        double close = price_dist(rng);
        int volume = vol_dist(rng);

        char date_buf[16];
        std::snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", year, month, day);

        out << date_buf << ','
            << open << ',' << high << ',' << low << ',' << close << ','
            << volume << ",0\n";

        // Advance date (simplistic: skip weekends)
        ++day;
        if (day > 28) { day = 1; ++month; }
        if (month > 12) { month = 1; ++year; }
    }

    out.close();
    return {path, num_rows};
}

// Benchmark: Load CSV from disk and parse all rows.
void benchmark_csv_load(const std::string& filepath, std::size_t expected_rows,
                        std::size_t iterations, JsonReport& report) {
    const auto file_size = std::filesystem::file_size(filepath);
    const double file_mb = static_cast<double>(file_size) / (1024.0 * 1024.0);

    auto bench_fn = [&]() {
        stratforge::CsvData feed(stratforge::CsvData::Params{
            .filename = filepath,
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });
        volatile bool ok = feed.load();
        (void)ok;
    };

    const auto summary = run_benchmark(iterations, bench_fn);

    const double mb_per_sec = (file_mb / (summary.avg_ns / 1'000'000'000.0));
    const double rows_per_sec = static_cast<double>(expected_rows) /
                                (summary.avg_ns / 1'000'000'000.0);
    const double ns_per_row = summary.avg_ns / static_cast<double>(expected_rows);

    std::cout << "CSV Load [" << expected_rows << " rows, "
              << std::fixed << std::setprecision(2) << file_mb << " MB]:\n"
              << "  avg=" << summary.avg_ns / 1'000'000.0 << " ms"
              << "  p50=" << summary.p50_ns / 1'000'000.0 << " ms"
              << "  p99=" << summary.p99_ns / 1'000'000.0 << " ms\n"
              << "  throughput=" << mb_per_sec << " MB/s"
              << "  rows/sec=" << rows_per_sec
              << "  ns/row=" << ns_per_row << "\n";

    report.add({.name = "csv_load_" + std::to_string(expected_rows) + "_rows",
                .summary = summary,
                .bars_per_iteration = expected_rows});
}

// Benchmark: Raw line tokenization throughput (no date parsing, no data feed).
void benchmark_csv_tokenize(const std::string& filepath, std::size_t expected_rows,
                            std::size_t iterations, JsonReport& report) {
    // Read file into memory first (we want to measure tokenization, not I/O)
    std::ifstream in(filepath);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();

    const double file_mb = static_cast<double>(content.size()) / (1024.0 * 1024.0);

    auto bench_fn = [&]() {
        std::size_t row_count = 0;
        const char* ptr = content.data();
        const char* end = ptr + content.size();

        // Skip header
        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr < end) ++ptr;

        while (ptr < end) {
            // Count fields by finding commas (7 fields per row)
            int fields = 0;
            while (ptr < end && *ptr != '\n') {
                if (*ptr == ',') ++fields;
                ++ptr;
            }
            if (ptr < end) ++ptr;
            ++row_count;
        }
        volatile auto r = row_count;
        (void)r;
    };

    const auto summary = run_benchmark(iterations, bench_fn);

    const double mb_per_sec = file_mb / (summary.avg_ns / 1'000'000'000.0);
    std::cout << "CSV Tokenize [" << expected_rows << " rows, "
              << std::fixed << std::setprecision(2) << file_mb << " MB, in-memory]:\n"
              << "  avg=" << summary.avg_ns / 1'000'000.0 << " ms"
              << "  p50=" << summary.p50_ns / 1'000'000.0 << " ms"
              << "  p99=" << summary.p99_ns / 1'000'000.0 << " ms\n"
              << "  throughput=" << mb_per_sec << " MB/s\n";

    report.add({.name = "csv_tokenize_" + std::to_string(expected_rows) + "_rows",
                .summary = summary,
                .bars_per_iteration = expected_rows});
}

// Benchmark: Per-row latency (how long to parse one row of CsvData)
void benchmark_csv_per_row(const std::string& filepath, std::size_t expected_rows,
                           JsonReport& report) {
    const double ns_budget = 1'000'000'000.0; // measure over ~1s
    const std::size_t load_iterations = std::max(
        std::size_t{5}, static_cast<std::size_t>(ns_budget / 50'000'000.0));

    std::vector<double> per_row_samples;
    per_row_samples.reserve(load_iterations);

    for (std::size_t i = 0; i < load_iterations; ++i) {
        stratforge::CsvData feed(stratforge::CsvData::Params{
            .filename = filepath,
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });

        const auto start = std::chrono::steady_clock::now();
        (void)feed.load();
        const auto end = std::chrono::steady_clock::now();
        const double total_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        per_row_samples.push_back(total_ns / static_cast<double>(expected_rows));
    }

    auto summary = summarize_samples(std::move(per_row_samples));
    std::cout << "CSV Per-Row Latency [" << expected_rows << " rows]:\n"
              << "  avg=" << summary.avg_ns << " ns/row"
              << "  p50=" << summary.p50_ns << " ns/row"
              << "  p99=" << summary.p99_ns << " ns/row\n";

    report.add({.name = "csv_per_row_" + std::to_string(expected_rows),
                .summary = summary,
                .bars_per_iteration = 1});
}

} // namespace

int main() {
    std::cout << "=== stratforge CSV Parse Benchmarks ===\n";
    setup_benchmark_env();
    std::cout << '\n';

    JsonReport report;

    // Generate test datasets
    auto [path_1k, rows_1k] = generate_csv(1000);
    auto [path_10k, rows_10k] = generate_csv(10000);
    auto [path_100k, rows_100k] = generate_csv(100000);

    std::cout << "--- CSV Full Load (disk I/O + parse) ---\n";
    benchmark_csv_load(path_1k, rows_1k, 100, report);
    std::cout << '\n';
    benchmark_csv_load(path_10k, rows_10k, 50, report);
    std::cout << '\n';
    benchmark_csv_load(path_100k, rows_100k, 10, report);
    std::cout << '\n';

    std::cout << "--- CSV Tokenization (in-memory, no date parse) ---\n";
    benchmark_csv_tokenize(path_100k, rows_100k, 50, report);
    std::cout << '\n';

    std::cout << "--- CSV Per-Row Latency ---\n";
    benchmark_csv_per_row(path_10k, rows_10k, report);
    std::cout << '\n';

    // Also benchmark the real dataset if available
    const auto real_path = source_path("tools/golden_extract/datas/2005-2006-day-001.txt");
    if (std::filesystem::exists(real_path)) {
        stratforge::CsvData probe(stratforge::CsvData::Params{
            .filename = real_path,
            .columns = {},
            .date_format = "%Y-%m-%d",
            .separator = ',',
            .has_headers = true,
            .fromdate = std::nullopt,
            .todate = std::nullopt,
        });
        (void)probe.load();
        std::cout << "--- Real Dataset [" << probe.size() << " bars] ---\n";
        benchmark_csv_load(real_path, probe.size(), 200, report);
        std::cout << '\n';
    }

    report.write(source_path("build/bench_results/csv_parse_benchmark.json"),
                 "csv_parse_benchmark");

    std::cout << "=== CSV Parse Benchmarks Complete ===\n";
    return 0;
}
