# <img src="https://github.com/user-attachments/assets/7ee41ddf-cf24-42fb-953b-d44c55e9f352" width="400">

> High-performance C++ matching engine and live market simulation dashboard built around a custom intrusive order-book core, Abseil-backed containers, real-time WebSocket streaming, and bounded agent-based microstructure dynamics.

## Overview

Mercury is a low-latency trading engine implementing a full limit order book with price-time priority matching. It ships with a localhost HTTP/WebSocket server, a React dashboard for live visualization, and a unified market simulation runtime where manual browser orders, replayed flow, and built-in agents all trade through the same engine thread.

**Key Metrics:**
- **3.2M+ orders/sec** sustained throughput
- **~320 ns** average order insertion latency
- **O(1)** order lookup, insertion, and cancellation
- **248** backend tests, all passing
- **Nanosecond** gateway-to-engine latency instrumentation

## Features

- **Order Types:** Limit, Market, Cancel, Modify
- **Time-in-Force:** GTC (Good-til-Canceled), IOC (Immediate-or-Cancel), FOK (Fill-or-Kill)
- **Price-Time Priority:** FIFO matching at each price level
- **Self-Trade Prevention:** Client ID based filtering
- **Risk Management:** Pre-trade risk checks with position/exposure limits
- **P&L Tracking:** Realized and unrealized P&L with FIFO cost basis
- **Unified Market Runtime:** One runtime for manual orders, replay, built-in agents, and headless simulation
- **Built-In Agents:** Passive market maker, aggressive momentum trader, mean-reversion bot
- **Live Server:** HTTP order entry + dual WebSocket market data (JSON and binary)
- **React Dashboard:** Real-time ladder, trade tape, mid-price chart, order entry, P&L, simulation controls, system health
- **Latency Telemetry:** Nanosecond-precision tracking from gateway entry through engine to publication
- **Throughput Monitoring:** Messages-per-second counter broadcast to the dashboard
- **Bounded Volatility Presets:** `low`, `normal`, and `high` widen spread and increase activity without runaway 100% to 1000% price jumps in seconds
- **Binary Protocol:** Packed wire-format structs for high-throughput market data consumers

## Architecture

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                            React Dashboard                                 │
│  Order Entry │ PnL │ Sim Controls │ Ladder │ Tape │ Chart │ System Health │
│                  Zustand Store ← WebSocket ← /ws/market                    │
└─────────────────────────────────┬───────────────────────────────────────────┘
                                  │ HTTP POST /api/orders
┌─────────────────────────────────▼───────────────────────────────────────────┐
│                           OrderEntryGateway                                │
│            JSON parse → latency stamp → MarketRuntime::submitOrder()       │
└─────────────────────────────────┬───────────────────────────────────────────┘
                                  │
┌─────────────────────────────────▼───────────────────────────────────────────┐
│                             MarketRuntime                                  │
│  Simulation loop │ Environment │ Agent registry │ Replay │ Runtime fanout  │
│  manual orders + replay + built-in agents → shared submission path         │
└─────────────────────────────────┬───────────────────────────────────────────┘
                                  │
┌─────────────────────────────────▼───────────────────────────────────────────┐
│                             EngineService                                  │
│   Engine thread (single writer) │ PnL tracker │ stats │ sequencing         │
└─────────────────────────────────┬───────────────────────────────────────────┘
                                  │
┌─────────────────────────────────▼───────────────────────────────────────────┐
│                            MatchingEngine                                  │
│    Validation → Matching → Book mutations → trade/execution callbacks      │
└─────────────────────────────────┬───────────────────────────────────────────┘
                                  │
┌─────────────────────────────────▼───────────────────────────────────────────┐
│                          MarketDataPublisher                               │
│     JSON /ws/market                     Binary /ws/market/bin              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Core Data Structures

The core book keeps intrusive FIFO queues inside each price level while using `absl::btree_map` for sorted bid/ask ladders and an Abseil-backed `HashMap` wrapper for O(1) average order lookup.

| Component | Purpose | Complexity |
|-----------|---------|------------|
| **HashMap** | Abseil-backed `flat_hash_map` wrapper for order lookup | O(1) avg |
| **btree_map ladders** | Sorted bid/ask price ladders with cache-friendly traversal | O(log N) level ops |
| **IntrusiveList** | Order queue at each price level | O(1) insert/remove |
| **ObjectPool** | Pre-allocated order nodes | O(1) alloc/free |
| **PriceLevel** | Orders + cached aggregate quantity | O(1) quantity query |
| **ThreadPool** | Task scheduling for parallel work | O(1) submit |
| **AsyncWriter** | Background I/O with buffering | Lock-free fast path |

### Server Components

