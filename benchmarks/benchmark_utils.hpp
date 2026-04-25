#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#endif

// Allocation counting via global operator new/delete override
// Thread-local counter — scoped regions track heap allocations

namespace stratforge::bench {

// ============================================================================
// RDTSC Timing (adapted from NexusFix — TICKET_015)
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define NBT_HAS_RDTSC 1
#else
#define NBT_HAS_RDTSC 0
#endif

#if NBT_HAS_RDTSC

/// Read Time Stamp Counter (basic, lfence-serialized)
[[nodiscard]] inline std::uint64_t rdtsc() noexcept {
    std::uint64_t lo, hi;
    asm volatile ("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return (hi << 32) | lo;
}

/// RDTSC with full pipeline serialization (cpuid)
/// More accurate but higher overhead, may cause VM Exit on cloud
[[nodiscard]] inline std::uint64_t rdtsc_precise() noexcept {
    std::uint32_t lo, hi;
    asm volatile (
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        :
        : "%rbx", "%rcx"
    );
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

/// VM-safe RDTSC (lfence instead of cpuid to avoid VM Exit penalty)
[[nodiscard]] inline std::uint64_t rdtsc_vm_safe() noexcept {
    std::uint64_t lo, hi;
    asm volatile (
        "lfence\n\t"
        "rdtsc\n\t"
        "lfence\n\t"
        : "=a"(lo), "=d"(hi)
    );
    return (hi << 32) | lo;
}

/// RDTSCP — includes processor ID, partial ordering guarantee
[[nodiscard]] inline std::uint64_t rdtscp(std::uint32_t* processor_id = nullptr) noexcept {
    std::uint32_t lo, hi, aux;
    asm volatile ("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    if (processor_id) *processor_id = aux;
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

#endif // NBT_HAS_RDTSC

/// Compiler barrier — prevent reordering around measurements
inline void compiler_barrier() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#endif
}

// ============================================================================
// Cycle to Time Conversion
// ============================================================================

/// Convert cycles to nanoseconds given CPU frequency in GHz
[[nodiscard]] inline double cycles_to_ns(std::uint64_t cycles, double freq_ghz) noexcept {
    return static_cast<double>(cycles) / freq_ghz;
}

#if NBT_HAS_RDTSC

/// Estimate CPU frequency in GHz (sleep-based, may underestimate under throttling)
[[nodiscard]] inline double estimate_cpu_freq_ghz() noexcept {
    auto start_time = std::chrono::steady_clock::now();
    std::uint64_t start_cycles = rdtsc_vm_safe();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::uint64_t end_cycles = rdtsc_vm_safe();
    auto end_time = std::chrono::steady_clock::now();
    double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count());
    return static_cast<double>(end_cycles - start_cycles) / elapsed_ns;
}

/// Estimate CPU frequency in GHz (busy-wait, keeps CPU at full speed)
[[nodiscard]] inline double estimate_cpu_freq_ghz_busy() noexcept {
    auto start_time = std::chrono::steady_clock::now();
    std::uint64_t start_cycles = rdtsc_vm_safe();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::milliseconds(100)) {
        asm volatile("pause");
    }
    std::uint64_t end_cycles = rdtsc_vm_safe();
    auto end_time = std::chrono::steady_clock::now();
    double elapsed_ns = std::chrono::duration<double, std::nano>(end_time - start_time).count();
    return static_cast<double>(end_cycles - start_cycles) / elapsed_ns;
}

#endif // NBT_HAS_RDTSC

// ============================================================================
// CPU Affinity & Scheduling
// ============================================================================

/// Bind current thread to a specific CPU core (Linux only)
[[nodiscard]] inline bool bind_to_core([[maybe_unused]] int core_id) noexcept {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    return false;
#endif
}

/// Get the current CPU core
[[nodiscard]] inline int get_current_core() noexcept {
#ifdef __linux__
    return sched_getcpu();
#else
    return -1;
#endif
}

/// Set real-time scheduling (SCHED_FIFO). Requires CAP_SYS_NICE or root.
[[nodiscard]] inline bool set_realtime_priority([[maybe_unused]] int priority = 99) noexcept {
#ifdef __linux__
    struct sched_param param{};
    param.sched_priority = priority;
    return sched_setscheduler(0, SCHED_FIFO, &param) == 0;
#else
    return false;
#endif
}

/// Convenience: bind to core + set realtime priority
[[nodiscard]] inline bool setup_benchmark_thread(int core_id, int priority = 99) noexcept {
    if (!bind_to_core(core_id)) return false;
    return set_realtime_priority(priority);
}

// ============================================================================
// RDTSC-based Latency Statistics
// ============================================================================

/// Latency statistics with percentile computation
struct LatencyStats {
    double min_ns{0};
    double max_ns{0};
    double mean_ns{0};
    double stddev_ns{0};
    double p50_ns{0};
    double p90_ns{0};
    double p99_ns{0};
    double p999_ns{0};
    std::size_t count{0};

    /// Compute statistics from a vector of cycle counts
    void compute(std::vector<std::uint64_t>& cycles, double freq_ghz) {
        if (cycles.empty()) return;
        count = cycles.size();
        std::sort(cycles.begin(), cycles.end());

        auto to_ns = [freq_ghz](std::uint64_t c) { return cycles_to_ns(c, freq_ghz); };

        min_ns = to_ns(cycles.front());
        max_ns = to_ns(cycles.back());

        double sum = 0;
        for (auto c : cycles) sum += to_ns(c);
        mean_ns = sum / static_cast<double>(count);

        double sq_sum = 0;
        for (auto c : cycles) {
            double diff = to_ns(c) - mean_ns;
            sq_sum += diff * diff;
        }
        stddev_ns = std::sqrt(sq_sum / static_cast<double>(count));

        p50_ns = to_ns(cycles[count / 2]);
        p90_ns = to_ns(cycles[count * 90 / 100]);
        p99_ns = to_ns(cycles[count * 99 / 100]);
        p999_ns = to_ns(cycles[count * 999 / 1000]);
    }

    /// Compute statistics from a vector of nanoseconds (chrono path)
    void compute_from_ns(std::vector<double>& samples) {
        if (samples.empty()) return;
        count = samples.size();
        std::sort(samples.begin(), samples.end());

        min_ns = samples.front();
        max_ns = samples.back();
        mean_ns = std::accumulate(samples.begin(), samples.end(), 0.0) /
                  static_cast<double>(count);

        double sq_sum = 0;
        for (auto s : samples) {
            double diff = s - mean_ns;
            sq_sum += diff * diff;
        }
        stddev_ns = std::sqrt(sq_sum / static_cast<double>(count));

        p50_ns = samples[count / 2];
        p90_ns = samples[count * 90 / 100];
        p99_ns = samples[count * 99 / 100];
        p999_ns = samples[count * 999 / 1000];
    }
};

// ============================================================================
// Scoped Timers
// ============================================================================

#if NBT_HAS_RDTSC
/// RAII timer using RDTSC — writes elapsed cycles to output
class ScopedRdtscTimer {
public:
    explicit ScopedRdtscTimer(std::uint64_t& output) noexcept
        : output_(output), start_(rdtsc_vm_safe()) {}

    ~ScopedRdtscTimer() { output_ = rdtsc_vm_safe() - start_; }

    ScopedRdtscTimer(const ScopedRdtscTimer&) = delete;
    ScopedRdtscTimer& operator=(const ScopedRdtscTimer&) = delete;

private:
    std::uint64_t& output_;
    std::uint64_t start_;
};
#endif

/// RAII timer using chrono — writes elapsed nanoseconds
class ScopedChronoTimer {
public:
    using Clock = std::chrono::steady_clock;

    explicit ScopedChronoTimer(std::chrono::nanoseconds& output) noexcept
        : output_(output), start_(Clock::now()) {}

    ~ScopedChronoTimer() { output_ = Clock::now() - start_; }

    ScopedChronoTimer(const ScopedChronoTimer&) = delete;
    ScopedChronoTimer& operator=(const ScopedChronoTimer&) = delete;

private:
    std::chrono::nanoseconds& output_;
    Clock::time_point start_;
};

// ============================================================================
// Warmup Utilities
// ============================================================================

/// Warm up instruction cache by running function multiple times
template <typename Func>
inline void warmup_icache(Func&& func, std::size_t iterations = 10000) {
    for (std::size_t i = 0; i < iterations; ++i) {
        compiler_barrier();
        func();
        compiler_barrier();
    }
}

/// Warm up data cache by touching memory at cache-line stride
inline void warmup_dcache(void* data, std::size_t size) {
    volatile char* p = static_cast<volatile char*>(data);
    for (std::size_t i = 0; i < size; i += 64) {
        (void)p[i];
    }
}

// ============================================================================
// Comparison Output Utilities
// ============================================================================

/// Print before/after comparison with delta percentage
inline void print_comparison(const char* label,
                             double before_ns,
                             double after_ns,
                             int width = 12) {
    double delta_pct = (before_ns - after_ns) / before_ns * 100.0;
    std::cout << std::setw(30) << std::left << label
              << std::setw(width) << std::fixed << std::setprecision(1) << before_ns
              << std::setw(width) << after_ns
              << std::setw(width) << std::showpos << delta_pct << "%"
              << std::noshowpos << '\n';
}

/// Print comparison header
inline void print_comparison_header(const char* before_label = "Before",
                                    const char* after_label = "After") {
    std::cout << std::setw(30) << std::left << "Operation"
              << std::setw(12) << before_label
              << std::setw(12) << after_label
              << std::setw(12) << "Delta\n";
    std::cout << std::string(66, '-') << '\n';
}

// ============================================================================
// Allocation Counting (existing)
// ============================================================================

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

// --- Allocation Counter ---------------------------------------------------

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

// --- Sample Summary -------------------------------------------------------

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

// --- Benchmark Runners ----------------------------------------------------

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

// --- Output: Human-Readable -----------------------------------------------

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

// --- Output: JSON ---------------------------------------------------------

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

// --- Cold/Warm Comparison --------------------------------------------------

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

// --- Dataset Path Helper --------------------------------------------------

inline std::string source_path(const std::string& relative) {
    return (std::filesystem::path(SF_SOURCE_DIR) / relative).string();
}

} // namespace stratforge::bench
