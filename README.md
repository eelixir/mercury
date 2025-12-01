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
- **Trading Strategies:** Market making and momentum strategies with real-time execution
- **Backtesting:** Comprehensive backtesting framework with simulated order flow and P&L tracking
- **Trade Logging:** CSV output for all executions, risk events, and P&L snapshots
- **Comprehensive Validation:** Detailed rejection reasons for invalid orders
- **Concurrency Support:** Thread pool, parallel CSV parsing, async I/O writers

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                      StrategyManager                            │
│  ┌─────────────────┐  ┌─────────────────┐  ┌────────────────┐  │
│  │ MarketMaking    │  │ Momentum        │  │ Custom         │  │
│  │ (bid-ask quote) │  │ (trend follow)  │  │ Strategies     │  │
│  └─────────────────┘  └─────────────────┘  └────────────────┘  │
└────────────────────────────────┬───────────────────────────────┘
                                 │
┌────────────────────────────────▼────────────────────────────────┐
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
│   ├── Strategy.h     # Base strategy framework
│   ├── MarketMakingStrategy.h  # Bid-ask quoting strategy
│   ├── MomentumStrategy.h      # Trend-following strategy
│   ├── StrategyManager.h       # Strategy orchestration
│   ├── StrategyDemo.h  # Demo scenarios
│   ├── Backtester.h    # Backtesting framework
│   ├── BacktestDemo.h  # Backtest demo functions
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
│   ├── strategy_test.cpp     # Strategy unit tests (29 tests)
│   ├── concurrency_test.cpp  # Thread pool, async writer tests
│   └── ...
├── benchmarks/        # Google Benchmark micro-benchmarks
├── data/              # Sample order datasets
└── docs/              # Additional documentation
    ├── PROFILING.md   # Performance analysis guide
    ├── STRATEGIES.md  # Strategy development guide
    └── BACKTESTING.md # Backtesting framework guide
```

## Building & Running

### Prerequisites
- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.10+

### Build (Linux/macOS)

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

### Build (Windows with MSVC)

```powershell
# Standard build (from Developer PowerShell or Command Prompt)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# With AddressSanitizer (MSVC 2019 16.9+ required)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DMERCURY_ENABLE_ASAN=ON
cmake --build build --config Debug

# With benchmarks
cmake -B build -G "Visual Studio 17 2022" -A x64 -DMERCURY_BUILD_BENCHMARKS=ON
cmake --build build --config Release
```

### Build (Windows with MinGW/MSYS2)

```bash
# Using MSYS2 UCRT64 environment
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With sanitizers
cmake -B build -G "MinGW Makefiles" -DMERCURY_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
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

# Run strategy simulation demo
./build/mercury --strategies

# Run backtesting demos
./build/mercury --backtest             # All backtest demos
./build/mercury --backtest mm          # Market making backtest
./build/mercury --backtest momentum    # Momentum strategy backtest
./build/mercury --backtest multi       # Multi-strategy comparison
./build/mercury --backtest compare     # Market condition comparison
./build/mercury --backtest stress      # Stress test
```

### Command Line Options

| Option | Short | Description |
|--------|-------|-------------|
| `--concurrent` | `-c` | Enable parallel CSV parsing and async post-trade processing |
| `--async-io` | `-a` | Enable asynchronous file I/O with background writer threads |
| `--strategies` | `-s` | Run trading strategy simulation demos |
| `--backtest` | `-b` | Run backtesting demos (mm, momentum, multi, compare, stress) |

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

## Trading Strategies

Mercury includes a strategy layer for developing and backtesting trading algorithms.

### Market Making Strategy

Provides liquidity by continuously quoting bid and ask prices:

```cpp
#include "StrategyManager.h"

MarketMakingConfig config;
config.name = "MarketMaker";
config.quoteQuantity = 100;    // Size per quote
config.minSpread = 2;          // Minimum bid-ask spread
config.maxSpread = 10;         // Maximum spread
config.maxInventory = 500;     // Position limit
config.fadePerUnit = 0.01;     // Spread adjustment per inventory unit

StrategyManager manager(engine);
manager.addStrategy(std::make_unique<MarketMakingStrategy>(config));
```

Features:
- Dynamic spread based on inventory risk
- Automatic quote refresh on market moves
- Inventory skew (fade) to manage position
- Configurable quote sizes and limits

### Momentum Strategy

Trend-following strategy using technical indicators:

```cpp
MomentumConfig config;
config.name = "Momentum";
config.baseQuantity = 50;
config.shortPeriod = 5;        // Short MA period
config.longPeriod = 20;        // Long MA period
config.entryThreshold = 0.02;  // 2% momentum for entry
config.stopLossPct = 0.03;     // 3% stop loss
config.takeProfitPct = 0.06;   // 6% take profit
config.maxPosition = 100;      // Conservative position limit

