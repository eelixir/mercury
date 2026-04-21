# <img src="https://github.com/user-attachments/assets/7ee41ddf-cf24-42fb-953b-d44c55e9f352" width="400">

> High-performance C++ matching engine and live trading dashboard built from scratch with custom data structures, real-time WebSocket streaming, and nanosecond latency instrumentation.

## Overview

Mercury is a low-latency trading engine implementing a full limit order book with price-time priority matching. It ships with a localhost HTTP/WebSocket server and a React dashboard for real-time market visualization, order entry, and performance monitoring.

**Key Metrics:**
- **3.2M+ orders/sec** sustained throughput
- **~320 ns** average order insertion latency
- **O(1)** order lookup, insertion, and cancellation
- **243** backend tests, all passing
- **Nanosecond** gateway-to-engine latency instrumentation

## Features

- **Order Types:** Limit, Market, Cancel, Modify
- **Time-in-Force:** GTC (Good-til-Canceled), IOC (Immediate-or-Cancel), FOK (Fill-or-Kill)
- **Price-Time Priority:** FIFO matching at each price level
- **Self-Trade Prevention:** Client ID based filtering
- **Risk Management:** Pre-trade risk checks with position/exposure limits
- **P&L Tracking:** Realized and unrealized P&L with FIFO cost basis
- **Trading Strategies:** Market making and momentum strategies with real-time execution
- **Backtesting:** Framework with simulated order flow and P&L tracking
- **Live Server:** HTTP order entry + dual WebSocket market data (JSON and binary)
- **React Dashboard:** Real-time ladder, trade tape, mid-price chart, order entry, P&L, system health
- **Latency Telemetry:** Nanosecond-precision tracking from gateway entry through engine to publication
- **Throughput Monitoring:** Messages-per-second counter broadcast to the dashboard
- **Binary Protocol:** Packed wire-format structs for high-throughput market data consumers

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       React Dashboard                           │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────┐ │
│  │ Ladder   │ │ Trade    │ │ Chart    │ │ Order    │ │System│ │
│  │ (L2)     │ │ Tape     │ │ (Mid)    │ │ Entry    │ │Health│ │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────┘ │
│                Zustand Store ← WebSocket ← /ws/market           │
└────────────────────────────┬────────────────────────────────────┘
                             │ HTTP POST /api/orders
┌────────────────────────────▼────────────────────────────────────┐
│                    OrderEntryGateway                             │
│  JSON parse → latency stamp → EngineService::submitOrder()      │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                      EngineService                              │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────┐ │
│  │ Engine Thread    │  │ PnL Tracker      │  │ Stats/MPS     │ │
│  │ (single-writer)  │  │ (FIFO method)    │  │ (1s samples)  │ │
│  └──────────────────┘  └──────────────────┘  └───────────────┘ │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                      MatchingEngine                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ Order       │  │ Trade       │  │ Book Mutation            │ │
│  │ Validation  │─▶│ Matching    │─▶│ Callbacks               │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                    MarketDataPublisher                           │
│  ┌──────────────────┐              ┌──────────────────────────┐ │
│  │ /ws/market       │              │ /ws/market/bin           │ │
│  │ JSON text frames │              │ Binary packed frames     │ │
│  │ + engineLatencyNs│              │ + engineLatencyNs        │ │
│  └──────────────────┘              └──────────────────────────┘ │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                         OrderBook                               │
│  ┌──────────────────────┐    ┌──────────────────────────┐      │
│  │ Bids (price desc)    │    │ Asks (price asc)         │      │
│  │ std::map<PriceLevel> │    │ std::map<PriceLevel>     │      │
│  └──────────┬───────────┘    └───────────┬──────────────┘      │
│             │                            │                      │
│  ┌──────────▼────────────────────────────▼──────────────┐      │
│  │              PriceLevel (IntrusiveList)              │      │
│  │  OrderNode ←→ OrderNode ←→ OrderNode ←→ ...          │      │
│  └──────────────────────────────────────────────────────┘      │
│                              │                                  │
│  ┌───────────────────────────▼──────────────────────────┐      │
│  │ HashMap<OrderID, Location>  │  ObjectPool<OrderNode> │      │
│  │ O(1) lookup (Robin Hood)    │  Pre-allocated memory  │      │
│  └──────────────────────────────────────────────────────┘      │
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

### Server Components

| Component | Purpose |
|-----------|---------|
| **EngineService** | Engine thread serialization, sequencing, latency/MPS telemetry |
| **OrderEntryGateway** | HTTP POST parsing, latency stamping, sync engine roundtrip |
| **MarketDataPublisher** | JSON + binary WebSocket broadcast via uWS loop defer |
| **ServerApp** | HTTP/WS route registration, lifecycle management |
| **BinaryProtocol** | Packed `#pragma pack(push,1)` structs for wire-efficient streaming |

## Dashboard

The React frontend (`/frontend`) provides a real-time trading interface:

