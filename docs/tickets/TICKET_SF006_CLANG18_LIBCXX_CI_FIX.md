# TICKET_SF006: Fix Cross-Platform CI Build Failures (Clang 18 / MSVC)

**Category**: CI/CD / Build Correctness / Cross-Platform
**Status**: Done
**Created**: 2026-04-28
**Resolved**: 2026-04-28
**Priority**: High (blocks CI green on Clang 18 + MSVC)

## Problem

GitHub Actions CI fails on **Linux Clang 18** (`-stdlib=libc++ -Werror`) and **Windows MSVC** (`/W4 /WX`). 12 distinct issues across compilation errors, warnings-as-errors, and runtime test failures.

### Phase 1: Build Errors (Clang 18 + MSVC)

| # | Error | File | Platform |
|---|-------|------|----------|
| 1 | `[[nodiscard]]` discarded | `data_feed.hpp:51` | Clang 18 |
| 2 | `std::from_chars<double>` deleted | `csv_data.hpp:267` | Clang 18 / libc++ |
| 3 | `std::jthread` missing | `optimizer.hpp:143` | Clang 18 / libc++ |
| 4 | Unused `high_`/`low_` private fields | `candlestick.hpp` (10 classes) | Clang 18 |
| 5 | C4324 padding warning | `data_feed.hpp:174` | MSVC |

### Phase 2: MSVC C4100 / C2065 / C4996

| # | Error | File | Description |
|---|-------|------|-------------|
| 6 | C4100 | `strategy.hpp`, `analyzer.hpp`, `observer.hpp` | Virtual base class unreferenced params |
| 7 | C4996 | `test_resample_golden.cpp:25` | `gmtime` deprecated |
| 8 | C4100 | `test_optstrategy.cpp`, `test_coverage_engine.cpp` | Lambda `params` unreferenced |
| 9 | C4100 | `candlestick.hpp` (10 patterns) | Constructor `high`/`low` unreferenced |
| 10 | C2065 | `linearreg.hpp:137` | `M_PI` undeclared |

### Phase 3: Windows Runtime Test Failures

| # | Error | Files | Description |
|---|-------|-------|-------------|
| 11 | `feed.load()` = false | 3 test files | `/tmp/` doesn't exist on Windows |
| 12 | SIMD tests unmatched | `test_simd_ops.cpp` | Em-dash `—` corrupted by CTest on Windows |

### Phase 4: Coverage & Sanitizer CI Failures

| # | Error | File | Job |
|---|-------|------|-----|
| 13 | gcovr crashes on Catch2 `.gcda` | `ci.yml` | Coverage |
| 14 | gcov version mismatch (exit code 3) | `ci.yml` | Coverage |
| 15 | gcov negative hit count ([gcc #68080](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68080)) | `ci.yml` | Coverage |
| 16 | Global `-Werror` leaks to Catch2 (false `-Wmaybe-uninitialized`) | `CMakeLists.txt` | ASan |
| 17 | LaguerreRSI test heap-buffer-overflow (`data()[5]` on 5-element vector) | `test_indicator_phase8.cpp` | ASan |
| 18 | Signed integer overflow in 100K row date generation (year 2293) | `test_data_robustness.cpp` | UBSan |

## Fix

All fixes applied upstream, synced via `tools/publish_stratforge.sh`.

| # | Fix |
|---|-----|
| 1 | `(void)load();` explicit cast |
| 2 | `std::strtod` fallback for libc++ |
| 3 | `std::thread` + explicit `join()` |
| 4 | Remove unused `high_`/`low_` private members |
| 5 | `#pragma warning(disable: 4324)` under `_MSC_VER` |
| 6 | `[[maybe_unused]]` on virtual method params |
| 7 | `gmtime_s` under `_MSC_VER` |
| 8 | `[[maybe_unused]]` on lambda params |
| 9 | `[[maybe_unused]]` on constructor `high`/`low` params |
| 10 | `std::numbers::pi` replaces `M_PI` |
| 11 | `std::filesystem::temp_directory_path()` replaces `/tmp/` |
| 12 | ASCII ` -` replaces em-dash `—` in TEST_CASE names |
| 13 | `--gcov-exclude-directories 'build/_deps'` skips Catch2 `.gcda` scanning |
| 14 | `--gcov-executable gcov-14` matches gcc-14 `.gcda` format |
| 15 | `--gcov-ignore-parse-errors=negative_hits.warn_once_per_file` tolerates gcc bug |
| 16 | Move `-Werror`/`/WX` from global `add_compile_options` to `target_compile_options` on test target only |
| 17 | Expand LaguerreRSI test source from 5 to 7 elements (need ≥6 for `period=6`) |
| 18 | Hourly increments (100K hours ≈ 11.4 years) instead of daily |

## Commits

| Commit | Description |
|--------|-------------|
| `c1db22c` | Fix 1 + Fix 2: nodiscard cast, strtod fallback |
| `f8bf3ba` | Fix 3: std::thread replacement |
| `c8fcb94` | Fix 4: remove unused candlestick members |
| `194ef15` | Fix 5: suppress MSVC C4324 |
| `22e5cc4` | Ticket docs update |
| `553a46c` | Fix 6 + Fix 7: virtual params + gmtime |
| `d973359` | Fix 8a: test_optstrategy lambda params |
| `8a71504` | Fix 9 + Fix 10: candlestick params + M_PI |
| `3b7469a` | Fix 8b: test_coverage_engine lambda params |
| `5b64869` | Fix 11 + Fix 12: temp paths + CTest Unicode |
| `03028f7` | Fix 13: gcov-exclude-directories for Catch2 .gcda |
| `ac0fcad` | Fix 14: gcov-executable gcov-14 |
| `3a89791` | Fix 15: gcov-ignore-parse-errors negative_hits |
| `852ece1` | Fix 16: move -Werror/WX to test target only |
| `8361787` | Fix 17: LaguerreRSI test OOB fix |
| `a01d40d` | Fix 18: UBSan 100K row date overflow |
