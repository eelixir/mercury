# <img src="https://github.com/user-attachments/assets/7ee41ddf-cf24-42fb-953b-d44c55e9f352" width="400">

> Market-making and order-book simulation lab built around a high-performance C++ matching engine, custom intrusive order-book core, instant backtests, real-time WebSocket streaming, and bounded agent-based microstructure dynamics.

## Overview

Mercury is a local market-making and order-book simulation lab. Its core is a full limit order book with price-time priority matching, wrapped by a unified runtime where manual browser orders, replayed flow, built-in agents, instant backtests, and live real-time simulation all trade through the same engine thread.

The intended use case is experimentation: compare liquidity-provision settings, stress order-flow regimes, inspect queue and P&L behavior, and produce repeatable backtest artifacts without connecting to a live broker or venue.

**Key Metrics:**
- **3.2M+ orders/sec** sustained throughput
- **~320 ns** average order insertion latency
- **O(1)** order lookup, insertion, and cancellation
- **249** backend tests, all passing
- **Nanosecond** gateway-to-engine latency instrumentation

## Features

- **Order Types:** Limit, Market, Cancel, Modify
- **Time-in-Force:** GTC (Good-til-Canceled), IOC (Immediate-or-Cancel), FOK (Fill-or-Kill)
- **Price-Time Priority:** FIFO matching at each price level
- **Self-Trade Prevention:** Client ID based filtering
- **Risk Management:** Pre-trade risk checks with position/exposure limits
- **P&L Tracking:** Realized and unrealized P&L with FIFO cost basis
- **Unified Market Runtime:** One runtime for manual orders, replay, built-in agents, instant backtests, and live real-time simulation
- **Browser Lab Runner:** Start instant backtests, headless runs, sweeps, and replay calibration from the Lab tab or `/api/lab/run`
- **Backtest Artifacts:** Local JSON/CSV output for run summary, config, trades, stats, P&L, simulation state, agent attribution, and queue analytics
- **Parameter Sweeps:** JSON-driven batch runner for comparing market-maker counts, volatility presets, seeds, flow mixes, P&L, drawdown, and fill quality
- **Scenario Presets:** Versioned JSON scenarios for calm books, toxic flow, thin-book stress, high-cancel churn, and momentum bursts
- **Market-Maker Tuning:** Runtime and file-driven controls for quote levels, spread, size, wake interval, toxicity sensitivity, and inventory skew
- **Replay Calibration:** Replay CSV calibration reports comparing target order mix and quantity profile with observed simulated output
- **Built-In Agents:** Passive market maker, aggressive momentum trader, mean-reversion bot, Poisson-flow noise trader
- **Advanced Microstructure:** Queue-position-aware agents, deeper multi-level market-maker quoting, and toxicity-driven spread widening
- **Regime Manager:** Auto-detected `calm`/`normal`/`stressed` regimes with explicit Poisson lambda controls for limit, cancel, and marketable arrival rates, plus Pareto order-size dispersion for whale-vs-retail flow
- **Live Server:** HTTP order entry + dual WebSocket market data (JSON and binary)
- **React Dashboard:** Real-time ladder, trade tape, mid-price chart, order entry, P&L, simulation controls, system health
- **Latency Telemetry:** Nanosecond-precision tracking from gateway entry through engine to publication
- **Throughput Monitoring:** Messages-per-second counter broadcast to the dashboard
- **Bounded Volatility Presets:** `low`, `normal`, and `high` widen spread and increase activity without runaway 100% to 1000% price jumps in seconds
- **Binary Protocol:** Packed wire-format structs for high-throughput market data consumers

## Architecture

