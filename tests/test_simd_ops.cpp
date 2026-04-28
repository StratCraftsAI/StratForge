#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stratforge/simd/simd_ops.hpp>
#include <stratforge/indicators/sma.hpp>
#include <stratforge/indicators/bollinger.hpp>
#include <stratforge/indicators/stddev.hpp>
#include <stratforge/indicators/variance.hpp>
#include <stratforge/indicators/highest.hpp>
#include <stratforge/indicators/lowest.hpp>
#include <stratforge/indicators/sumn.hpp>
#include <stratforge/indicators/rsi.hpp>
#include <stratforge/indicators/statistics.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Runtime Dispatch Verification
// ============================================================================

TEST_CASE("simd::dispatched_arch_name  - returns non-empty ISA name", "[simd][dispatch]") {
    const char* name = stratforge::simd::dispatched_arch_name();
    REQUIRE(name != nullptr);
    REQUIRE(std::strlen(name) > 0);
    INFO("Runtime-dispatched ISA: " << name);

#if defined(SF_HAS_XSIMD) && SF_HAS_XSIMD
    // When xsimd is enabled, dispatch should select a real SIMD ISA on x86,
    // not fall through to the generic scalar emulation.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    const std::string arch(name);
    // Must be at least SSE2 on any x86-64 CPU
    REQUIRE_FALSE(arch.empty());
    // The name should NOT be the xsimd emulated/"generic" fallback
    REQUIRE(arch.find("emulated") == std::string::npos);
    // Verify it's a known x86 SIMD ISA
    const bool known_x86 = (arch.find("sse") != std::string::npos)
                        || (arch.find("avx") != std::string::npos)
                        || (arch.find("fma") != std::string::npos);
    REQUIRE(known_x86);
#elif defined(__aarch64__) || defined(_M_ARM64)
    const std::string arch(name);
    REQUIRE(arch.find("neon") != std::string::npos);
#endif
#else
    // Scalar-only build
    REQUIRE(std::string(name).find("scalar") != std::string::npos);
#endif
}

// ============================================================================
// SIMD Primitive Unit Tests
// ============================================================================

TEST_CASE("simd::reduce_sum  - basic correctness", "[simd]") {
    const std::vector<double> data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    REQUIRE_THAT(stratforge::simd::reduce_sum(data.data(), data.size()),
                 WithinAbs(36.0, 1e-12));
}

TEST_CASE("simd::reduce_sum  - single element", "[simd]") {
    const double val = 42.5;
    REQUIRE_THAT(stratforge::simd::reduce_sum(&val, 1), WithinAbs(42.5, 1e-12));
}

TEST_CASE("simd::reduce_sum  - count not multiple of batch size", "[simd]") {
    const std::vector<double> data = {1.0, 2.0, 3.0, 4.0, 5.0};
    REQUIRE_THAT(stratforge::simd::reduce_sum(data.data(), data.size()),
                 WithinAbs(15.0, 1e-12));
}

TEST_CASE("simd::reduce_sum  - large array matches std::accumulate", "[simd]") {
    std::vector<double> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<double>(i) * 0.1;
    }
    const double expected = std::accumulate(data.begin(), data.end(), 0.0);
    REQUIRE_THAT(stratforge::simd::reduce_sum(data.data(), data.size()),
                 WithinRel(expected, 1e-10));
}

TEST_CASE("simd::reduce_min  - basic correctness", "[simd]") {
    const std::vector<double> data = {5.0, 3.0, 8.0, 1.0, 7.0, 2.0, 9.0, 4.0};
    REQUIRE(stratforge::simd::reduce_min(data.data(), data.size()) == 1.0);
}

TEST_CASE("simd::reduce_min  - single element", "[simd]") {
    const double val = 42.5;
    REQUIRE(stratforge::simd::reduce_min(&val, 1) == 42.5);
}

TEST_CASE("simd::reduce_min  - large array", "[simd]") {
    std::vector<double> data(500);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<double>(i) + 100.0;
    }
    data[317] = 0.5;
    REQUIRE(stratforge::simd::reduce_min(data.data(), data.size()) == 0.5);
}

TEST_CASE("simd::reduce_max  - basic correctness", "[simd]") {
    const std::vector<double> data = {5.0, 3.0, 8.0, 1.0, 7.0, 2.0, 9.0, 4.0};
    REQUIRE(stratforge::simd::reduce_max(data.data(), data.size()) == 9.0);
}

TEST_CASE("simd::reduce_max  - single element", "[simd]") {
    const double val = 42.5;
    REQUIRE(stratforge::simd::reduce_max(&val, 1) == 42.5);
}

TEST_CASE("simd::reduce_max  - large array", "[simd]") {
    std::vector<double> data(500);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<double>(i);
    }
    data[123] = 9999.0;
    REQUIRE(stratforge::simd::reduce_max(data.data(), data.size()) == 9999.0);
}