| Component | Purpose |
|-----------|---------|
| **MarketRuntime** | Simulation loop, environment state, built-in agents, replay coordination, runtime fanout |
| **EngineService** | Engine thread serialization, sequencing, P&L, latency/MPS telemetry |
| **OrderEntryGateway** | HTTP POST parsing, latency stamping, sync runtime roundtrip |
| **MarketDataPublisher** | JSON + binary WebSocket broadcast via uWS loop defer |
| **ServerApp** | HTTP/WS route registration, lifecycle management |
| **BinaryProtocol** | Packed `#pragma pack(push,1)` structs for wire-efficient streaming |

## Dashboard

The React frontend (`/frontend`) provides a real-time market-operations interface:

| Panel | Description |
|-------|-------------|
| **Top Bar** | Symbol, mid-price, spread, connection badge, runtime status |
| **Stats Strip** | Bid, ask, mid, spread, trades, volume, orders, levels |
| **Order Entry** | Limit/market/cancel/modify with buy/sell toggle, price, qty, TIF |
| **PnL Card** | Net position, total/realized/unrealized P&L |
| **Simulation Controls** | Pause/resume, restart, volatility preset, runtime state |
| **System Health** | Engine latency, throughput, connection state |
| **Mid-Price Chart** | Lightweight-charts line view of recent mid-price evolution |
| **Order Book Ladder** | L2 depth, asks above and bids below |
| **Trade Tape** | Time and sales with self-trade highlighting |
| **Status Bar** | WS state, active client, trade count, volume, levels, timezone |

## Project Structure

```text
mercury/
├── include/                    # Headers
│   ├── Order.h                 # Order, Trade, ExecutionResult types
│   ├── OrderBook.h             # Order book with Abseil ladders + intrusive FIFO levels
│   ├── MatchingEngine.h        # Price-time priority matching
│   ├── EngineService.h         # Live engine thread + telemetry
│   ├── MarketRuntime.h         # Unified simulation/runtime layer
│   ├── MarketData.h            # Market-data DTOs and sink interfaces
│   ├── MarketDataPublisher.h   # JSON + binary WebSocket publisher
│   ├── OrderEntryGateway.h     # HTTP order entry handler
│   ├── BinaryProtocol.h        # Packed binary wire-format structs
│   ├── ServerApp.h             # Server entrypoint
│   └── ...
├── src/                        # Implementations
│   ├── MatchingEngine.cpp
│   ├── EngineService.cpp
│   ├── MarketRuntime.cpp
│   ├── OrderEntryGateway.cpp
│   ├── MarketDataPublisher.cpp
│   ├── ServerApp.cpp
│   └── main.cpp
├── tests/                      # Google Test suites (248 tests)
├── benchmarks/                 # Optional benchmark target
├── frontend/                   # React/Vite/TypeScript dashboard
├── data/                       # Sample CSV inputs
├── docs/                       # ARCHITECTURE.md, WORKFLOWS.md
└── AGENTS.md                   # Agent guidance
```

## Quick Start

### Build

```powershell
cmake -B build -G Ninja
cmake --build build
```

The backend pulls third-party dependencies with CMake `FetchContent`, including Abseil for the ladder and lookup containers.

### Run The Live Stack

Terminal 1 — backend:
```powershell
.\build\mercury.exe --server --sim --host 127.0.0.1 --port 9001 --symbol SIM
```

Terminal 2 — frontend:
```powershell
Set-Location frontend
npm install
npm run dev
```

Open `http://127.0.0.1:5173`. Vite proxies `/api` and `/ws` to the backend.

### Run With Replay

```powershell
.\build\mercury.exe --server --sim --host 127.0.0.1 --port 9001 --symbol SIM --replay data\sample_orders_with_clients.csv --replay-speed 10
```

### Run Headless Accelerated Simulation

```powershell
.\build\mercury.exe --sim --headless --sim-speed 25 --sim-seed 42 --sim-duration-ms 30000 --sim-volatility normal
```

## HTTP API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/health` | GET | Server liveness and runtime status |
| `/api/state` | GET | Engine metadata, market summary, and simulation state |
| `/api/orders` | POST | Submit order (limit, market, cancel, modify) |
| `/api/simulation/control` | POST | Pause, resume, restart, or change volatility |

Example order submission:

```json
{
  "type": "limit",
  "side": "buy",
  "price": 101,
  "quantity": 10,
  "clientId": 1,
  "tif": "GTC"
}
```

Simulation control example:

```json
{
  "action": "set_volatility",
  "volatility": "high"
}
```

## WebSocket API