```text
+------------------------------------------------------------------------------+
|                              React Dashboard                                 |
|  Order Entry | PnL | Sim Controls | Ladder | Tape | Chart | System Health    |
|                    Zustand Store <- WebSocket <- /ws/market                  |
+-----------------------------------+------------------------------------------+
                                    | HTTP POST /api/orders
+-----------------------------------v------------------------------------------+
|                             OrderEntryGateway                                |
|              JSON parse -> latency stamp -> MarketRuntime::submitOrder()     |
+-----------------------------------+------------------------------------------+
                                    |
+-----------------------------------v------------------------------------------+
|                               MarketRuntime                                  |
|  Simulation loop | Environment | Agent registry | Replay | Runtime fanout     |
|  manual orders + replay + built-in agents -> shared submission path          |
+-----------------------------------+------------------------------------------+
                                    |
+-----------------------------------v------------------------------------------+
|                               EngineService                                  |
|   Engine thread (single writer) | PnL tracker | stats | sequencing           |
+-----------------------------------+------------------------------------------+
                                    |
+-----------------------------------v------------------------------------------+
|                              MatchingEngine                                  |
|      Validation -> Matching -> Book mutations -> trade/execution callbacks   |
+-----------------------------------+------------------------------------------+
                                    |
+-----------------------------------v------------------------------------------+
|                            MarketDataPublisher                               |
|       JSON /ws/market                     Binary /ws/market/bin              |
+------------------------------------------------------------------------------+
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
| **PnL Card** | Net position plus total/realized/unrealized P&L marked live to the current mid |
| **Simulation Controls** | Pause/resume, restart, clock speed, replay, volatility/regime, scenarios, agent counts, market-maker tuning |
| **Lab View** | Run instant/headless backtests, sweeps, and replay calibration, or import saved artifacts for P&L, inventory, mid/spread, toxicity, queue metrics, and agent attribution |
| **System Health** | Engine latency, throughput, connection state |
| **Mid-Price Chart** | Lightweight-charts 1s candlestick view with zoom/pan |
| **Order Book Ladder** | L2 depth, asks above and bids below |
| **Trade Tape** | Time and sales with self-trade highlighting |
| **Status Bar** | WS state, active client, trade count, volume, levels, timezone |

## Project Structure

```text
mercury/
|-- include/                    # Headers
|   |-- Order.h                 # Order, Trade, ExecutionResult types
|   |-- OrderBook.h             # Order book with Abseil ladders + intrusive FIFO levels
|   |-- MatchingEngine.h        # Price-time priority matching
|   |-- EngineService.h         # Live engine thread + telemetry
|   |-- MarketRuntime.h         # Unified simulation/runtime layer
|   |-- MarketData.h            # Market-data DTOs and sink interfaces
|   |-- BacktestReport.h        # Backtest summary and artifact writers
|   |-- BacktestRunner.h        # Shared CLI/server lab runner
|   |-- MarketDataPublisher.h   # JSON + binary WebSocket publisher
|   |-- OrderEntryGateway.h     # HTTP order entry handler
|   |-- BinaryProtocol.h        # Packed binary wire-format structs
|   |-- ServerApp.h             # Server entrypoint
|   `-- ...
|-- src/                        # Implementations
|   |-- MatchingEngine.cpp
|   |-- EngineService.cpp
|   |-- MarketRuntime.cpp
|   |-- BacktestRunner.cpp
|   |-- OrderEntryGateway.cpp
|   |-- MarketDataPublisher.cpp
|   |-- ServerApp.cpp
|   `-- main.cpp
|-- scenarios/                  # Versioned market-making lab scenarios
|-- tests/                      # Google Test suites (249 tests)
|-- benchmarks/                 # Optional benchmark target
|-- frontend/                   # React/Vite/TypeScript dashboard
|-- data/                       # Sample CSV inputs
|-- docs/                       # ARCHITECTURE.md, WORKFLOWS.md
`-- AGENTS.md                   # Agent guidance
```

## Quick Start

### Build

```powershell
cmake -B build -G Ninja
cmake --build build
```

The backend pulls third-party dependencies with CMake `FetchContent`, including Abseil for the ladder and lookup containers.

### Run The Live Stack

Terminal 1 - backend:
```powershell
.\build\mercury.exe --server --sim --host 127.0.0.1 --port 9001 --symbol SIM
```

Terminal 2 - frontend:
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

### Run An Instant Backtest

```powershell
.\build\mercury.exe --backtest --sim-seed 42 --sim-duration-ms 30000 --sim-volatility normal --backtest-output runs\baseline
```

Artifacts include `summary.json`, `config.json`, `trades.csv`, `stats.csv`, `pnl.csv`, `sim_state.csv`, `agent_metrics.csv`, and `agent_summary.csv`.

Run a preset scenario:

```powershell
.\build\mercury.exe --backtest --scenario scenarios\toxic-flow.json --backtest-output runs\toxic-flow
```

Run with a market-maker config file:

```powershell
.\build\mercury.exe --backtest --mm-config scenarios\calm-two-sided-market.json --backtest-output runs\custom-mm
```