TEST_CASE("simd::reduce_mean_variance  - basic correctness", "[simd]") {
    const std::vector<double> data = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    const auto [mean, variance] = stratforge::simd::reduce_mean_variance(data.data(), data.size());
    REQUIRE_THAT(mean, WithinAbs(5.0, 1e-12));
    REQUIRE_THAT(variance, WithinAbs(4.0, 1e-12));
}

TEST_CASE("simd::reduce_mean_variance  - single element", "[simd]") {
    const double val = 7.0;
    const auto [mean, variance] = stratforge::simd::reduce_mean_variance(&val, 1);
    REQUIRE_THAT(mean, WithinAbs(7.0, 1e-12));
    REQUIRE_THAT(variance, WithinAbs(0.0, 1e-12));
}

// ============================================================================
// : SIMD Scalar Tail Boundary Tests
// Exhaustive edge-case coverage for SIMD/scalar boundary transitions.
// ============================================================================

TEST_CASE("simd::reduce_sum  - boundary: count = 0", "[simd][boundary]") {
    const double dummy = 0.0;
    REQUIRE(stratforge::simd::reduce_sum(&dummy, 0) == 0.0);
    REQUIRE(stratforge::simd::scalar::reduce_sum(&dummy, 0) == 0.0);
}

TEST_CASE("simd::reduce_min  - boundary: count = 0", "[simd][boundary]") {
    const double dummy = 0.0;
    REQUIRE(stratforge::simd::reduce_min(&dummy, 0) == 0.0);
    REQUIRE(stratforge::simd::scalar::reduce_min(&dummy, 0) == 0.0);
}

TEST_CASE("simd::reduce_max  - boundary: count = 0", "[simd][boundary]") {
    const double dummy = 0.0;
    REQUIRE(stratforge::simd::reduce_max(&dummy, 0) == 0.0);
    REQUIRE(stratforge::simd::scalar::reduce_max(&dummy, 0) == 0.0);
}

TEST_CASE("simd  - boundary sweep: all ops cross-validated at critical sizes", "[simd][boundary]") {
    // Test all critical boundaries for every possible double batch size
    // {2 (SSE), 4 (AVX), 8 (AVX-512)}: bs-1, bs, bs+1, 2*bs-1, 2*bs, 2*bs+1
    std::vector<std::size_t> critical_sizes = {0, 1};
    for (std::size_t bs : {2u, 4u, 8u}) {
        critical_sizes.push_back(bs - 1);
        critical_sizes.push_back(bs);
        critical_sizes.push_back(bs + 1);
        critical_sizes.push_back(2 * bs - 1);
        critical_sizes.push_back(2 * bs);
        critical_sizes.push_back(2 * bs + 1);
    }
    // Add large non-power-of-2 boundary
    critical_sizes.push_back(127);
    critical_sizes.push_back(255);
    critical_sizes.push_back(1023);

    // Build test data large enough for all sizes
    std::vector<double> data(1024);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = 100.0 + static_cast<double>(i) * 0.37 - static_cast<double>(i % 7) * 1.1;
    }

    for (const auto count : critical_sizes) {
        if (count == 0) continue; // count=0 tested separately above
        CAPTURE(count);

        // reduce_sum: SIMD vs scalar within ULP tolerance
        {
            const double simd_val = stratforge::simd::reduce_sum(data.data(), count);
            const double scalar_val = stratforge::simd::scalar::reduce_sum(data.data(), count);
            REQUIRE_THAT(simd_val, WithinRel(scalar_val, 1e-12));
        }

        // reduce_min: SIMD vs scalar bit-identical
        {
            const double simd_val = stratforge::simd::reduce_min(data.data(), count);
            const double scalar_val = stratforge::simd::scalar::reduce_min(data.data(), count);
            REQUIRE(simd_val == scalar_val);
        }

        // reduce_max: SIMD vs scalar bit-identical
        {
            const double simd_val = stratforge::simd::reduce_max(data.data(), count);
            const double scalar_val = stratforge::simd::scalar::reduce_max(data.data(), count);
            REQUIRE(simd_val == scalar_val);
        }

        // reduce_mean_variance: SIMD vs scalar within ULP tolerance
        {
            const auto [s_mean, s_var] = stratforge::simd::scalar::reduce_mean_variance(data.data(), count);
            const auto [v_mean, v_var] = stratforge::simd::reduce_mean_variance(data.data(), count);
            REQUIRE_THAT(v_mean, WithinRel(s_mean, 1e-12));
            REQUIRE_THAT(v_var, WithinRel(s_var, 1e-10));
        }
    }
}

