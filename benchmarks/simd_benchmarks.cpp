#include "benchmark_utils.hpp"

#include <stratforge/simd/simd_ops.hpp>
#include <stratforge/data/csv_data.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/indicators/variance.hpp>
#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/lowest.hpp>
#include <stratforge/indicators/statistics.hpp>

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace stratforge::bench;

// ============================================================================
// Scalar-only indicator replicas for before/after comparison.
// These reproduce the original pre-SIMD bar-by-bar loops exactly.
// ============================================================================

namespace scalar_ind {

class SMA_Scalar : public stratforge::Indicator<SMA_Scalar> {
public:
    explicit SMA_Scalar(const stratforge::Line<double>& source, std::size_t period)
        : source_(source), period_(period == 0 ? 1 : period) {}
    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN()); return;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) { sum += source_.data()[idx - i]; }
        line_.forward(sum / static_cast<double>(period_));
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return period_; }
private:
    const stratforge::Line<double>& source_;
    std::size_t period_;
};

class BollingerBands_Scalar : public stratforge::Indicator<BollingerBands_Scalar> {
public:
    explicit BollingerBands_Scalar(const stratforge::Line<double>& source, std::size_t period = 20, double devfactor = 2.0)
        : source_(source), period_(period), devfactor_(devfactor) {}
    void next_impl() {
        if (line_.empty()) [[unlikely]] {
            const auto n = source_.size();
            reserve_output(n); top_.data().reserve(n); bottom_.data().reserve(n);
        }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            line_.forward(nan); top_.forward(nan); bottom_.forward(nan); return;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) { sum += source_.data()[idx - i]; }
        const double mean = sum / static_cast<double>(period_);
        double var_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double d = source_.data()[idx - i] - mean; var_sum += d * d;
        }
        const double sd = std::sqrt(var_sum / static_cast<double>(period_));
        line_.forward(mean); top_.forward(mean + devfactor_ * sd); bottom_.forward(mean - devfactor_ * sd);
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return period_; }
private:
    const stratforge::Line<double>& source_;
    std::size_t period_;
    double devfactor_;
    stratforge::Line<double> top_;
    stratforge::Line<double> bottom_;
};

class StdDev_Scalar : public stratforge::Indicator<StdDev_Scalar> {
public:
    explicit StdDev_Scalar(const stratforge::Line<double>& source, std::size_t period)
        : source_(source), period_(period == 0 ? 1 : period) {}
    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN()); return;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) { sum += source_.data()[idx - i]; }
        const double mean = sum / static_cast<double>(period_);
        double var_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double d = source_.data()[idx - i] - mean; var_sum += d * d;
        }
        line_.forward(std::sqrt(var_sum / static_cast<double>(period_)));
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return period_; }
private:
    const stratforge::Line<double>& source_;
    std::size_t period_;
};

class Variance_Scalar : public stratforge::Indicator<Variance_Scalar> {
public:
    explicit Variance_Scalar(const stratforge::Line<double>& source, std::size_t period = 5)
        : source_(source), period_(period == 0 ? 1 : period) {}
    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN()); return;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) { sum += source_.data()[idx - i]; }
        const double mean = sum / static_cast<double>(period_);
        double var_sum = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double d = source_.data()[idx - i] - mean; var_sum += d * d;
        }
        line_.forward(var_sum / static_cast<double>(period_));
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return period_; }
private:
    const stratforge::Line<double>& source_;
    std::size_t period_;
};

class Highest_Scalar : public stratforge::Indicator<Highest_Scalar> {
public:
    explicit Highest_Scalar(const stratforge::Line<double>& source, std::size_t period)
        : source_(source), period_(period) {}
    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN()); return;
        }
        double h = source_.data()[idx];
        for (std::size_t i = 1; i < period_; ++i) {
            const double c = source_.data()[idx - i]; if (c > h) { h = c; }
        }
        line_.forward(h);
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return period_; }
private:
    const stratforge::Line<double>& source_;
    std::size_t period_;
};