Calibrate against replay flow:

```powershell
.\build\mercury.exe --calibrate-replay data\sample_orders_with_clients.csv --backtest-output runs\replay-calibration
```

### Run A Parameter Sweep

Create a sweep file:

```json
{
  "runs": [
    { "name": "baseline", "seed": 42, "volatility": "normal", "marketMakerCount": 2, "noiseTraderCount": 1 },
    { "name": "stressed-flow", "seed": 42, "volatility": "high", "marketMakerCount": 3, "noiseTraderCount": 4 }
  ]
}
```

Run the sweep as instant backtests:

```powershell
.\build\mercury.exe --sweep runs\sweep.json --sim-duration-ms 30000 --backtest-output runs\sweep
```

## HTTP API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/health` | GET | Server liveness and runtime status |
| `/api/state` | GET | Engine metadata, market summary, and simulation state |
| `/api/scenarios` | GET | Built-in live scenario IDs and display names |
| `/api/orders` | POST | Submit order (limit, market, cancel, modify) |
| `/api/simulation/control` | POST | Pause/resume/restart, change timing, volatility/regime, scenarios, agent counts, market-maker quoting |
| `/api/replay/control` | POST | Start or stop local CSV replay against the running simulator |
| `/api/lab/run` | POST | Run instant/headless backtests, sweeps, or replay calibration and return renderable artifacts |

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

Accelerate or return the live simulator clock to realtime:

```json
{
  "action": "set_timing",
  "clockMode": "accelerated",
  "speed": 25
}
```

Force a specific market regime (`calm`, `normal`, or `stressed`):

```json
{
  "action": "set_regime",
  "volatility": "stressed"
}
```

Start local replay flow from the browser or API:

```json
{
  "action": "start",
  "replayFile": "data\\sample_orders_with_clients.csv",
  "speed": 10,
  "loop": false,
  "loopPauseMs": 1000
}
```

Run an instant lab backtest from the browser or API:

```json
{
  "mode": "backtest",
  "name": "ui-baseline",
  "symbol": "SIM",
  "scenarioFile": "scenarios\\calm-two-sided-market.json",
  "durationMs": 30000,
  "seed": 42,
  "volatility": "normal",
  "marketMakerCount": 2,
  "momentumCount": 2,
  "meanReversionCount": 2,
  "noiseTraderCount": 1,
  "outputDir": "runs\\ui-baseline"
}
```

`mode` can be `backtest`, `headless`, `sweep`, or `calibrate_replay`. The response includes summary JSON and chart/table-ready artifacts; when `outputDir` is supplied, the normal local JSON/CSV files are written as well.

Apply a built-in live scenario:

```json
{
  "action": "apply_scenario",
  "scenario": "toxic-flow"
}
```

Tune the simulated market-maker population and quote shape:

```json
{
  "action": "set_market_maker",
  "marketMaker": {
    "levels": 4,
    "quoteQuantity": 90,
    "minQuantity": 20,
    "baseSpreadTicks": 3,
    "toxicitySensitivity": 1.2,
    "wakeIntervalMs": 80
  }
}
```

## WebSocket API

| Path | Format | Snapshot | Events |
|------|--------|----------|--------|
| `/ws/market` | JSON text | Yes, on connect | `book_delta`, `trade`, `execution`, `stats`, `pnl`, `sim_state`, `agent_metrics` |
| `/ws/market/bin` | Binary packed | No | `book_delta`, `trade` |

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

`/api/state` and `sim_state` frames expose:

- running and paused state
- clock mode and speed multiplier
- current volatility preset
- simulation timestamp
- market-maker, momentum, mean-reversion, and noise-trader agent counts
- realized volatility and average spread summaries
- toxicity score, measuring recent sweep-like flow versus displayed top-book liquidity
- current market regime (`calm`, `normal`, `stressed`) and the active Poisson arrival intensities (`limitLambda`, `cancelLambda`, `marketableLambda`), all expressed as expected events per millisecond
- live market-maker configuration and latest agent attribution metrics

### Binary Protocol

Messages on `/ws/market/bin` use packed structs from `include/BinaryProtocol.h`:

| Struct | Size | Header Type |
|--------|------|-------------|
| `BinaryBookDelta` | 61 bytes | `1` |
| `BinaryTradeEvent` | 85 bytes | `2` |

