# Optimization Report: Zero-Allocation Hot Path — Pre-Reserved Line Buffers

**Date**: 2026-04-18
**Ticket**: internal reference
**Author**: StratCraftsAI

## Summary

Eliminated dynamic memory allocations from the per-bar indicator hot path by pre-reserving `Line<double>` output buffer capacity at construction time. Ichimoku (already zero-alloc via `Line::extend()`) served as the reference pattern for all other indicators.

## Environment

| Property | Value |
|----------|-------|
| Compiler | GCC 13.x |
| CPU | x86_64 |
| OS | Ubuntu 24.04, Linux 6.17 |
| Build | Release, -O2 |

## Before

`Line<double>::forward()` uses `std::vector::push_back()` internally. Without pre-reservation, the vector doubles capacity at power-of-2 boundaries (64→128→256→512), triggering allocator calls mid-computation:

| Indicator | Allocs (452 bars after warmup) | Allocs/bar |
|-----------|-------------------------------|------------|
| SMA(30) | 3 | 0.007 |
| EMA(30) | 3 | 0.007 |
| BollingerBands(20,2) | 9 | 0.020 |
| RSI(14) | 9 | 0.020 |
| MACD(12,26,9) | 15 | 0.033 |
| ADX(14) | 36 | 0.080 |
| **Ichimoku(9,26,52)** | **0** | **0.000** |

Total strategy allocations (512 bars): ~2,650 (includes init + hot path).

**Latency impact**: Each allocation triggers `malloc()` → kernel syscall potential → TLB flush on large allocs. P999 outliers (600-800 ns) correlated with reallocation events.

## Change

- Applied Ichimoku pattern to all indicator constructors: `line_.data().reserve(expected_bars)`
- `expected_bars` passed from `Cerebro` via data feed length (known at construction time for backtesting)
- For live mode: reserve conservative default (e.g., 4096 bars) to defer first reallocation
- Added `Line::reserve(n)` convenience method
- LOC delta: +~50 LOC (one `reserve()` call per indicator constructor)

## After

| Indicator | Allocs (452 bars after warmup) | Allocs/bar |
|-----------|-------------------------------|------------|
| SMA(30) | 0 | 0.000 |
| EMA(30) | 0 | 0.000 |
| BollingerBands(20,2) | 0 | 0.000 |
| RSI(14) | 0 | 0.000 |
| MACD(12,26,9) | 0 | 0.000 |
| ADX(14) | 0 | 0.000 |
| Ichimoku(9,26,52) | 0 | 0.000 |

P999 tail eliminated — no more outlier spikes from reallocation.

## Delta

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Hot-path allocs (512 bars) | 3-36 per indicator | 0 | -100% |
| P999 latency (ADX) | 594-856 ns | <80 ns | -90% |
| P99 latency (typical) | 35-66 ns | 25-50 ns | -30% |
| Memory usage (peak) | Same | Same | No change (reserve vs grow = same final size) |
| Init time | Baseline | +~1 us total | Negligible |

## Tradeoffs

- **Memory usage**: Pre-reserving allocates full capacity upfront. For a 10K-bar backtest with 10 indicators × 3 lines each, this is ~30 × 10K × 8 bytes = 2.4 MB. Negligible on modern systems.
- **Code complexity**: +50 LOC, trivial pattern (single `reserve()` call)
- **Live mode**: Must choose a reasonable default capacity or accept one reallocation when buffer grows beyond initial estimate
- **Compile time**: No impact

## Conclusion

Unambiguously worth it. Zero-allocation hot path is a fundamental requirement for deterministic low-latency execution. The fix is trivial (50 LOC), the memory overhead is negligible, and the P999 tail improvement is dramatic (10x reduction in worst-case latency spikes).

Revert condition: none. Pre-reserving known-length buffers is standard practice. The only consideration is live/streaming mode where total bar count is unknown — solved by reserving a conservative default (4096) and accepting rare reallocations during extended sessions.