class Lowest_Scalar : public stratforge::Indicator<Lowest_Scalar> {
public:
    explicit Lowest_Scalar(const stratforge::Line<double>& source, std::size_t period)
        : source_(source), period_(period) {}
    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(source_.size()); }
        const auto idx = source_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN()); return;
        }
        double l = source_.data()[idx];
        for (std::size_t i = 1; i < period_; ++i) {
            const double c = source_.data()[idx - i]; if (c < l) { l = c; }
        }
        line_.forward(l);
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return period_; }
private:
    const stratforge::Line<double>& source_;
    std::size_t period_;
};

class Correlation_Scalar : public stratforge::Indicator<Correlation_Scalar> {
public:
    Correlation_Scalar(const stratforge::Line<double>& d0, const stratforge::Line<double>& d1, std::size_t period = 20)
        : d0_(d0), d1_(d1), period_(period == 0 ? 1 : period) {}
    void next_impl() {
        if (line_.empty()) [[unlikely]] { reserve_output(d0_.size()); }
        const auto idx = d0_.index();
        if (idx + 1 < period_) [[unlikely]] {
            line_.forward(std::numeric_limits<double>::quiet_NaN()); return;
        }
        double sx = 0.0, sy = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            sx += d0_.data()[idx - i]; sy += d1_.data()[idx - i];
        }
        const double mx = sx / static_cast<double>(period_);
        const double my = sy / static_cast<double>(period_);
        double cov = 0.0, vx = 0.0, vy = 0.0;
        for (std::size_t i = 0; i < period_; ++i) {
            const double dx = d0_.data()[idx - i] - mx;
            const double dy = d1_.data()[idx - i] - my;
            cov += dx * dy; vx += dx * dx; vy += dy * dy;
        }
        const double denom = std::sqrt(vx * vy);
        line_.forward(denom == 0.0 ? std::numeric_limits<double>::quiet_NaN() : cov / denom);
    }
    [[nodiscard]] std::size_t minimum_period_impl() const noexcept { return period_; }
private:
    const stratforge::Line<double>& d0_;
    const stratforge::Line<double>& d1_;
    std::size_t period_;
};

} // namespace scalar_ind

// ============================================================================
// Benchmark infrastructure
// ============================================================================

stratforge::CsvData load_feed(const std::string& relative_path) {
    stratforge::CsvData feed(stratforge::CsvData::Params{
        .filename = source_path(relative_path),
        .columns = {},
        .date_format = "%Y-%m-%d",
        .separator = ',',
        .has_headers = true,
        .fromdate = std::nullopt,
        .todate = std::nullopt,
    });
    if (!feed.load()) {
        throw std::runtime_error("Unable to load benchmark dataset: " + relative_path);
    }
    return feed;
}

/// Benchmark an indicator's hot path only — NO clone/load in timing loop.
///
/// Approach: the feed is already loaded and its Lines hold contiguous data.
/// Each iteration we reset the feed's Line cursors via home(), construct a
/// fresh indicator (cheap — just sets period + pointer), and run the next()
/// loop.  This isolates the indicator computation from I/O overhead.
template <typename IndicatorFactory>
SampleSummary bench_hotpath(const std::string& label,
                             stratforge::CsvData& preloaded,
                             std::size_t iterations,
                             IndicatorFactory&& make,
                             JsonReport& report) {
    const auto bars = preloaded.size();

    // Helper: reset all feed Line cursors to bar 0 (data stays intact)
    auto reset_feed = [&]() {
        preloaded.datetime().home();
        preloaded.open().home();
        preloaded.high().home();
        preloaded.low().home();
        preloaded.close().home();
        preloaded.volume().home();
        preloaded.openinterest().home();
    };

    const auto summary = run_benchmark(iterations, [&]() {
        reset_feed();
        auto ind = make(preloaded);
        for (std::size_t i = 0; i < bars; ++i) {
            ind.next();
            if (i + 1 < bars) { preloaded.advance(); }
        }
    });
    report.add({.name = label, .summary = summary, .bars_per_iteration = bars});
    return summary;
}

