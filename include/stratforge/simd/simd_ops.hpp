#pragma once

/// @file simd_ops.hpp
/// Portable SIMD primitives for indicator computation.
///
/// When SF_HAS_XSIMD is defined, uses xsimd with **runtime dispatch**:
/// the best available ISA (AVX-512 > AVX2 > SSE > NEON > scalar) is
/// selected at first call via CPUID.  When SF_HAS_XSIMD is not defined,
/// a plain scalar fallback is compiled instead.
///
/// Floating-point note: SIMD horizontal reductions may accumulate in a
/// different order than left-to-right scalar code.  For min/max this is
/// exact.  For sum and variance the results are equivalent within ULP
/// tolerance (see  Golden Master TDD).

#include <cmath>
#include <cstddef>
#include <utility>

#if defined(SF_HAS_XSIMD) && SF_HAS_XSIMD
#include <xsimd/xsimd.hpp>
#endif

namespace stratforge::simd {

// ============================================================================
// Scalar implementations — always available, also used for benchmarking
// ============================================================================

namespace scalar {

[[nodiscard]] inline double reduce_sum(const double* data, std::size_t count) noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += data[i];
    }
    return sum;
}

[[nodiscard]] inline double reduce_min(const double* data, std::size_t count) noexcept {
    if (count == 0) [[unlikely]] { return 0.0; }
    double result = data[0];
    for (std::size_t i = 1; i < count; ++i) {
        if (data[i] < result) { result = data[i]; }
    }
    return result;
}

[[nodiscard]] inline double reduce_max(const double* data, std::size_t count) noexcept {
    if (count == 0) [[unlikely]] { return 0.0; }
    double result = data[0];
    for (std::size_t i = 1; i < count; ++i) {
        if (data[i] > result) { result = data[i]; }
    }
    return result;
}

[[nodiscard]] inline std::pair<double, double> reduce_mean_variance(const double* data, std::size_t count) noexcept {
    const double mean = reduce_sum(data, count) / static_cast<double>(count);

    double variance_sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double delta = data[i] - mean;
        variance_sum += delta * delta;
    }

    return {mean, variance_sum / static_cast<double>(count)};
}

} // namespace scalar

// ============================================================================
// SIMD implementations with runtime dispatch via xsimd
// ============================================================================

#if defined(SF_HAS_XSIMD) && SF_HAS_XSIMD

