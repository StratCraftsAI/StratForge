#include "benchmark_utils.hpp"

#include <stratforge/core/line.hpp>
#include <stratforge/core/line_series.hpp>

#include <cmath>
#include <iostream>
#include <random>
#include <string>

namespace {

using namespace stratforge::bench;

// --- Line<double> append benchmark ---

void benchmark_line_append(std::size_t num_elements, std::size_t iterations,
                           JsonReport& report) {
    auto bench_fn = [num_elements]() {
        stratforge::Line<double> line;
        for (std::size_t i = 0; i < num_elements; ++i) {
            line.forward(static_cast<double>(i) * 1.001);
        }
        volatile double v = line[0];
        (void)v;
    };

    const auto summary = run_benchmark(iterations, bench_fn);
    const double ns_per_op = summary.avg_ns / static_cast<double>(num_elements);

    std::cout << "Line::forward [" << num_elements << " elements]:\n"
              << "  total: avg=" << summary.avg_ns << " ns"
              << "  p50=" << summary.p50_ns << " ns"
              << "  p99=" << summary.p99_ns << " ns\n"
              << "  per-op: " << ns_per_op << " ns/append\n";

    report.add({.name = "line_append_" + std::to_string(num_elements),
                .summary = summary,
                .bars_per_iteration = num_elements});
}

// --- Line<double> append with reserve ---

void benchmark_line_append_reserved(std::size_t num_elements, std::size_t iterations,
                                    JsonReport& report) {
    auto bench_fn = [num_elements]() {
        stratforge::Line<double> line(num_elements);
        for (std::size_t i = 0; i < num_elements; ++i) {
            line.forward(static_cast<double>(i) * 1.001);
        }
        volatile double v = line[0];
        (void)v;
    };

    const auto summary = run_benchmark(iterations, bench_fn);
    const double ns_per_op = summary.avg_ns / static_cast<double>(num_elements);

    std::cout << "Line::forward (reserved) [" << num_elements << " elements]:\n"
              << "  total: avg=" << summary.avg_ns << " ns"
              << "  p50=" << summary.p50_ns << " ns"
              << "  p99=" << summary.p99_ns << " ns\n"
              << "  per-op: " << ns_per_op << " ns/append\n";

    report.add({.name = "line_append_reserved_" + std::to_string(num_elements),
                .summary = summary,
                .bars_per_iteration = num_elements});
}

// --- Line<double> random access benchmark ---

void benchmark_line_access(std::size_t num_elements, std::size_t iterations,
                           JsonReport& report) {
    // Pre-populate line
    stratforge::Line<double> line(num_elements);
    for (std::size_t i = 0; i < num_elements; ++i) {
        line.forward(static_cast<double>(i) * 1.001);
    }

    // Generate random offset pattern (negative offsets for historical access)
    std::mt19937 rng(42);
    const int max_lookback = static_cast<int>(std::min(num_elements, std::size_t{100}));
    std::uniform_int_distribution<int> offset_dist(-max_lookback + 1, 0);
    std::vector<int> offsets(1000);
    for (auto& o : offsets) o = offset_dist(rng);

    auto bench_fn = [&]() {
        double sum = 0.0;
        for (auto offset : offsets) {
            sum += line[offset];
        }
        volatile double v = sum;
        (void)v;
    };

    const auto summary = run_benchmark(iterations, bench_fn);
    const double ns_per_access = summary.avg_ns / static_cast<double>(offsets.size());

    std::cout << "Line::operator[] (random lookback) [" << num_elements << " elements]:\n"
              << "  1000 accesses: avg=" << summary.avg_ns << " ns"
              << "  p50=" << summary.p50_ns << " ns\n"
              << "  per-access: " << ns_per_access << " ns\n";

    report.add({.name = "line_random_access_" + std::to_string(num_elements),
                .summary = summary,
                .bars_per_iteration = offsets.size()});
}

// --- Line<double> sequential access (operator[](0) repeated) ---

void benchmark_line_current_access(std::size_t num_elements, std::size_t iterations,
                                   JsonReport& report) {
    stratforge::Line<double> line(num_elements);
    for (std::size_t i = 0; i < num_elements; ++i) {
        line.forward(static_cast<double>(i) * 1.001);
    }

    auto bench_fn = [&]() {
        line.home();
        double sum = 0.0;
        for (std::size_t i = 0; i < num_elements; ++i) {
            sum += line[0];
            if (i + 1 < num_elements) line.advance();
        }
        volatile double v = sum;
        (void)v;
    };

    const auto summary = run_benchmark(iterations, bench_fn);
    const double ns_per_access = summary.avg_ns / static_cast<double>(num_elements);

    std::cout << "Line::operator[](0) sequential [" << num_elements << " elements]:\n"
              << "  total: avg=" << summary.avg_ns << " ns\n"
              << "  per-access: " << ns_per_access << " ns\n";

    report.add({.name = "line_sequential_access_" + std::to_string(num_elements),
                .summary = summary,
                .bars_per_iteration = num_elements});
}

// --- LineSeries access benchmark ---

void benchmark_lineseries(std::size_t num_elements, std::size_t iterations,
                          JsonReport& report) {
    // Setup: OHLCV series
    stratforge::LineSeries<double> series;
    series.add_line("open");
    series.add_line("high");
    series.add_line("low");
    series.add_line("close");
    series.add_line("volume");

    for (std::size_t i = 0; i < num_elements; ++i) {
        double v = static_cast<double>(i) * 1.001;
        series.line("open").forward(v);
        series.line("high").forward(v + 1.0);
        series.line("low").forward(v - 1.0);
        series.line("close").forward(v + 0.5);
        series.line("volume").forward(v * 1000.0);
    }

    auto bench_fn = [&]() {
        series.home();
        double sum = 0.0;
        for (std::size_t i = 0; i < num_elements; ++i) {
            sum += series.line("close")[0];
            sum += series.line("high")[0];
            sum += series.line("low")[0];
            if (i + 1 < num_elements) series.advance();
        }
        volatile double v = sum;
        (void)v;
    };

    const auto summary = run_benchmark(iterations, bench_fn);
    const double ns_per_bar = summary.avg_ns / static_cast<double>(num_elements);

    std::cout << "LineSeries (5 lines, 3 accesses/bar) [" << num_elements << " bars]:\n"
              << "  total: avg=" << summary.avg_ns << " ns"
              << "  p50=" << summary.p50_ns << " ns\n"
              << "  per-bar: " << ns_per_bar << " ns\n";

    report.add({.name = "lineseries_access_" + std::to_string(num_elements),
                .summary = summary,
                .bars_per_iteration = num_elements});
}

// --- Allocation audit ---

void benchmark_line_allocs(std::size_t num_elements, JsonReport& report) {
    {
        AllocationCounter ac("line_no_reserve");
        stratforge::Line<double> line;
        for (std::size_t i = 0; i < num_elements; ++i) {
            line.forward(static_cast<double>(i));
        }
        const auto allocs = ac.count();
        std::cout << "Line allocs (no reserve, " << num_elements << " elements): "
                  << allocs << "\n";
        report.add({.name = "line_allocs_no_reserve_" + std::to_string(num_elements),
                    .summary = {},
                    .bars_per_iteration = num_elements,
                    .alloc_count = allocs});
    }
    {
        AllocationCounter ac("line_reserved");
        stratforge::Line<double> line(num_elements);
        for (std::size_t i = 0; i < num_elements; ++i) {
            line.forward(static_cast<double>(i));
        }
        const auto allocs = ac.count();
        std::cout << "Line allocs (reserved, " << num_elements << " elements): "
                  << allocs << "\n";
        report.add({.name = "line_allocs_reserved_" + std::to_string(num_elements),
                    .summary = {},
                    .bars_per_iteration = num_elements,
                    .alloc_count = allocs});
    }
}

} // namespace

