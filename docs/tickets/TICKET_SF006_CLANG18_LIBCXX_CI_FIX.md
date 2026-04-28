# TICKET_SF006: Fix Clang 18 / libc++ CI Build Failures

**Category**: CI/CD / Build Correctness / Cross-Platform
**Status**: Open
**Created**: 2026-04-28
**Priority**: High (blocks CI green on Clang 18)

## Problem

GitHub Actions CI fails on the **Linux Clang 18** job (`-stdlib=libc++ -Werror`). Three compilation errors due to incomplete libc++ 18 standard library coverage:

### Error 1: `[[nodiscard]]` return value discarded — `data_feed.hpp:51`

`load()` returns `bool` (declared `[[nodiscard]]`). Inside `load_with_error()`, the return value is intentionally discarded. Fix: explicit `(void)` cast.

### Error 2: `std::from_chars` for `double` deleted in libc++ 18 — `csv_data.hpp:267`

libc++ 18 does **not** implement `std::from_chars` for floating-point types. Fix: use `std::strtod` fallback.

### Error 3: `std::jthread` not available in libc++ 18 — `optimizer.hpp:143`

libc++ 18 does **not** implement `std::jthread`. Fix: use `std::thread` + explicit `join()`.

## Fix

Applied upstream in nonabackTrader, synced via `tools/sync_to_stratforge.sh`.