namespace detail {

/// Arch-templated SIMD reduction: sum
///
/// Scalar tail safety invariant:
///   vec_end = count - (count % bs)
///   - When count >= bs: vec_end is the largest multiple of bs <= count.
///     SIMD processes [0, vec_end), scalar tail processes [vec_end, count).
///   - When count < bs:  vec_end = 0, SIMD loop is skipped entirely,
///     scalar tail processes all elements.
///   - load_unaligned(&data[i]) reads exactly bs elements starting at i.
///     The guard i < vec_end ensures i + bs <= count, so no out-of-bounds read.
struct reduce_sum_fn {
    template <typename Arch>
    double operator()(Arch, const double* data, std::size_t count) const noexcept {
        using batch_t = xsimd::batch<double, Arch>;
        constexpr std::size_t bs = batch_t::size;

        batch_t acc(0.0);
        std::size_t i = 0;
        const std::size_t vec_end = count - (count % bs);

        for (; i < vec_end; i += bs) {
            acc += batch_t::load_unaligned(&data[i]);
        }

        double result = xsimd::reduce_add(acc);
        for (; i < count; ++i) { result += data[i]; }
        return result;
    }
};

/// Arch-templated SIMD reduction: min
/// See reduce_sum_fn for scalar tail safety invariant documentation.
/// Additional guard: count >= bs check prevents SIMD path for small inputs.
struct reduce_min_fn {
    template <typename Arch>
    double operator()(Arch, const double* data, std::size_t count) const noexcept {
        using batch_t = xsimd::batch<double, Arch>;
        constexpr std::size_t bs = batch_t::size;

        if (count == 0) [[unlikely]] { return 0.0; }

        std::size_t i = 0;
        double result;

        if (count >= bs) {
            batch_t vmin = batch_t::load_unaligned(&data[0]);
            i = bs;
            const std::size_t vec_end = count - (count % bs);
            for (; i < vec_end; i += bs) {
                vmin = xsimd::min(vmin, batch_t::load_unaligned(&data[i]));
            }
            result = xsimd::reduce_min(vmin);
        } else {
            result = data[0];
            i = 1;
        }

        for (; i < count; ++i) {
            if (data[i] < result) { result = data[i]; }
        }
        return result;
    }
};

/// Arch-templated SIMD reduction: max
/// See reduce_sum_fn for scalar tail safety invariant documentation.
struct reduce_max_fn {
    template <typename Arch>
    double operator()(Arch, const double* data, std::size_t count) const noexcept {
        using batch_t = xsimd::batch<double, Arch>;
        constexpr std::size_t bs = batch_t::size;

        if (count == 0) [[unlikely]] { return 0.0; }

        std::size_t i = 0;
        double result;

        if (count >= bs) {
            batch_t vmax = batch_t::load_unaligned(&data[0]);
            i = bs;
            const std::size_t vec_end = count - (count % bs);
            for (; i < vec_end; i += bs) {
                vmax = xsimd::max(vmax, batch_t::load_unaligned(&data[i]));
            }
            result = xsimd::reduce_max(vmax);
        } else {
            result = data[0];
            i = 1;
        }

        for (; i < count; ++i) {
            if (data[i] > result) { result = data[i]; }
        }
        return result;
    }
};

/// Arch-templated SIMD reduction: mean + population variance
/// See reduce_sum_fn for scalar tail safety invariant documentation.
/// Precondition: count > 0 (division by count; callers must guard).
struct reduce_mean_variance_fn {
    template <typename Arch>
    std::pair<double, double> operator()(Arch arch, const double* data, std::size_t count) const noexcept {
        using batch_t = xsimd::batch<double, Arch>;
        constexpr std::size_t bs = batch_t::size;

        const double mean = reduce_sum_fn{}(arch, data, count) / static_cast<double>(count);

        batch_t var_acc(0.0);
        const batch_t vmean(mean);
        std::size_t i = 0;
        const std::size_t vec_end = count - (count % bs);

        for (; i < vec_end; i += bs) {
            const batch_t delta = batch_t::load_unaligned(&data[i]) - vmean;
            var_acc += delta * delta;
        }

        double variance_sum = xsimd::reduce_add(var_acc);
        for (; i < count; ++i) {
            const double delta = data[i] - mean;
            variance_sum += delta * delta;
        }

        return {mean, variance_sum / static_cast<double>(count)};
    }
};

/// Arch-templated query: return ISA name string
struct arch_name_fn {
    template <typename Arch>
    const char* operator()(Arch) const noexcept { return Arch::name(); }
};

} // namespace detail

// --- Public API: runtime-dispatched SIMD primitives ------------------------
// Dispatchers are cached in static locals — CPUID + arch walk construction
// happens once on first call; subsequent calls only pay the branch cascade
// in walk_archs (well-predicted on hot paths).

[[nodiscard]] inline double reduce_sum(const double* data, std::size_t count) noexcept {
    static auto dispatcher = xsimd::dispatch(detail::reduce_sum_fn{});
    return dispatcher(data, count);
}

[[nodiscard]] inline double reduce_min(const double* data, std::size_t count) noexcept {
    static auto dispatcher = xsimd::dispatch(detail::reduce_min_fn{});
    return dispatcher(data, count);
}

[[nodiscard]] inline double reduce_max(const double* data, std::size_t count) noexcept {
    static auto dispatcher = xsimd::dispatch(detail::reduce_max_fn{});
    return dispatcher(data, count);
}

[[nodiscard]] inline std::pair<double, double> reduce_mean_variance(const double* data, std::size_t count) noexcept {
    static auto dispatcher = xsimd::dispatch(detail::reduce_mean_variance_fn{});
    return dispatcher(data, count);
}

/// Query the name of the ISA selected by runtime dispatch.
/// Returns e.g. "avx2", "avx512bw", "neon64", "sse2".
[[nodiscard]] inline const char* dispatched_arch_name() noexcept {
    static auto dispatcher = xsimd::dispatch(detail::arch_name_fn{});
    return dispatcher();
}

#else // Scalar fallback when xsimd is not available

[[nodiscard]] inline double reduce_sum(const double* data, std::size_t count) noexcept {
    return scalar::reduce_sum(data, count);
}

[[nodiscard]] inline double reduce_min(const double* data, std::size_t count) noexcept {
    return scalar::reduce_min(data, count);
}

[[nodiscard]] inline double reduce_max(const double* data, std::size_t count) noexcept {
    return scalar::reduce_max(data, count);
}

[[nodiscard]] inline std::pair<double, double> reduce_mean_variance(const double* data, std::size_t count) noexcept {
    return scalar::reduce_mean_variance(data, count);
}

[[nodiscard]] inline const char* dispatched_arch_name() noexcept {
    return "scalar (xsimd disabled)";
}

#endif // SF_HAS_XSIMD

} // namespace stratforge::simd