| Panel | Description |
|-------|-------------|
| **Top Bar** | Symbol, mid-price, spread, connection badge, live clock |
| **Stats Strip** | Bid, ask, mid, spread (bps), trades, volume, orders, levels |
| **Order Entry** | Limit/market/cancel/modify with buy/sell toggle, price, qty, TIF |
| **PnL Card** | Net position, total/realized/unrealized P&L (green/red) |
| **System Health** | Engine latency (color-coded µs), throughput (msg/s), connection dot |
| **Mid-Price Chart** | Lightweight-charts line with delta % tracking |
| **Order Book Ladder** | L2 depth — asks (red) above, spread marker, bids (green) below |
| **Trade Tape** | Time & sales with uptick/downtick, value, self-trade "You" badge |
| **Status Bar** | WS state, active client, trade count, volume, levels, timezone |

## Project Structure

```
mercury/
├── include/                    # Headers
│   ├── Order.h                 # Order, Trade, ExecutionResult types
│   ├── OrderBook.h             # Order book with custom data structures
│   ├── MatchingEngine.h        # Price-time priority matching
│   ├── EngineService.h         # Live engine thread + telemetry
│   ├── MarketData.h            # Market-data DTOs (BookDelta, TradeEvent, StatsEvent)
│   ├── MarketDataPublisher.h   # JSON + binary WebSocket publisher
│   ├── OrderEntryGateway.h     # HTTP order entry handler
│   ├── BinaryProtocol.h        # Packed binary wire-format structs
│   ├── ServerApp.h             # Server entrypoint
│   ├── ServerHelpers.h         # Shared JSON/HTTP helpers
│   ├── RiskManager.h           # Pre-trade risk checks
│   ├── PnLTracker.h            # Position and P&L tracking
│   ├── Profiler.h              # Latency measurement utilities
│   ├── HashMap.h               # O(1) Robin Hood hash map
│   ├── IntrusiveList.h         # Cache-friendly doubly-linked list
│   ├── ObjectPool.h            # Pre-allocated node pool
│   └── ...                     # Strategies, backtesting, concurrency
├── src/                        # Implementations
│   ├── MatchingEngine.cpp
│   ├── EngineService.cpp
│   ├── OrderEntryGateway.cpp
│   ├── MarketDataPublisher.cpp
│   ├── ServerApp.cpp
│   └── main.cpp
├── tests/                      # Google Test suites (243 tests)
├── benchmarks/                 # Optional benchmark target
├── frontend/                   # React/Vite/TypeScript dashboard
│   ├── src/
│   │   ├── components/         # Ladder, tape, chart, forms, health
│   │   ├── store/              # Zustand market-data store
│   │   ├── hooks/              # WebSocket connection hook
│   │   └── lib/                # Types, formatters, utilities
│   └── vite.config.ts          # Dev proxy → backend
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

### Run The Live Stack

Terminal 1 — backend:
```powershell
.\build\mercury.exe --server --host 127.0.0.1 --port 9001 --symbol SIM
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
.\build\mercury.exe --server --host 127.0.0.1 --port 9001 --symbol SIM --replay data\sample_orders_with_clients.csv --replay-speed 10
```

## HTTP API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/health` | GET | Server liveness check |
| `/api/state` | GET | Engine metadata and connection count |
| `/api/orders` | POST | Submit order (limit, market, cancel, modify) |

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

## WebSocket API

| Path | Format | Snapshot | Events |
|------|--------|----------|--------|
| `/ws/market` | JSON text | ✅ on connect | `book_delta`, `trade`, `stats`, `pnl` |
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
    "orderCount": 1,
    "action": "upsert",
    "timestamp": 1713660000000,
    "engineLatencyNs": 4200
  }
}
```

### Telemetry Fields

| Field | Location | Description |
|-------|----------|-------------|
| `engineLatencyNs` | `book_delta`, `trade` payloads | Gateway-to-engine latency in nanoseconds |
| `messagesPerSecond` | `stats` payload | Engine-thread throughput sampled every ~1 second |

### Binary Protocol

Messages on `/ws/market/bin` use packed structs from `include/BinaryProtocol.h`:

| Struct | Size | Header Type |
|--------|------|-------------|
| `BinaryBookDelta` | 53 bytes | `1` |
| `BinaryTradeEvent` | 77 bytes | `2` |

All fields are little-endian (x86/x64 host order). Read `BinaryHeader.type` to determine layout.

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
| `--host <addr>` | | Bind address (default `127.0.0.1`) |
| `--port <port>` | `-p` | Listen port (default `9001`) |
| `--symbol <name>` | | Symbol name (default `SIM`) |
| `--replay <file>` | | CSV replay file |
| `--replay-speed <x>` | | Replay speed multiplier |
| `--strategies` | `-s` | Run strategy simulation demos |
| `--backtest` | `-b` | Run backtesting demos |
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

243 unit tests covering:
- Order book operations (insert, remove, update)
- Matching engine (limit, market, IOC, FOK)
- Risk manager (position limits, exposure limits)
- P&L tracker (realized, unrealized, FIFO cost basis)
- Market data (sequencing, snapshots, deltas)
- Trading strategies (market making, momentum)
- Backtesting (order flow simulation, metrics)
- Concurrency (thread pool, async writers)
- Stress tests (100K+ orders, deep books)
- Custom data structures (HashMap, IntrusiveList)

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
- Frontend is a separate Vite dev app, not served by C++

## License

AGPL-3.0
