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
- **P&L Tracking:** Realized and unrealized P&L with FIFO cost basis
- **Trade Logging:** CSV output for all executions, risk events, and P&L snapshots
- **Comprehensive Validation:** Detailed rejection reasons for invalid orders
- **Concurrency Support:** Thread pool, parallel CSV parsing, async I/O writers

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
│                    ConcurrentMatchingEngine                      │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────┐  │
│  │ Symbol Sharding  │  │ Thread Pool      │  │ Async         │  │
│  │ (parallel books) │  │ (work stealing)  │  │ Callbacks     │  │
│  └──────────────────┘  └──────────────────┘  └───────────────┘  │
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
│                        PnLTracker                                │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────┐  │
│  │ Position Tracking│  │ Realized P&L     │  │ Unrealized    │  │
│  │ (FIFO method)    │  │ (closed trades)  │  │ (mark-to-mkt) │  │
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
| **ThreadPool** | Task scheduling for parallel work | O(1) submit |
| **AsyncWriter** | Background I/O with buffering | Lock-free fast path |
| **ConcurrentQueue** | Thread-safe producer/consumer | Lock-based |
| **SpinLock** | Short critical section protection | Wait-free fast path |

## Project Structure

```
mercury/
├── include/           # Header files
│   ├── Order.h        # Order, Trade, ExecutionResult types
│   ├── OrderBook.h    # Order book with custom data structures
│   ├── OrderNode.h    # Intrusive list node for orders
│   ├── PriceLevel.h   # Price level with order queue
│   ├── MatchingEngine.h
│   ├── ConcurrentMatchingEngine.h  # Thread-safe sharded engine
│   ├── RiskManager.h  # Pre-trade risk checks
│   ├── PnLTracker.h   # Position and P&L tracking
│   ├── HashMap.h      # Robin Hood hash map
│   ├── IntrusiveList.h
│   ├── ObjectPool.h   # Memory pool allocator
│   ├── ThreadPool.h   # Thread pool and parallel utilities
│   ├── AsyncWriter.h  # Async file I/O with buffering
│   ├── Profiler.h     # Latency instrumentation
│   ├── CSVParser.h    # CSV parsing (with parallel mode)
│   └── TradeWriter.h
├── src/               # Implementation files
├── tests/             # Google Test suites
│   ├── concurrency_test.cpp  # Thread pool, async writer tests
│   └── ...
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
./build/mercury data/sample_orders.csv trades.csv executions.csv riskevents.csv pnl.csv

# Run with concurrency enabled (parallel parsing + async post-trade)
./build/mercury data/sample_orders.csv --concurrent

# Run with async I/O writers (background file writes)
./build/mercury data/sample_orders.csv --async-io

# Run with both (maximum parallelism)
./build/mercury data/sample_orders.csv --concurrent --async-io

# Run interactive demo
./build/mercury
```

### Command Line Options

| Option | Short | Description |
|--------|-------|-------------|
| `--concurrent` | `-c` | Enable parallel CSV parsing and async post-trade processing |
| `--async-io` | `-a` | Enable asynchronous file I/O with background writer threads |

### Example Output

```
========================================
   Mercury File I/O Mode
========================================
Input:       data/sample_orders_with_clients.csv
Trades:      trades.csv
Executions:  executions.csv
Risk Events: riskevents.csv
P&L:         pnl.csv
Concurrency: Enabled
Async I/O:   Disabled
========================================

Processing Complete
----------------------------------------
Parse time:    1.07 ms
Process time:  0.82 ms
Total time:    1.89 ms
Throughput:    91799 orders/sec

Order Status Summary:
  Filled: 24, Partial Fill: 7, Resting: 34
  Cancelled: 5, Rejected: 5

Risk Manager Statistics:
  Risk Checks:  75
  Approved:     75
  Clients:      10

P&L Summary (per client):
  Client 1:  Net Pos=-85,   Total P&L=+4,250  (winner)
  Client 8:  Net Pos=+20,   Total P&L=0       (breakeven)
  Client 9:  Net Pos=+40,   Total P&L=-205    (small loss)
  Client 10: Net Pos=+75,   Total P&L=-535    (loss)
  Client 6:  Net Pos=+65,   Total P&L=-600    (loss)
  Client 4:  Net Pos=-1185, Total P&L=-4,405  (big short loss)
  Client 5:  Net Pos=+80,   Total P&L=-4,500  (loss)
  Client 3:  Net Pos=+100,  Total P&L=-10,075 (big loss)
  Client 7:  Net Pos=+70,   Total P&L=-29,950 (worst performer)

Total Trades: 53
Total Volume: 2660 units
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

200+ unit tests covering:
- Order book operations (insert, remove, update)
- Matching engine (limit, market, IOC, FOK)
- Risk manager (position limits, exposure limits, order limits)
- P&L tracker (realized P&L, unrealized P&L, FIFO cost basis)
- Concurrency (thread pool, async writers, parallel parsing)
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
- [x] PnL module (realized + unrealized)
- [x] Multithreading/concurrency (thread pool, async I/O, parallel parsing)
- [ ] Symbol-sharded matching (multi-symbol parallel processing)
- [ ] Strategy layer (market making, momentum)
- [ ] Backtesting framework

## License

AGPL-3.0