| Path | Format | Snapshot | Events |
|------|--------|----------|--------|
| `/ws/market` | JSON text | ✅ on connect | `book_delta`, `trade`, `execution`, `stats`, `pnl`, `sim_state` |
| `/ws/market/bin` | Binary packed | ❌ | `book_delta`, `trade` |

### Envelope Shape (JSON)

```json
{
  "type": "book_delta",
  "sequence": 42,
  "symbol": "SIM",
  "payload": {
    "side": "buy",
    "price": 100,
    "quantity": 5,
    "orderCount": 1
  }
}
```

### Telemetry Fields

| Field | Location | Description |
|-------|----------|-------------|
| `engineLatencyNs` | `book_delta`, `trade` payloads | Gateway-to-engine latency in nanoseconds |
| `messagesPerSecond` | `stats` payload | Engine-thread throughput sampled every ~1 second |

### Simulation State

`sim_state` frames expose:

- running and paused state
- clock mode and speed multiplier
- current volatility preset
- simulation timestamp
- market-maker, momentum, and mean-reversion agent counts
- realized volatility and average spread summaries

### Binary Protocol

Messages on `/ws/market/bin` use packed structs from `include/BinaryProtocol.h`:

| Struct | Size | Header Type |
|--------|------|-------------|
| `BinaryBookDelta` | 53 bytes | `1` |
| `BinaryTradeEvent` | 77 bytes | `2` |

All fields are little-endian (x86/x64 host order).

## Other Runtime Modes

### File Processing

```powershell
.\build\mercury.exe data\sample_orders.csv trades.csv executions.csv riskevents.csv pnl.csv
.\build\mercury.exe data\sample_orders.csv --concurrent --async-io
```

### CLI Flags

| Flag | Short | Description |
|------|-------|-------------|
| `--server` | `-S` | Start HTTP/WebSocket server |
| `--sim` | | Enable the living market simulation runtime |
| `--headless` | | Run the same simulation runtime without the browser server |
| `--host <addr>` | | Bind address (default `127.0.0.1`) |
| `--port <port>` | `-p` | Listen port (default `9001`) |
| `--symbol <name>` | | Symbol name (default `SIM`) |
| `--replay <file>` | | CSV replay file |
| `--replay-speed <x>` | | Replay speed multiplier |
| `--sim-speed <x>` | | Simulation clock speed multiplier |
| `--sim-seed <n>` | | Deterministic simulation seed |
| `--sim-volatility <low\|normal\|high>` | | Volatility preset |
| `--mm-count <n>` | | Passive market-maker count |
| `--mom-count <n>` | | Aggressive momentum-agent count |
| `--mr-count <n>` | | Mean-reversion-agent count |
| `--sim-duration-ms <n>` | | Bounded headless run duration in simulated milliseconds |
| `--strategies` | `-s` | Route legacy strategy demos through the unified runtime |
| `--backtest` | `-b` | Route legacy backtests through the unified runtime |
| `--concurrent` | `-c` | Enable concurrent processing |
| `--async-io` | `-a` | Enable async file I/O |

## Performance

Benchmarks run on 12-core CPU @ 3.6GHz (Release build):

| Operation | Latency | Throughput |
|-----------|---------|------------|
| Order Insert | 321 ns | 3.1M/sec |
| Order Match (10 levels) | 1.9 µs | 526K/sec |
| Order Cancel | 2.7 µs | 370K/sec |
| Market Sweep (5 levels) | 1.8 µs | 556K/sec |
| **Sustained Mixed Load** | 312 ns | **3.2M/sec** |

```powershell
cmake -B build -DMERCURY_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build
.\build\mercury_benchmarks.exe
```

## Testing

248 unit tests covering:
- Order book operations (insert, remove, update)
- Matching engine (limit, market, IOC, FOK)
- Risk manager (position limits, exposure limits)
- P&L tracker (realized, unrealized, FIFO cost basis)
- Market data (sequencing, snapshots, deltas)
- Unified simulation runtime and agent fanout
- Bounded volatility excursion and long-run two-sided book maintenance
- Trading strategies and legacy migration coverage
- Concurrency (thread pool, async writers)
- Stress tests (100K+ orders, deep books)
- Core data structures and container behavior

```powershell
# Backend
ctest --test-dir build --output-on-failure

# Frontend
Set-Location frontend
npm run test:run
npm run build
```

## Current V1 Boundaries

- Localhost only, no auth
- Single book in core engine, symbol at API layer only
- JSON primary transport, binary secondary for throughput-sensitive consumers
- Browser writes over HTTP, market data over WebSocket
- Browser is a thin operator client; the market can stay active without manual orders
- Frontend is a separate Vite dev app, not served by C++
- Python strategy loading is deferred; v1 custom strategies are C++ only

## License

AGPL-3.0
