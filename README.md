# <img src="https://github.com/user-attachments/assets/7ee41ddf-cf24-42fb-953b-d44c55e9f352" width="400">

> High-performance C++ order book and matching engine built from scratch with custom data structures.

## Overview

Mercury is a low-latency trading engine implementing a full limit order book with price-time priority matching. Designed to demonstrate systems programming concepts used in quantitative finance: cache-friendly data structures, memory pooling, and microsecond-level performance optimization.

**Key Metrics:**
- **3.2M+ orders/sec** sustained throughput
- **~320 ns** average order insertion latency
- **O(1)** order lookup, insertion, and cancellation

## Features

- **Order Types:** Limit, Market, Cancel, Modify
- **Time-in-Force:** GTC (Good-til-Canceled), IOC (Immediate-or-Cancel), FOK (Fill-or-Kill)
- **Price-Time Priority:** FIFO matching at each price level
- **Self-Trade Prevention:** Optional client ID based filtering
- **Risk Management:** Pre-trade risk checks with position/exposure limits
- **Trade Logging:** CSV output for all executions and risk events
- **Comprehensive Validation:** Detailed rejection reasons for invalid orders

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        MatchingEngine                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Order       │  │ Trade       │  │ Execution               │  │
│  │ Validation  │─▶│ Matching    │─▶│ Callbacks               │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                        RiskManager                               │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────┐  │
│  │ Position Limits  │  │ Exposure Limits  │  │ Order Limits  │  │
│  │ (per client)     │  │ (gross/net)      │  │ (qty/value)   │  │
│  └──────────────────┘  └──────────────────┘  └───────────────┘  │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                         OrderBook                                │
│  ┌──────────────────────┐    ┌──────────────────────────┐       │
│  │ Bids (price desc)    │    │ Asks (price asc)         │       │
│  │ std::map<PriceLevel> │    │ std::map<PriceLevel>     │       │
│  └──────────┬───────────┘    └───────────┬──────────────┘       │
│             │                            │                       │
│  ┌──────────▼────────────────────────────▼──────────────┐       │
│  │              PriceLevel (IntrusiveList)              │       │
│  │  OrderNode ←→ OrderNode ←→ OrderNode ←→ ...          │       │
│  └──────────────────────────────────────────────────────┘       │
│                              │                                   │
│  ┌───────────────────────────▼──────────────────────────┐       │
│  │ HashMap<OrderID, Location>  │  ObjectPool<OrderNode> │       │
│  │ O(1) lookup (Robin Hood)    │  Pre-allocated memory  │       │
│  └──────────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────┘
```

### Custom Data Structures

| Component | Purpose | Complexity |
|-----------|---------|------------|
| **HashMap** | Order ID → location lookup | O(1) avg |
| **IntrusiveList** | Order queue at each price level | O(1) insert/remove |
| **ObjectPool** | Pre-allocated order nodes | O(1) alloc/free |
| **PriceLevel** | Orders + cached aggregate quantity | O(1) quantity query |

## Project Structure

```
mercury/
├── include/           # Header files
│   ├── Order.h        # Order, Trade, ExecutionResult types
│   ├── OrderBook.h    # Order book with custom data structures
│   ├── OrderNode.h    # Intrusive list node for orders
│   ├── PriceLevel.h   # Price level with order queue
│   ├── MatchingEngine.h
│   ├── RiskManager.h  # Pre-trade risk checks
│   ├── HashMap.h      # Robin Hood hash map
│   ├── IntrusiveList.h
│   ├── ObjectPool.h   # Memory pool allocator
│   ├── Profiler.h     # Latency instrumentation
│   ├── CSVParser.h
│   └── TradeWriter.h
├── src/               # Implementation files
├── tests/             # Google Test suites
├── benchmarks/        # Google Benchmark micro-benchmarks
├── data/              # Sample order datasets
└── docs/              # Additional documentation
```

## Building & Running

### Prerequisites
- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.10+

### Build

```bash
# Standard build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With sanitizers (development)
cmake -B build -DMERCURY_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# With benchmarks
cmake -B build -DMERCURY_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run

```bash
# Process orders from CSV
./build/mercury data/sample_orders.csv trades.csv executions.csv riskevents.csv

# Run interactive demo
./build/mercury
```

### Example Output

```
========================================
   Mercury File I/O Mode
========================================
Input:       data/sample_orders.csv
Trades:      trades.csv
Executions:  executions.csv
Risk Events: riskevents.csv
========================================

Processing Complete
----------------------------------------
Time elapsed: 0.42 ms
Throughput: 119047 orders/sec

Risk Manager Statistics:
  Risk Checks:  50
  Approved:     48
  Risk Rejected: 2

Total Trades: 37
Total Volume: 1903 units
```

## Performance

Benchmarks run on 12-core CPU @ 3.6GHz (Release build):

| Operation | Latency | Throughput |
|-----------|---------|------------|
| Order Insert | 321 ns | 3.1M/sec |
| Order Match (10 levels) | 1.9 µs | 526K/sec |
| Order Cancel | 2.7 µs | 370K/sec |
| Market Sweep (5 levels) | 1.8 µs | 556K/sec |
| **Sustained Mixed Load** | 312 ns | **3.2M/sec** |

### Run Benchmarks

```bash
cmake -B build -DMERCURY_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mercury_benchmarks
```

## Testing

165+ unit tests covering:
- Order book operations (insert, remove, update)
- Matching engine (limit, market, IOC, FOK)
- Risk manager (position limits, exposure limits, order limits)
- Edge cases (partial fills, empty book, invalid orders)
- Stress tests (100K+ orders, deep books)
- Data structure correctness (HashMap, IntrusiveList)

```bash
# Run all tests
./build/mercury_tests

# Run specific test suite
./build/mercury_tests --gtest_filter="MatchingEngineTest.*"

# Run with verbose output
./build/mercury_tests --gtest_filter="StressTest.*" --gtest_print_time=1
```

## Roadmap

- [x] Order book with std::map prototype
- [x] CSV parsing and file I/O
- [x] Matching engine (limit/market/cancel/modify)
- [x] Google Test unit tests + stress tests
- [x] Custom data structures (HashMap, IntrusiveList, ObjectPool)
- [x] Profiling infrastructure (sanitizers, Google Benchmark)
- [x] Cache-friendly design with memory pre-allocation
- [x] Risk manager (position limits, exposure checks)
- [ ] PnL module (realized + unrealized)
- [ ] Multithreading/concurrency
- [ ] Strategy layer (market making, momentum)
- [ ] Backtesting framework

## License

AGPL-3.0