TEST_CASE("simd  - boundary: min/max value in scalar tail", "[simd][boundary]") {
    // Place the extreme value in the scalar tail (last element of a non-multiple-of-bs array)
    // to verify the scalar tail path actually processes those elements.
    for (std::size_t bs : {2u, 4u, 8u}) {
        const std::size_t count = bs + 1; // one SIMD batch + one scalar tail element
        CAPTURE(bs, count);

        std::vector<double> data(count, 50.0); // all 50s
        data.back() = 1.0;  // min in scalar tail

        REQUIRE(stratforge::simd::reduce_min(data.data(), count) == 1.0);
        REQUIRE(stratforge::simd::scalar::reduce_min(data.data(), count) == 1.0);

        data.back() = 999.0; // max in scalar tail
        REQUIRE(stratforge::simd::reduce_max(data.data(), count) == 999.0);
        REQUIRE(stratforge::simd::scalar::reduce_max(data.data(), count) == 999.0);
    }
}

TEST_CASE("simd  - boundary: sum contribution from scalar tail", "[simd][boundary]") {
    // Verify the scalar tail's contribution is included in the sum.
    for (std::size_t bs : {2u, 4u, 8u}) {
        const std::size_t count = bs + 1;
        CAPTURE(bs, count);

        std::vector<double> data(count, 0.0);
        data.back() = 42.0; // only the scalar tail element is non-zero

        REQUIRE_THAT(stratforge::simd::reduce_sum(data.data(), count), WithinAbs(42.0, 1e-12));
    }
}

// ============================================================================
// SIMD vs Scalar cross-validation
// ============================================================================

TEST_CASE("simd::reduce_min  - bit-identical to scalar", "[simd][cross]") {
    std::vector<double> data(500);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = 1000.0 - static_cast<double>(i) * 1.7 + static_cast<double>(i % 17);
    }
    REQUIRE(stratforge::simd::reduce_min(data.data(), data.size())
            == stratforge::simd::scalar::reduce_min(data.data(), data.size()));
}

TEST_CASE("simd::reduce_max  - bit-identical to scalar", "[simd][cross]") {
    std::vector<double> data(500);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = 1000.0 - static_cast<double>(i) * 1.7 + static_cast<double>(i % 17);
    }
    REQUIRE(stratforge::simd::reduce_max(data.data(), data.size())
            == stratforge::simd::scalar::reduce_max(data.data(), data.size()));
}

TEST_CASE("simd::reduce_sum  - within ULP tolerance of scalar", "[simd][cross]") {
    // Financial-scale data: sums of ~100-valued doubles over windows of 20-200
    std::vector<double> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = 100.0 + static_cast<double>(i % 50) * 0.37;
    }
    const double simd_result = stratforge::simd::reduce_sum(data.data(), data.size());
    const double scalar_result = stratforge::simd::scalar::reduce_sum(data.data(), data.size());
    // ULP tolerance: relative error < 1e-12 (well within  Golden Master)
    REQUIRE_THAT(simd_result, WithinRel(scalar_result, 1e-12));
}

TEST_CASE("simd::reduce_mean_variance  - within ULP tolerance of scalar", "[simd][cross]") {
    std::vector<double> data(200);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = 100.0 + static_cast<double>(i % 50) * 0.37;
    }
    const auto [s_mean, s_var] = stratforge::simd::scalar::reduce_mean_variance(data.data(), data.size());
    const auto [v_mean, v_var] = stratforge::simd::reduce_mean_variance(data.data(), data.size());
    REQUIRE_THAT(v_mean, WithinRel(s_mean, 1e-12));
    REQUIRE_THAT(v_var, WithinRel(s_var, 1e-12));
}

// ============================================================================
// Indicator Integration Tests  - verify SIMD indicators match scalar reference
// ============================================================================

namespace {

stratforge::Line<double> make_line(const std::vector<double>& values) {
    stratforge::Line<double> line;
    for (const auto& v : values) {
        line.forward(v);
    }
    return line;
}

template <typename Indicator>
void run_all(stratforge::Line<double>& source, Indicator& ind) {
    source.home();
    for (std::size_t i = 0; i < source.size(); ++i) {
        ind.next();
        if (i + 1 < source.size()) { source.advance(); }
    }
}

const std::vector<double> test_data = {
    100.0, 101.5, 99.8, 102.3, 98.5, 103.0, 104.2, 97.6,
    105.1, 106.3, 99.0, 101.8, 103.5, 100.2, 98.7, 104.8,
    107.0, 102.5, 99.3, 106.7, 108.2, 101.0, 103.8, 105.5,
    100.8, 107.3, 109.0, 102.1, 104.6, 106.0
};

} // namespace