/// Run SIMD and scalar indicator side-by-side using per-bar timing, print speedup.
template <typename SimdFactory, typename ScalarFactory>
void bench_indicator_comparison(const std::string& name,
                                 stratforge::CsvData& feed,
                                 std::size_t iterations,
                                 SimdFactory&& make_simd,
                                 ScalarFactory&& make_scalar,
                                 JsonReport& report) {
    const auto simd_s = bench_hotpath(name + " SIMD", feed, iterations, make_simd, report);
    const auto scalar_s = bench_hotpath(name + " scalar", feed, iterations, make_scalar, report);

    const double speedup = static_cast<double>(scalar_s.p50_ns)
                           / static_cast<double>(simd_s.p50_ns);

    std::cout << "  " << std::left << std::setw(40) << (name + " SIMD")
              << "  p50=" << std::setw(12) << simd_s.p50_ns << " ns\n";
    std::cout << "  " << std::left << std::setw(40) << (name + " scalar")
              << "  p50=" << std::setw(12) << scalar_s.p50_ns << " ns"
              << "  speedup=" << std::fixed << std::setprecision(2) << speedup << "x\n";
}

// --- Primitive comparison --------------------------------------------------

void bench_primitive_comparison(const std::string& name,
                                 std::size_t count,
                                 std::size_t iterations,
                                 auto simd_fn,
                                 auto scalar_fn,
                                 JsonReport& report) {
    std::vector<double> data(count);
    for (std::size_t i = 0; i < count; ++i) {
        data[i] = 100.0 + static_cast<double>(i % 50) * 0.37;
    }
    volatile double sink = 0.0;

    const std::string simd_label = name + "(" + std::to_string(count) + ") SIMD";
    const std::string scalar_label = name + "(" + std::to_string(count) + ") scalar";

    const auto simd_s = run_benchmark(iterations, [&]() { sink = simd_fn(data.data(), data.size()); });
    report.add({.name = simd_label, .summary = simd_s, .bars_per_iteration = 1});

    const auto scalar_s = run_benchmark(iterations, [&]() { sink = scalar_fn(data.data(), data.size()); });
    report.add({.name = scalar_label, .summary = scalar_s, .bars_per_iteration = 1});

    const double speedup = static_cast<double>(scalar_s.p50_ns) / static_cast<double>(simd_s.p50_ns);
    std::cout << "  " << std::left << std::setw(32) << simd_label
              << "  p50=" << std::setw(8) << simd_s.p50_ns << " ns\n";
    std::cout << "  " << std::left << std::setw(32) << scalar_label
              << "  p50=" << std::setw(8) << scalar_s.p50_ns << " ns"
              << "  speedup=" << std::fixed << std::setprecision(2) << speedup << "x\n";
    (void)sink;
}

void bench_mean_variance_comparison(std::size_t count, std::size_t iterations, JsonReport& report) {
    std::vector<double> data(count);
    for (std::size_t i = 0; i < count; ++i) {
        data[i] = 100.0 + static_cast<double>(i % 50) * 0.37;
    }
    volatile double sink = 0.0;

    const std::string simd_label = "reduce_mean_variance(" + std::to_string(count) + ") SIMD";
    const std::string scalar_label = "reduce_mean_variance(" + std::to_string(count) + ") scalar";

    const auto simd_s = run_benchmark(iterations, [&]() {
        const auto [m, v] = stratforge::simd::reduce_mean_variance(data.data(), data.size());
        sink = m + v;
    });
    report.add({.name = simd_label, .summary = simd_s, .bars_per_iteration = 1});

    const auto scalar_s = run_benchmark(iterations, [&]() {
        const auto [m, v] = stratforge::simd::scalar::reduce_mean_variance(data.data(), data.size());
        sink = m + v;
    });
    report.add({.name = scalar_label, .summary = scalar_s, .bars_per_iteration = 1});

    const double speedup = static_cast<double>(scalar_s.p50_ns) / static_cast<double>(simd_s.p50_ns);
    std::cout << "  " << std::left << std::setw(40) << simd_label
              << "  p50=" << std::setw(8) << simd_s.p50_ns << " ns\n";
    std::cout << "  " << std::left << std::setw(40) << scalar_label
              << "  p50=" << std::setw(8) << scalar_s.p50_ns << " ns"
              << "  speedup=" << std::fixed << std::setprecision(2) << speedup << "x\n";
    (void)sink;
}

} // namespace

