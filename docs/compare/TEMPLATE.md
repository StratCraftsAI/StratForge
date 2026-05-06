# Optimization Report: <Title>

**Date**: YYYY-MM-DD
**Ticket**: TICKET_XXX
**Author**: <name>

## Summary

<1-2 sentence description of what was optimized and why>

## Environment

| Property | Value |
|----------|-------|
| Compiler | GCC XX.X |
| CPU | <model> |
| OS | <distro + kernel> |
| Build | Release, <flags> |

## Before

| Metric | Value |
|--------|-------|
| P50 latency | XX ns |
| P99 latency | XX ns |
| Throughput | XX M ops/sec |
| Binary size | XX KB |

## Change

<Brief description of code changes, LOC delta, key design decisions>

## After

| Metric | Value |
|--------|-------|
| P50 latency | XX ns |
| P99 latency | XX ns |
| Throughput | XX M ops/sec |
| Binary size | XX KB |

## Delta

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| P50 | XX ns | XX ns | -XX% |
| P99 | XX ns | XX ns | -XX% |
| Throughput | XX M/s | XX M/s | +XX% |
| Binary size | XX KB | XX KB | +XX KB |

## Tradeoffs

- **Compile time**: +X seconds
- **Code complexity**: +X LOC
- **Portability**: <any platform restrictions>
- **Maintainability**: <impact on contributor experience>

## Conclusion

<Was it worth it? Under what conditions would we revert?>
