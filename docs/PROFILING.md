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

### Benchmark Output Example

```
Running ./mercury_benchmarks
-------------------------------------------------------------
Benchmark                   Time             CPU   Iterations
-------------------------------------------------------------
BM_LimitOrderInsert       245 ns          244 ns      2867429
BM_LimitOrderMatch/10     892 ns          891 ns       784231
BM_MarketOrderSweep/5    2.34 us         2.33 us       300124
BM_OrderCancel/1000       312 ns          311 ns      2245891
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
|--------|--------|---------|
| Order insertion | < 1 µs | Run benchmarks |
| Order matching | < 5 µs | Run benchmarks |
| Throughput | > 100k orders/sec | Run benchmarks |
| p99 latency | < 10 µs | Run profiler |

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
