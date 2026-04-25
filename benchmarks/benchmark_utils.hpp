#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

// Allocation counting via global operator new/delete override
// Thread-local counter — scoped regions track heap allocations

namespace stratforge::bench {

namespace detail {

inline thread_local std::int64_t g_alloc_count = 0;
inline thread_local bool g_alloc_tracking = false;

} // namespace detail

} // namespace stratforge::bench

// Global operator new/delete overrides for allocation counting
// These must be at global scope (not in a namespace)
inline void* operator new(std::size_t size) {
    if (stratforge::bench::detail::g_alloc_tracking) {
        ++stratforge::bench::detail::g_alloc_count;
    }
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

inline void* operator new[](std::size_t size) {
    if (stratforge::bench::detail::g_alloc_tracking) {
        ++stratforge::bench::detail::g_alloc_count;
    }
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

inline void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

inline void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

inline void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

inline void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

namespace stratforge::bench {

// ─── Allocation Counter ─────────────────────────────────────────────

/// RAII scope that counts heap allocations within its lifetime.
/// Usage: { AllocationCounter ac("label"); ... } ac.count() returns the count.
struct AllocationCounter {
    const char* label;
    std::int64_t start_count;

    explicit AllocationCounter(const char* lbl) : label(lbl) {
        detail::g_alloc_count = 0;
        detail::g_alloc_tracking = true;
        start_count = detail::g_alloc_count;
    }

    ~AllocationCounter() {
        detail::g_alloc_tracking = false;
    }

    [[nodiscard]] std::int64_t count() const noexcept {
        return detail::g_alloc_count - start_count;
    }
};

#define NBT_BENCH_ZERO_ALLOC_REGION(label) \
    stratforge::bench::AllocationCounter NBT_CONCAT(_alloc_counter_, __LINE__)(label)

#define NBT_CONCAT(a, b) NBT_CONCAT_IMPL(a, b)
#define NBT_CONCAT_IMPL(a, b) a ## b

// ─── Sample Summary ─────────────────────────────────────────────────

struct SampleSummary {
    double min_ns = 0.0;
    double max_ns = 0.0;
    double avg_ns = 0.0;
    double p50_ns = 0.0;
    double p99_ns = 0.0;
    double p999_ns = 0.0;
    std::size_t sample_count = 0;
};

inline SampleSummary summarize_samples(std::vector<double> samples_ns) {
    SampleSummary summary;
    if (samples_ns.empty()) return summary;

    std::sort(samples_ns.begin(), samples_ns.end());
    summary.sample_count = samples_ns.size();

    const auto percentile_index = [&](double p) {
        return static_cast<std::size_t>(p * static_cast<double>(samples_ns.size() - 1));
    };

    summary.min_ns = samples_ns.front();
    summary.max_ns = samples_ns.back();
    summary.avg_ns = std::accumulate(samples_ns.begin(), samples_ns.end(), 0.0)
                     / static_cast<double>(samples_ns.size());
    summary.p50_ns = samples_ns[percentile_index(0.50)];
    summary.p99_ns = samples_ns[percentile_index(0.99)];
    summary.p999_ns = samples_ns[percentile_index(0.999)];
    return summary;
}

// ─── Benchmark Runners ──────────────────────────────────────────────

/// Run benchmark with warmup phase. Discards first `warmup` iterations.
template <typename Fn>
inline SampleSummary run_benchmark(std::size_t iterations, Fn&& fn,
                                   std::size_t warmup = 3) {
    // Warmup phase — run but discard
    for (std::size_t i = 0; i < warmup; ++i) {
        fn();
    }

    std::vector<double> samples_ns;
    samples_ns.reserve(iterations);

    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        fn();
        const auto end = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        samples_ns.push_back(static_cast<double>(elapsed));
    }

    return summarize_samples(std::move(samples_ns));
}

/// Per-bar benchmark: times individual calls (e.g., indicator next()) and
/// collects per-call nanosecond samples.
/// `setup` is called once before the timed loop, `per_bar` is called per bar.
template <typename SetupFn, typename PerBarFn>
inline SampleSummary run_perbar_benchmark(std::size_t num_bars,
                                          SetupFn&& setup,
                                          PerBarFn&& per_bar,
                                          std::size_t warmup_bars = 50) {
    setup();

    std::vector<double> samples_ns;
    samples_ns.reserve(num_bars);

    for (std::size_t bar = 0; bar < num_bars; ++bar) {
        const auto start = std::chrono::steady_clock::now();
        per_bar(bar);
        const auto end = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

        // Only record after warmup bars
        if (bar >= warmup_bars) {
            samples_ns.push_back(static_cast<double>(elapsed));
        }
    }

    return summarize_samples(std::move(samples_ns));
}

// ─── Output: Human-Readable ─────────────────────────────────────────

inline void print_summary(const std::string& label,
                          const SampleSummary& summary,
                          std::size_t iterations,
                          std::size_t bars_per_iteration) {
    const double total_bars = static_cast<double>(iterations * bars_per_iteration);
    const double avg_bars_per_sec =
        summary.avg_ns > 0.0
            ? (1'000'000'000.0 / summary.avg_ns) * static_cast<double>(bars_per_iteration)
            : 0.0;

    std::cout << label
              << ": iterations=" << iterations
              << " bars/iter=" << bars_per_iteration
              << " total_bars=" << static_cast<std::uint64_t>(total_bars)
              << " avg_ns=" << summary.avg_ns
              << " p50_ns=" << summary.p50_ns
              << " p99_ns=" << summary.p99_ns
              << " p999_ns=" << summary.p999_ns
              << " min_ns=" << summary.min_ns
              << " max_ns=" << summary.max_ns
              << " bars_per_sec=" << avg_bars_per_sec
              << '\n';
}

inline void print_perbar_summary(const std::string& label,
                                 const SampleSummary& summary) {
    const double bars_per_sec =
        summary.avg_ns > 0.0 ? 1'000'000'000.0 / summary.avg_ns : 0.0;

    std::cout << label
              << ": samples=" << summary.sample_count
              << " avg_ns/bar=" << summary.avg_ns
              << " p50_ns=" << summary.p50_ns
              << " p99_ns=" << summary.p99_ns
              << " p999_ns=" << summary.p999_ns
              << " min_ns=" << summary.min_ns
              << " max_ns=" << summary.max_ns
              << " bars/sec=" << bars_per_sec
              << '\n';
}

// ─── Output: JSON ───────────────────────────────────────────────────

/// Write a single benchmark result as JSON to a stream.
inline void write_json_entry(std::ostream& out,
                             const std::string& name,
                             const SampleSummary& summary,
                             std::size_t bars_per_iteration = 0,
                             std::int64_t alloc_count = -1) {
    const double bars_per_sec =
        (summary.avg_ns > 0.0 && bars_per_iteration > 0)
            ? (1'000'000'000.0 / summary.avg_ns) * static_cast<double>(bars_per_iteration)
            : 0.0;

    out << "    {\n"
        << "      \"name\": \"" << name << "\",\n"
        << "      \"avg_ns\": " << summary.avg_ns << ",\n"
        << "      \"p50_ns\": " << summary.p50_ns << ",\n"
        << "      \"p99_ns\": " << summary.p99_ns << ",\n"
        << "      \"p999_ns\": " << summary.p999_ns << ",\n"
        << "      \"min_ns\": " << summary.min_ns << ",\n"
        << "      \"max_ns\": " << summary.max_ns << ",\n"
        << "      \"samples\": " << summary.sample_count << ",\n"
        << "      \"bars_per_iteration\": " << bars_per_iteration << ",\n"
        << "      \"bars_per_sec\": " << bars_per_sec;
    if (alloc_count >= 0) {
        out << ",\n      \"alloc_count\": " << alloc_count;
    }
    out << "\n    }";
}

/// JSON report writer: collects entries, writes final JSON file.
class JsonReport {
public:
    struct Entry {
        std::string name;
        SampleSummary summary;
        std::size_t bars_per_iteration = 0;
        std::int64_t alloc_count = -1;
    };

    void add(Entry entry) {
        entries_.push_back(std::move(entry));
    }

    void write(const std::string& filepath, const std::string& suite_name) const {
        std::filesystem::create_directories(
            std::filesystem::path(filepath).parent_path());

        std::ofstream out(filepath);
        if (!out.is_open()) {
            std::cerr << "Warning: cannot write JSON report to " << filepath << '\n';
            return;
        }

        out << "{\n"
            << "  \"suite\": \"" << suite_name << "\",\n"
            << "  \"timestamp\": \"" << timestamp() << "\",\n"
            << "  \"results\": [\n";

        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            write_json_entry(out, e.name, e.summary, e.bars_per_iteration, e.alloc_count);
            if (i + 1 < entries_.size()) out << ',';
            out << '\n';
        }

        out << "  ]\n}\n";
        out.close();

        std::cout << "[JSON] Report written to " << filepath << '\n';
    }

private:
    [[nodiscard]] static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = {};
        localtime_r(&time, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return buf;
    }

    std::vector<Entry> entries_;
};

// ─── Cold/Warm Comparison ───────────────────────────────────────────

/// Run once cold, then N warm iterations. Reports both and ratio.
template <typename Fn>
inline void run_cold_warm_comparison(const std::string& label,
                                     std::size_t warm_iterations,
                                     Fn&& fn) {
    // Cold run (first execution, no prior caching)
    const auto cold_start = std::chrono::steady_clock::now();
    fn();
    const auto cold_end = std::chrono::steady_clock::now();
    const double cold_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(cold_end - cold_start).count());

    // Warm runs
    auto warm_summary = run_benchmark(warm_iterations, std::forward<Fn>(fn), 0);

    const double ratio = cold_ns / warm_summary.avg_ns;

    std::cout << label << " cold/warm:"
              << " cold_ns=" << cold_ns
              << " warm_avg_ns=" << warm_summary.avg_ns
              << " ratio=" << ratio
              << '\n';
}

// ─── Dataset Path Helper ────────────────────────────────────────────

inline std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

} // namespace stratforge::bench
