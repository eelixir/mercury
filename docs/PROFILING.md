# Mercury Profiling & Benchmarking Guide

This guide covers performance analysis tools and techniques used in Mercury.

## Quick Start

### Build with Sanitizers (Recommended for Development)

```bash
# AddressSanitizer - catches memory errors
cmake -B build -DMERCURY_ENABLE_ASAN=ON
cmake --build build

# UndefinedBehaviorSanitizer - catches undefined behavior
cmake -B build -DMERCURY_ENABLE_UBSAN=ON
cmake --build build

# Both sanitizers together
cmake -B build -DMERCURY_ENABLE_ASAN=ON -DMERCURY_ENABLE_UBSAN=ON
cmake --build build
```

### Build with Benchmarks

```bash
cmake -B build -DMERCURY_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run benchmarks
./build/bin/mercury_benchmarks
```

### Build with Profiling Instrumentation

```bash
cmake -B build -DMERCURY_ENABLE_PROFILING=ON
cmake --build build
```

---

## Sanitizers

### AddressSanitizer (ASan)
Detects:
- Heap/stack/global buffer overflow
- Use-after-free, use-after-return
- Double-free, invalid free
- Memory leaks

**Runtime overhead:** ~2x slower

### UndefinedBehaviorSanitizer (UBSan)
Detects:
- Signed integer overflow
- Null pointer dereference
- Invalid shift operations
- Out-of-bounds array access

**Runtime overhead:** Minimal (~10-20%)

### Example: Running Tests with Sanitizers

```bash
# Build with sanitizers
cmake -B build-asan -DMERCURY_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan

# Run tests - any memory errors will be reported
./build-asan/bin/mercury_tests
```

---

## Google Benchmark

Industry-standard micro-benchmarking framework. Results include:
- Mean execution time
- CPU time
- Iterations per second
- Custom counters

### Benchmark Output Example (Release Build)

```
Running ./mercury_benchmarks
----------------------------------------------------------------------------------------------------------
Benchmark                                                Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------------
BM_LimitOrderInsert                                    321 ns          318 ns      2357895 items_per_second=3.14M/s
BM_LimitOrderMatch/10                                 1988 ns         1659 ns       414357 items_per_second=602k/s
BM_MarketOrderSweep/5                                 1.83 us         1.34 us       896000 levels=5
BM_OrderCancel/100                                    2724 ns         2384 ns       275314 items_per_second=419k/s
BM_SustainedThroughput                                0.37 us         0.31 us       100000 items_per_second=3.2M/s
BM_RealisticMix                                       0.29 ms         0.27 ms         2240 items_per_second=3.77M/s
```

### Benchmark Targets

| Benchmark | What it measures |
|-----------|------------------|
| `BM_LimitOrderInsert` | Raw order insertion latency |
| `BM_LimitOrderMatch` | Matching latency by book depth |
| `BM_MarketOrderSweep` | Market order sweeping N levels |
| `BM_OrderCancel` | Cancel latency by book size |
| `BM_SustainedThroughput` | Steady-state orders/second |
| `BM_RealisticMix` | Mixed workload simulation |

---

## Custom Data Structures

Mercury uses custom data structures optimized for trading workloads:

### HashMap (Robin Hood Hashing)
- **Location:** `include/HashMap.h`
- **Use:** O(1) order ID lookups
- **Benefits:** Open addressing, power-of-2 sizing, cache-friendly probing

### IntrusiveList
- **Location:** `include/IntrusiveList.h`  
- **Use:** Order queues at each price level
- **Benefits:** O(1) removal from any position, no separate node allocation

### ObjectPool
- **Location:** `include/ObjectPool.h`
- **Use:** Pre-allocated order nodes
- **Benefits:** Eliminates malloc on hot path, reduces memory fragmentation

### PriceLevel
- **Location:** `include/PriceLevel.h`
- **Use:** Encapsulates orders at a price with cached aggregate quantity
- **Benefits:** No recomputation of level quantity on each query

---

## Built-in Profiler

Mercury includes a lightweight instrumentation profiler for measuring critical path latency.

### Usage in Code

```cpp
#include "Profiler.h"

void processOrder(Order& order) {
    MERCURY_PROFILE_FUNCTION();  // Auto-profile this function
    
    // Or manual scoped timing:
    MERCURY_PROFILE_SCOPE("order_matching");
    matchOrder(order);
}

// Print statistics at the end
Mercury::Profiler::instance().printAll();
```

### Output Example

```
╔═══════════════════════════════════════════════════════════╗
║ order_matching                                            ║
╠═══════════════════════════════════════════════════════════╣
║ Samples: 50000                                            ║
╠═══════════════════════════════════════════════════════════╣
║ Min:         123 ns  │  Mean:        456.78 ns            ║
║ Max:        9876 ns  │  Stdev:       234.56 ns            ║
╠═══════════════════════════════════════════════════════════╣
║ Percentiles:                                              ║
║   p50:       412 ns  (   0.41 µs)                         ║
║   p90:       678 ns  (   0.68 µs)                         ║
║   p99:      1234 ns  (   1.23 µs)                         ║
║   p999:     5678 ns  (   5.68 µs)                         ║
╚═══════════════════════════════════════════════════════════╝
```

---

## Linux-Specific Tools (via WSL2)

For more advanced profiling, use WSL2:

### perf (Linux Kernel Profiler)

```bash
# Record CPU samples
perf record -g ./mercury data/sample_orders.csv

# View report
perf report

# Generate flame graph
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

### Valgrind (Memory Analysis)

```bash
# Memory leak check
valgrind --leak-check=full ./mercury data/sample_orders.csv

# Cache profiling
valgrind --tool=cachegrind ./mercury data/sample_orders.csv
```

---

## Performance Targets

For a trading engine, aim for:

| Metric | Target | Current |
|--------|--------|--------|
| Order insertion | < 1 µs | ✅ 321 ns |
| Order matching | < 5 µs | ✅ 1.9 µs (10 levels) |
| Throughput | > 100k orders/sec | ✅ 3.2M orders/sec |
| p99 latency | < 10 µs | ✅ See profiler |

> **Note:** Results from Release build on 12-core CPU @ 3.6GHz. Debug builds are ~3-5x slower.

---

## CI Integration

Add benchmarks to your CI pipeline:

```yaml
# .github/workflows/benchmark.yml
- name: Build Benchmarks
  run: |
    cmake -B build -DMERCURY_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build

- name: Run Benchmarks
  run: ./build/bin/mercury_benchmarks --benchmark_format=json > benchmark_results.json
```