TEST_CASE("SMA with SIMD matches scalar reference", "[simd][indicator]") {
    constexpr std::size_t period = 10;
    auto source = make_line(test_data);
    stratforge::SMA sma(source, period);
    run_all(source, sma);

    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        const double expected = stratforge::simd::scalar::reduce_sum(&test_data[i - period + 1], period) / static_cast<double>(period);
        REQUIRE_THAT(sma.line().data()[i], WithinRel(expected, 1e-12));
    }
}

TEST_CASE("BollingerBands with SIMD matches scalar reference", "[simd][indicator]") {
    constexpr std::size_t period = 10;
    constexpr double devfactor = 2.0;
    auto source = make_line(test_data);
    stratforge::BollingerBands bb(source, period, devfactor);
    run_all(source, bb);

    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        const auto [mean, var] = stratforge::simd::scalar::reduce_mean_variance(&test_data[i - period + 1], period);
        const double sd = std::sqrt(var);

        REQUIRE_THAT(bb.mid().data()[i], WithinRel(mean, 1e-12));
        REQUIRE_THAT(bb.top().data()[i], WithinRel(mean + devfactor * sd, 1e-12));
        REQUIRE_THAT(bb.bottom().data()[i], WithinRel(mean - devfactor * sd, 1e-12));
    }
}

TEST_CASE("StdDev with SIMD matches scalar reference", "[simd][indicator]") {
    constexpr std::size_t period = 10;
    auto source = make_line(test_data);
    stratforge::StdDev sd(source, period);
    run_all(source, sd);

    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        const auto [mean, var] = stratforge::simd::scalar::reduce_mean_variance(&test_data[i - period + 1], period);
        REQUIRE_THAT(sd.line().data()[i], WithinRel(std::sqrt(var), 1e-12));
    }
}

TEST_CASE("Variance with SIMD matches scalar reference", "[simd][indicator]") {
    constexpr std::size_t period = 10;
    auto source = make_line(test_data);
    stratforge::Variance var(source, period);
    run_all(source, var);

    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        const auto [mean, expected_var] = stratforge::simd::scalar::reduce_mean_variance(&test_data[i - period + 1], period);
        REQUIRE_THAT(var.line().data()[i], WithinRel(expected_var, 1e-12));
    }
}

TEST_CASE("Highest with SIMD matches scalar reference", "[simd][indicator]") {
    constexpr std::size_t period = 10;
    auto source = make_line(test_data);
    stratforge::Highest highest(source, period);
    run_all(source, highest);

    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        const double expected = stratforge::simd::scalar::reduce_max(&test_data[i - period + 1], period);
        REQUIRE(highest.line().data()[i] == expected);
    }
}

TEST_CASE("Lowest with SIMD matches scalar reference", "[simd][indicator]") {
    constexpr std::size_t period = 10;
    auto source = make_line(test_data);
    stratforge::Lowest lowest(source, period);
    run_all(source, lowest);

    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        const double expected = stratforge::simd::scalar::reduce_min(&test_data[i - period + 1], period);
        REQUIRE(lowest.line().data()[i] == expected);
    }
}

TEST_CASE("SumN matches scalar reduce_sum reference", "[indicator]") {
    constexpr std::size_t period = 10;
    auto source = make_line(test_data);
    stratforge::SumN sumn(source, period);
    run_all(source, sumn);

    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        const double expected = stratforge::simd::scalar::reduce_sum(&test_data[i - period + 1], period);
        REQUIRE_THAT(sumn.line().data()[i], WithinRel(expected, 1e-12));
    }
}

TEST_CASE("RSI seed average produces valid output", "[indicator]") {
    auto source = make_line(test_data);
    stratforge::RSI rsi(source, 14);
    run_all(source, rsi);

    for (std::size_t i = rsi.minimum_period(); i < test_data.size(); ++i) {
        const double val = rsi.line().data()[i];
        REQUIRE_FALSE(std::isnan(val));
        REQUIRE(val >= 0.0);
        REQUIRE(val <= 100.0);
    }
}

TEST_CASE("Correlation with SIMD matches scalar reference", "[simd][indicator]") {
    constexpr std::size_t period = 10;
    std::vector<double> data1;
    data1.reserve(test_data.size());
    for (const auto& v : test_data) {
        data1.push_back(v * 1.5 + 10.0);
    }

    auto source0 = make_line(test_data);
    auto source1 = make_line(data1);
    stratforge::Correlation corr(source0, source1, period);

    source0.home();
    source1.home();
    for (std::size_t i = 0; i < test_data.size(); ++i) {
        corr.next();
        if (i + 1 < test_data.size()) {
            source0.advance();
            source1.advance();
        }
    }

    // Perfectly linear relationship => correlation ~1.0
    for (std::size_t i = period - 1; i < test_data.size(); ++i) {
        REQUIRE_THAT(corr.line().data()[i], WithinAbs(1.0, 1e-10));
    }
}