All fields are little-endian (x86/x64 host order).

## Other Runtime Modes

### File Processing

```powershell
.\build\mercury.exe data\sample_orders_with_clients.csv trades.csv executions.csv riskevents.csv pnl.csv
.\build\mercury.exe data\sample_orders_with_clients.csv --concurrent --async-io
```

### CLI Flags

| Flag | Short | Description |
|------|-------|-------------|
| `--server` | `-S` | Start HTTP/WebSocket server |
| `--sim` | | Enable the living market simulation runtime |
| `--headless` | | Run the same simulation runtime without the browser server |
| `--backtest` | | Run headless simulation as fast as possible |
| `--backtest-output <dir>` | | Write backtest summary/config/trade/stat/PnL/simulation-state artifacts |
| `--sweep <file>` | | Run multiple instant backtests from a JSON sweep file |
| `--scenario <file>` | | Apply a scenario JSON file to server, headless, backtest, or sweep base settings |
| `--mm-config <file>` | | Apply market-maker quote configuration from JSON |
| `--calibrate-replay <file>` | | Run instant replay calibration and write `calibration.json` when output is enabled |
| `--host <addr>` | | Bind address (default `127.0.0.1`) |
| `--port <port>` | `-p` | Listen port (default `9001`) |
| `--symbol <name>` | | Comma-separated list of symbols (default `SIM`) |
| `--replay <file>` | | CSV replay file |
| `--replay-speed <x>` | | Replay speed multiplier |
| `--replay-loop` | | Loop the replay file continuously |
| `--replay-loop-pause <ms>` | | Pause between replay loops |
| `--sim-speed <x>` | | Simulation clock speed multiplier |
| `--sim-seed <n>` | | Deterministic simulation seed |
| `--sim-volatility <low\|normal\|high>` | | Volatility preset |
| `--mm-count <n>` | | Passive market-maker count |
| `--mom-count <n>` | | Aggressive momentum-agent count |
| `--mr-count <n>` | | Mean-reversion-agent count |
| `--noise-count <n>` | | Poisson-flow noise-trader count |
| `--sim-duration-ms <n>` | | Bounded headless run duration in simulated milliseconds |

## Performance

Benchmarks run on 12-core CPU @ 3.6GHz (Release build):

| Operation | Latency | Throughput |
|-----------|---------|------------|
| Order Insert | 321 ns | 3.1M/sec |
| Order Match (10 levels) | 1.9 us | 526K/sec |
| Order Cancel | 2.7 us | 370K/sec |
| Market Sweep (5 levels) | 1.8 us | 556K/sec |
| **Sustained Mixed Load** | 312 ns | **3.2M/sec** |

```powershell
cmake -B build -DMERCURY_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build
.\build\mercury_benchmarks.exe
```

## Testing

249 unit tests covering:
- Order book operations (insert, remove, update)
- Matching engine (limit, market, IOC, FOK)
- Risk manager (position limits, exposure limits)
- P&L tracker (realized, unrealized, FIFO cost basis)
- Market data (sequencing, snapshots, deltas)
- Server/API contract smoke tests for state JSON, order parsing, order responses, and WebSocket envelopes
- Unified simulation runtime and agent fanout
- Instant backtest clock behavior without real-time pacing
- Backtest report metrics, agent attribution, queue analytics, Lab API JSON artifacts, and CSV escaping
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

## Future Updates

Mercury's current release is focused on local, repeatable market-making simulation. Good next steps are:

- [ ] Add optional broker or paper-trading adapters while keeping the simulator usable without external accounts.
- [ ] Add authentication, multi-user permissions, and deployment hardening for non-localhost operation.
- [ ] Add durable run storage so backtests, sweeps, calibration reports, and agent attribution can be queried across sessions.
- [ ] Package the dashboard for production use, including an option to serve the built React app from the C++ server.
- [ ] Add Python strategy loading or an external strategy sandbox with explicit safety and performance boundaries.
- [ ] Extend the binary WebSocket protocol beyond book deltas and trades if low-latency consumers need full event parity.
- [ ] Add richer scenario authoring tools for stress testing quote behavior, toxic flow, queue priority, and inventory limits.
- [ ] Add deeper replay calibration reports that compare simulated fill quality, spread capture, and adverse selection against target datasets.

## License

AGPL-3.0
