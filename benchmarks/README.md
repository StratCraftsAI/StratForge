# StratForge Benchmark Suite

Performance benchmarks for the StratForge engine with proper methodology:
warmup phases, I/O-isolated timing, per-bar micro-benchmarks, allocation
counting, and machine-parseable JSON output.

## Quick Start

```bash
# Build all benchmarks (Release mode required for meaningful results)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target gen_bench_data indicator_benchmarks \
    strategy_benchmarks optimization_benchmarks memory_benchmarks

# Generate synthetic 100K-bar dataset
./build/bin/benchmarks/gen_bench_data

# Run all benchmarks
./build/bin/benchmarks/indicator_benchmarks
./build/bin/benchmarks/strategy_benchmarks
./build/bin/benchmarks/optimization_benchmarks
./build/bin/benchmarks/memory_benchmarks
```

## Benchmark Executables

### `gen_bench_data`

Generates a synthetic 100K-bar OHLCV CSV using geometric Brownian motion.
Deterministic (fixed seed=42) for reproducible benchmarks.

```bash
# Default: 100K bars
./build/bin/benchmarks/gen_bench_data

# Custom bar count and output directory
./build/bin/benchmarks/gen_bench_data 500000 build/bench_data
```

### `indicator_benchmarks`

Benchmarks 11 indicators with three measurement methods:

1. **Whole-run**: Total time for all bars (50 iterations, 3 warmup)
2. **Per-bar micro-benchmark**: Individual `next()` call timing (50-bar warmup discarded)
3. **Allocation count**: Heap allocations in hot path (60-bar warmup discarded)

Tests on both 512-bar golden extract and 100K-bar synthetic datasets.

### `strategy_benchmarks`

Benchmarks composite strategy (5 indicators + broker):

- Cold/warm startup comparison
- Whole-run timing (I/O isolated — data loaded once, cloned per iteration)
- Full backtest wall-clock (512 and 100K bars)
- Strategy-level allocation audit

### `optimization_benchmarks`

Benchmarks parallel parameter grid optimization (4 threads):

- 2×2, 3×3, 5×5 grids
- Reports combos/sec throughput

### `memory_benchmarks`

Dedicated allocation audit for all indicators and strategy:

- Counts heap allocations via global `operator new` override
- 60-bar warmup to exclude initial vector growth
- Verifies zero-alloc hot-path target per design spec

## Methodology

### Timing

- `std::chrono::steady_clock` for nanosecond-precision wall-clock
- **Warmup phase**: 3 whole-run warmup iterations discarded; 50-bar per-bar warmup
- **I/O isolation**: Data loaded once before timing loop; `clone()` + `load()` used per iteration (no disk I/O in hot path)
- Metrics: avg, P50, P99, P999, min, max

### Allocation Counting

- Global `operator new`/`operator delete` overrides with thread-local counter
- `AllocationCounter` RAII scope tracks allocations in marked regions
- 60-bar warmup absorbs initial `Line<double>` vector growth

### Output

- Human-readable to stdout
- JSON reports to `build/bench_results/` for regression tracking

## Baseline Results

See [BASELINE.md](BASELINE.md) for the full baseline report.

**Highlights (512-bar, per-bar P50)**:
- EMA: 16 ns/bar (58M bars/sec)
- SMA: 29 ns/bar (33M bars/sec)
- MACD: 22 ns/bar (42M bars/sec)
- Ichimoku: 67 ns/bar (15M bars/sec) — only zero-alloc indicator
- All indicators meet <100ns P50 target

## Performance Targets

| Target | Metric | Status |
|--------|--------|--------|
| Per-indicator computation | <100 ns/bar P50 | **MET** (17-67 ns) |
| Per-bar composite strategy | <100 ns/bar | NOT MET (~1.2μs, includes broker) |
| Hot-path allocations | 0 allocations | NOT MET (vector growth — see BASELINE.md) |
| Parallel optimization | Linear speedup | **MET** (~4x on 4 threads) |

## Performance Regression Tracking

Automated regression detection via `tools/bench_regression.py`. Compares current
`build/bench_results/*.json` against stored baselines in `benchmarks/baselines/`.

```bash
# Seed baselines from current results
python3 tools/bench_regression.py --update-baseline

# Compare current results against baselines (exit 1 on regression)
python3 tools/bench_regression.py

# Run benchmarks first, then compare
python3 tools/bench_regression.py --run

# Custom thresholds
python3 tools/bench_regression.py --latency-threshold 0.20 --p99-threshold 0.25

# Single suite
python3 tools/bench_regression.py --suite indicator_benchmarks
```

**Default thresholds**:

| Metric | Threshold | Direction |
|--------|-----------|-----------|
| `p50_ns` | +15% | Higher is worse |
| `p99_ns` | +20% | Higher is worse |
| `avg_ns` | +15% | Higher is worse |
| `alloc_count` | +0 (exact) | Any increase is regression |

## Datasets

| Dataset | Bars | Source |
|---------|------|--------|
| 512-bar daily | 512 | `tools/golden_extract/datas/2005-2006-day-001.txt` |
| 100K synthetic | 100,000 | `build/bench_data/synthetic_100k.csv` (generated) |
