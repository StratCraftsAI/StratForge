# Contributing to StratForge

## Current Policy

Bug fixes, test improvements, documentation improvements, and performance work are welcome.

Feature PRs should start with an issue or discussion first so API shape, scope, and maintenance cost can be reviewed before implementation.

## What to Submit

### Bug Reports

Open an issue with:

- Compiler, standard library, and OS details
- A minimal reproduction case
- Expected behavior versus actual behavior
- Whether the issue appears in Release builds, Debug builds, or both

### Bug Fix PRs

1. Fork the repository and branch from `main`
2. Add or update a test that reproduces the issue
3. Keep the patch narrowly scoped to the bug being fixed
4. Reference the related issue in the PR description

### Performance PRs

1. Include before/after benchmark data
2. State the dataset and executable used
3. Call out any tradeoff in readability, portability, or API behavior
4. Avoid landing speculative micro-optimizations without measured benefit

### Feature PRs

Open an issue first for:

- New indicators
- New analyzers or broker behaviors
- Public API changes
- New dependencies or build-system changes

## Build and Test

### Requirements

- C++23 compiler: GCC 13+, Clang 17+, or MSVC 2022 17.10+
- CMake 3.25+

### Recommended Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Optional Benchmark Validation

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSF_BUILD_BENCHMARKS=ON
cmake --build build --target indicator_benchmarks strategy_benchmarks optimization_benchmarks memory_benchmarks
./build/bin/benchmarks/indicator_benchmarks
./build/bin/benchmarks/memory_benchmarks
```

Performance-sensitive changes should include benchmark results or an explanation for why benchmark coverage does not apply.

## Code Standards

All contributions should follow these standards:

- **C++23 only** with no compiler-specific extensions required for correctness
- **Header-first architecture** for public library code under `include/stratforge/`
- **Warning-clean builds** under the project's configured warning levels
- **No raw `new`/`delete`** in library code unless there is a justified low-level reason
- **No `using namespace` in headers**
- **No C-style casts**
- **No `std::endl`**; use `'\n'`
- **Prefer `constexpr` and strong typing** over macros and weakly typed flags

### Hot Path Rules

- No per-bar heap allocation in critical execution paths
- No unnecessary `std::string` construction on bar-processing paths
- Avoid exceptions in hot-path logic
- Document any intentional tradeoff against the zero-allocation target

## Validation Expectations

StratForge relies on golden-reference tests and reproducible benchmark harnesses. Changes should preserve that discipline.

- Indicator changes should update or extend golden tests when behavior changes intentionally
- Broker, analyzer, replay, and resample changes should include end-to-end validation where possible
- Public examples should compile and remain representative of best-practice usage

## License

By contributing, you agree that your contributions will be licensed under the [Apache License 2.0](LICENSE).
