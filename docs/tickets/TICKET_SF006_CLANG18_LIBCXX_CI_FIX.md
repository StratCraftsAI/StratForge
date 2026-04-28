# TICKET_SF006: Fix Cross-Platform CI Build Failures (Clang 18 / MSVC)

**Category**: CI/CD / Build Correctness / Cross-Platform
**Status**: Done
**Created**: 2026-04-28
**Resolved**: 2026-04-28
**Priority**: High (blocks CI green on Clang 18 + MSVC)

## Problem

GitHub Actions CI fails on **Linux Clang 18** (`-stdlib=libc++ -Werror`) and **Windows MSVC** (`/W4 /WX`). Five compilation errors:

### Error 1: `[[nodiscard]]` return value discarded — `data_feed.hpp:51` (Clang 18)

`load()` returns `bool` (declared `[[nodiscard]]`). Inside `load_with_error()`, the return value is intentionally discarded. Fix: explicit `(void)` cast.

### Error 2: `std::from_chars` for `double` deleted in libc++ 18 — `csv_data.hpp:267`

libc++ 18 does **not** implement `std::from_chars` for floating-point types. Fix: use `std::strtod` fallback.

### Error 3: `std::jthread` not available in libc++ 18 — `optimizer.hpp:143`

libc++ 18 does **not** implement `std::jthread`. Fix: use `std::thread` + explicit `join()`.

### Error 4: Unused private fields `high_`/`low_` — `candlestick.hpp` (Clang 18)

10 candlestick classes stored `high_`/`low_` references but only used `open_`/`close_`. Clang `-Wunused-private-field` flags this; GCC does not. Fix: remove unused members, keep constructor signatures.

### Error 5: MSVC C4324 padding warning — `data_feed.hpp:174` (MSVC)

`alignas(64)` cache-line alignment triggers C4324 under `/WX`. Fix: `#pragma warning(disable: 4324)` under `_MSC_VER`.

## Fix

All fixes applied upstream in nonabackTrader, synced via `tools/sync_to_stratforge.sh`.

| Commit | Description |
|--------|-------------|
| `c1db22c` | Fix 1 + Fix 2: nodiscard cast, strtod fallback |
| `f8bf3ba` | Fix 3: std::thread replacement |
| `c8fcb94` | Fix 4: remove unused candlestick members |
| `194ef15` | Fix 5: suppress MSVC C4324 |
