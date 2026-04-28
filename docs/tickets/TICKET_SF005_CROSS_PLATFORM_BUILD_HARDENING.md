# TICKET_SF005: Cross-Platform Build Hardening

**Category**: CI/CD / Build System / DevOps
**Priority**: High
**Status**: In Progress
**Created**: 2026-04-28
**Reference**: upstream CI/CD patterns (cross-pollination)

---

## Summary

Harden StratForge cross-platform build infrastructure by porting proven patterns from upstream. While StratForge already has a 4-compiler CI matrix (GCC 14, Clang 18, Apple Clang, MSVC) and cross-platform release packaging, several upstream capabilities are missing.

## Gap Analysis

| Feature | upstream | StratForge (Before) | Action |
|---------|----------|---------------------|--------|
| SIMD cross-platform CMake option | `NFX_ENABLE_SIMD` with `/arch:AVX2` vs `-mavx2` | Missing | **Add** |
| Coverage CMake option | `NFX_ENABLE_COVERAGE` | Raw flags in CI only | **Add** |
| Codecov upload | codecov-action | Missing | **Add** |
| Performance regression gate | `check_perf_regression.sh` + baselines.json | Missing | **Port** |
| Release helper script | `release.sh` (semver bump + tag) | Missing | **Port** |
| `fail-fast: false` | Yes | Already present | None |
| CI `-march=native` skip | `$CI` env check | Already present | None |
| Clang libc++ on Linux | Already present | Already present | None |
| MSVC `/permissive-` | Already present | Already present | None |

## Deliverables

### Phase 1: CMake Options
1. **`SF_ENABLE_SIMD`** — SIMD compilation flags per platform
   - MSVC: `/arch:AVX2`
   - GCC/Clang: `-mavx2`
   - Default: OFF (header-only lib, consumer controls arch)
2. **`SF_ENABLE_COVERAGE`** — Coverage instrumentation via CMake
   - `--coverage -fprofile-arcs -ftest-coverage -fno-inline` (GCC/Clang)
   - Replaces raw `-DCMAKE_CXX_FLAGS` in CI

### Phase 2: CI Enhancements
3. **Codecov integration** — Upload coverage XML from CI coverage job
4. **CI coverage job** uses `SF_ENABLE_COVERAGE=ON` instead of raw flags

### Phase 3: Scripts
5. **`scripts/check_perf_regression.sh`** — Performance regression gate
   - Compares benchmark JSON output against `benchmarks/baselines/baselines.json`
   - Exits non-zero if P50/P99 exceed thresholds
6. **`scripts/release.sh`** — Release helper
   - Semver bump (major/minor/patch)
   - Updates `CMakeLists.txt` version
   - Creates annotated git tag
   - `--dry-run` and `--no-tag` options

## Acceptance Criteria

- [ ] `SF_ENABLE_SIMD` compiles correctly on all 4 CI matrix configurations
- [ ] `SF_ENABLE_COVERAGE` produces `.gcda`/`.gcno` files; gcovr generates report
- [ ] Codecov action uploads coverage data in CI
- [ ] `scripts/check_perf_regression.sh` passes locally against baselines
- [ ] `scripts/release.sh --dry-run` shows correct version bump analysis
- [ ] All existing CI jobs still pass (no regression)