int main() {
    std::cout << "=== stratforge Line/LineSeries Benchmarks ===\n";
    setup_benchmark_env();
    std::cout << '\n';

    JsonReport report;

    // --- Append benchmarks at various sizes ---
    std::cout << "--- Line::forward (append) ---\n";
    for (std::size_t n : {512, 10000, 100000}) {
        benchmark_line_append(n, 100, report);
        std::cout << '\n';
    }

    std::cout << "--- Line::forward (pre-reserved) ---\n";
    for (std::size_t n : {512, 10000, 100000}) {
        benchmark_line_append_reserved(n, 100, report);
        std::cout << '\n';
    }

    // --- Access benchmarks ---
    std::cout << "--- Line::operator[] (random lookback) ---\n";
    for (std::size_t n : {512, 10000, 100000}) {
        benchmark_line_access(n, 1000, report);
        std::cout << '\n';
    }

    std::cout << "--- Line::operator[](0) (sequential) ---\n";
    for (std::size_t n : {512, 10000, 100000}) {
        benchmark_line_current_access(n, 100, report);
        std::cout << '\n';
    }

    // --- LineSeries benchmark ---
    std::cout << "--- LineSeries (OHLCV, 5 lines) ---\n";
    for (std::size_t n : {512, 10000, 100000}) {
        benchmark_lineseries(n, 100, report);
        std::cout << '\n';
    }

    // --- Allocation audit ---
    std::cout << "--- Allocation Audit ---\n";
    benchmark_line_allocs(10000, report);
    benchmark_line_allocs(100000, report);
    std::cout << '\n';

    report.write(source_path("build/bench_results/line_series_benchmark.json"),
                 "line_series_benchmark");

    std::cout << "=== Line/LineSeries Benchmarks Complete ===\n";
    return 0;
}