manager.addStrategy(std::make_unique<MomentumStrategy>(config));
```

Features:
- SMA/EMA moving averages
- MACD indicator with histogram tracking
- RSI overbought/oversold detection
- Position size limits and enforcement
- Stop-loss and take-profit automation
- Trailing stops with minimum hold period
- Signal confirmation over multiple bars

### Running Strategies

```bash
# Run strategy demo (market making + momentum simulation)
./build/mercury --strategies
```

Example output:
```
========================================
   Combined Strategies Demo
========================================
Strategies registered: 2
 - MarketMaking (Client 1)
 - Momentum (Client 2)

--- Simulating 80 Market Ticks ---
[MarketMaking] Order 1000000: RESTING Filled=0 Remaining=50 Trades=0
[Momentum] Order 2000000: FILLED Filled=30 Remaining=0 Trades=2
...

=== Strategy Manager Summary ===
--- Momentum ---
  Orders: 39 submitted, 102 filled
  Trades: 176, Volume: 5800
  Position: -30, P&L: 120

--- MarketMaking ---
  Orders: 125 submitted, 138 filled
  Trades: 138, Volume: 3965
  Position: -266, P&L: 4522
```

See [docs/STRATEGIES.md](docs/STRATEGIES.md) for detailed strategy development guide.

## Backtesting

Mercury includes a comprehensive backtesting framework for validating strategies with simulated order flow.

### Running Backtests

```bash
# Run all backtest demos
./build/mercury --backtest

# Run specific backtest
./build/mercury --backtest mm          # Market making
./build/mercury --backtest momentum    # Momentum strategy
./build/mercury --backtest multi       # Multi-strategy comparison
./build/mercury --backtest compare     # Market condition comparison
./build/mercury --backtest stress      # Stress test (2000 ticks)
```

### Features

- **Order Flow Simulation:** 7 market patterns (Random, Trending, MeanReverting, HighVolatility, LowVolatility, MomentumBurst, Choppy)
- **Strategy Validation:** Test strategies with realistic order flow
- **P&L Tracking:** FIFO-based realized P&L and mark-to-market unrealized P&L
- **CSV Output:** Trade logs, order logs, and P&L snapshots
- **Performance Metrics:** Win rate, fill rate, drawdown, Sharpe ratio

### Example Usage

```cpp
#include "Backtester.h"

// Configure backtest
BacktestConfig config;
config.numTicks = 1000;
config.warmupTicks = 50;
config.orderFlow.pattern = OrderFlowPattern::MeanReverting;
config.orderFlow.volatility = 0.015;

// Create backtester
Backtester backtester(config);

// Add strategy
MarketMakingConfig mmConfig;
mmConfig.quoteQuantity = 50;
mmConfig.maxInventory = 500;
backtester.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));

// Run and analyze
auto report = backtester.run();
std::cout << "P&L: " << report.strategyMetrics[0].totalPnL << "\n";
std::cout << "Win Rate: " << (report.strategyMetrics[0].winRate * 100) << "%\n";
```

See [docs/BACKTESTING.md](docs/BACKTESTING.md) for the complete backtesting guide.

## Performance

Benchmarks run on 12-core CPU @ 3.6GHz (Release build):

| Operation | Latency | Throughput |
|-----------|---------|------------|
| Order Insert | 314 ns | 3.2M/sec |
| Order Match (10 levels) | 1.1 µs | 1.3M/sec |
| Order Cancel | 2.9 µs | 427K/sec |
| Market Sweep (5 levels) | 1.8 µs | 551K/sec |
| **Sustained Mixed Load** | 312 ns | **3.2M/sec** |

### Run Benchmarks

```bash
cmake -B build -DMERCURY_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mercury_benchmarks
```

## Testing

241 unit tests covering:
- Order book operations (insert, remove, update)
- Matching engine (limit, market, IOC, FOK)
- Risk manager (position limits, exposure limits, order limits)
- P&L tracker (realized P&L, unrealized P&L, FIFO cost basis)
- Trading strategies (market making, momentum, signal generation)
- Backtesting (order flow simulation, P&L tracking, metrics)
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

## Future Improvements

Mercury provides a solid foundation for order matching with production-quality performance. To evolve into a complete trading system, the following areas could be explored:

- **Network Layer:** FIX protocol or WebSocket/REST APIs for external connectivity, session management with heartbeats and sequence numbers
- **Persistence:** Write-Ahead Log (WAL) for crash recovery and durable order/trade storage
- **Security:** Authentication, API keys, and role-based access control
- **Market Structure:** Multi-symbol instrument registry, market data dissemination (L2/L3 feeds)
- **Risk & Compliance:** Circuit breakers, price bands, audit trails, additional order types (Stop, Iceberg)
- **Observability:** Prometheus metrics, health checks, structured logging, alerting

## License

AGPL-3.0