int main() {
    constexpr std::size_t iterations = 1000;
    const std::string dataset_512 = "tools/golden_extract/datas/2005-2006-day-001.txt";
    const std::string dataset_100k = "build/bench_data/synthetic_100k.csv";

    std::cout << "=== StratForge SIMD Benchmarks ===\n";
    std::cout << "Dispatched ISA: " << stratforge::simd::dispatched_arch_name() << '\n';
    std::cout << "Iterations: " << iterations << "\n\n";

    JsonReport report;

    // --- Raw primitive: SIMD vs Scalar side-by-side -------------------------
    std::cout << "--- Primitive: SIMD vs Scalar ---\n";
    for (std::size_t count : {20uz, 50uz, 200uz, 1000uz}) {
        bench_primitive_comparison("reduce_sum", count, iterations,
            stratforge::simd::reduce_sum, stratforge::simd::scalar::reduce_sum, report);
        bench_primitive_comparison("reduce_min", count, iterations,
            stratforge::simd::reduce_min, stratforge::simd::scalar::reduce_min, report);
        bench_primitive_comparison("reduce_max", count, iterations,
            stratforge::simd::reduce_max, stratforge::simd::scalar::reduce_max, report);
        bench_mean_variance_comparison(count, iterations, report);
        std::cout << '\n';
    }

    // --- Indicator: SIMD vs Scalar side-by-side (all accelerated) -----------
    auto feed_512 = load_feed(dataset_512);
    std::cout << "Loaded 512-bar dataset: " << feed_512.size() << " bars\n\n";

    std::unique_ptr<stratforge::CsvData> feed_100k_ptr;
    const auto path_100k = source_path(dataset_100k);
    if (std::filesystem::exists(path_100k)) {
        feed_100k_ptr = std::make_unique<stratforge::CsvData>(stratforge::CsvData::Params{
            .filename = path_100k, .columns = {}, .date_format = "%Y-%m-%d",
            .separator = ',', .has_headers = true,
            .fromdate = std::nullopt, .todate = std::nullopt,
        });
        if (feed_100k_ptr->load()) {
            std::cout << "Loaded 100K-bar dataset: " << feed_100k_ptr->size() << " bars\n\n";
        } else {
            feed_100k_ptr.reset();
        }
    }

    constexpr std::size_t ind_iter = 100;

    std::cout << "--- Indicator: SIMD vs Scalar (512 bars) ---\n";

    bench_indicator_comparison("SMA(30)", feed_512, ind_iter,
        [](stratforge::CsvData& f) { return stratforge::SMA(f.close(), 30); },
        [](stratforge::CsvData& f) { return scalar_ind::SMA_Scalar(f.close(), 30); }, report);

    bench_indicator_comparison("BollingerBands(20,2)", feed_512, ind_iter,
        [](stratforge::CsvData& f) { return stratforge::BollingerBands(f.close(), 20, 2.0); },
        [](stratforge::CsvData& f) { return scalar_ind::BollingerBands_Scalar(f.close(), 20, 2.0); }, report);

    bench_indicator_comparison("StdDev(20)", feed_512, ind_iter,
        [](stratforge::CsvData& f) { return stratforge::StdDev(f.close(), 20); },
        [](stratforge::CsvData& f) { return scalar_ind::StdDev_Scalar(f.close(), 20); }, report);

    bench_indicator_comparison("Variance(20)", feed_512, ind_iter,
        [](stratforge::CsvData& f) { return stratforge::Variance(f.close(), 20); },
        [](stratforge::CsvData& f) { return scalar_ind::Variance_Scalar(f.close(), 20); }, report);

    bench_indicator_comparison("Highest(30)", feed_512, ind_iter,
        [](stratforge::CsvData& f) { return stratforge::Highest(f.high(), 30); },
        [](stratforge::CsvData& f) { return scalar_ind::Highest_Scalar(f.high(), 30); }, report);

    bench_indicator_comparison("Lowest(30)", feed_512, ind_iter,
        [](stratforge::CsvData& f) { return stratforge::Lowest(f.low(), 30); },
        [](stratforge::CsvData& f) { return scalar_ind::Lowest_Scalar(f.low(), 30); }, report);

    bench_indicator_comparison("Correlation(20)", feed_512, ind_iter,
        [](stratforge::CsvData& f) { return stratforge::Correlation(f.high(), f.low(), 20); },
        [](stratforge::CsvData& f) { return scalar_ind::Correlation_Scalar(f.high(), f.low(), 20); }, report);

    if (feed_100k_ptr) {
        std::cout << "\n--- Indicator: SIMD vs Scalar (100K bars) ---\n";

        bench_indicator_comparison("SMA(30) [100K]", *feed_100k_ptr, 10,
            [](stratforge::CsvData& f) { return stratforge::SMA(f.close(), 30); },
            [](stratforge::CsvData& f) { return scalar_ind::SMA_Scalar(f.close(), 30); }, report);

        bench_indicator_comparison("BollingerBands(20,2) [100K]", *feed_100k_ptr, 10,
            [](stratforge::CsvData& f) { return stratforge::BollingerBands(f.close(), 20, 2.0); },
            [](stratforge::CsvData& f) { return scalar_ind::BollingerBands_Scalar(f.close(), 20, 2.0); }, report);

        bench_indicator_comparison("StdDev(20) [100K]", *feed_100k_ptr, 10,
            [](stratforge::CsvData& f) { return stratforge::StdDev(f.close(), 20); },
            [](stratforge::CsvData& f) { return scalar_ind::StdDev_Scalar(f.close(), 20); }, report);

        bench_indicator_comparison("Variance(20) [100K]", *feed_100k_ptr, 10,
            [](stratforge::CsvData& f) { return stratforge::Variance(f.close(), 20); },
            [](stratforge::CsvData& f) { return scalar_ind::Variance_Scalar(f.close(), 20); }, report);

        bench_indicator_comparison("Highest(30) [100K]", *feed_100k_ptr, 10,
            [](stratforge::CsvData& f) { return stratforge::Highest(f.high(), 30); },
            [](stratforge::CsvData& f) { return scalar_ind::Highest_Scalar(f.high(), 30); }, report);

        bench_indicator_comparison("Lowest(30) [100K]", *feed_100k_ptr, 10,
            [](stratforge::CsvData& f) { return stratforge::Lowest(f.low(), 30); },
            [](stratforge::CsvData& f) { return scalar_ind::Lowest_Scalar(f.low(), 30); }, report);

        bench_indicator_comparison("Correlation(20) [100K]", *feed_100k_ptr, 10,
            [](stratforge::CsvData& f) { return stratforge::Correlation(f.high(), f.low(), 20); },
            [](stratforge::CsvData& f) { return scalar_ind::Correlation_Scalar(f.high(), f.low(), 20); }, report);
    }

    report.write(source_path("build/bench_results/simd_benchmarks.json"), "simd_benchmarks");
    std::cout << "\n=== SIMD Benchmarks Complete ===\n";
    return 0;
}
