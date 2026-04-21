# Mercury

Mercury is a C++17 single-book matching engine with two active developer-facing runtimes:

- terminal and CSV processing for demos, replay, and batch outputs
- a localhost HTTP/WebSocket server that streams live market data to a separate React/Vite dashboard in `/frontend`

The core engine is still single-book and single-writer. Symbol support is only carried at the API and frontend layer in v1.

## What Is In The Repo Today

- `MatchingEngine` and `OrderBook` with price-time priority matching
- limit, market, cancel, and modify order support
- risk checks and PnL tracking
- CSV parsing, replay-style processing, and file outputs
- localhost server mode with:
  - `POST /api/orders` (handled by `OrderEntryGateway`)
  - `GET /api/health`
  - `GET /api/state`
  - `/ws/market` (JSON text frames)
  - `/ws/market/bin` (binary packed frames)
- sequenced market-data events:
  - `snapshot`
  - `book_delta` (includes `engineLatencyNs`)
  - `trade` (includes `engineLatencyNs`)
  - `stats` (includes `messagesPerSecond`)
  - `pnl`
- engine telemetry: gateway-to-engine latency (nanoseconds), throughput counter (messages/second)
- React + Vite + TypeScript frontend in `/frontend`
- Google Test backend coverage (243 tests) and Vitest frontend coverage

## Repo Layout

```text
mercury/
|-- include/            # Core headers, server headers, market-data DTOs, binary protocol
|-- src/                # Engine, service, server, gateway, publisher, CLI entry point
|-- tests/              # Google Test suites
|-- benchmarks/         # Optional benchmark target
|-- docs/               # Architecture and workflow docs
|-- frontend/           # React/Vite dashboard app
|-- data/               # Sample CSV inputs
|-- AGENTS.md           # Repo-specific agent guidance
`-- CMakeLists.txt
```

Key server-side files:

- `include/ServerHelpers.h` — shared JSON/HTTP helpers
- `include/OrderEntryGateway.h` / `src/OrderEntryGateway.cpp` — HTTP order entry handler
- `include/MarketDataPublisher.h` / `src/MarketDataPublisher.cpp` — WebSocket publisher
- `include/BinaryProtocol.h` — packed binary wire-format structs

Start with [AGENTS.md](AGENTS.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and [docs/WORKFLOWS.md](docs/WORKFLOWS.md) before making large changes.

## Backend Build

Mercury uses CMake `FetchContent` for its C++ dependencies. With server mode enabled, configure pulls:

- `googletest`
- `nlohmann_json`
- `libuv`
- `uWebSockets`
- `uSockets`

`MERCURY_BUILD_SERVER` is `ON` by default. The Windows MinGW path includes an automatic `libuv` source patch during configure so the fetched dependency builds in this environment.

### Windows Or Cross-Platform Single-Config Build

```powershell
cmake -B build -G Ninja
cmake --build build
```

### Visual Studio Generator

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Disable Server Mode

```powershell
cmake -B build -G Ninja -DMERCURY_BUILD_SERVER=OFF
cmake --build build
```

## Running The Live Dashboard

The backend and frontend are separate processes in v1. Static asset serving from C++ is intentionally deferred.

### 1. Start The Backend Server

```powershell
.\build\mercury.exe --server --host 127.0.0.1 --port 9001 --symbol SIM
```

Optional replay:

```powershell
.\build\mercury.exe --server --host 127.0.0.1 --port 9001 --symbol SIM --replay data\sample_orders_with_clients.csv --replay-speed 10
```

### 2. Start The Frontend Dev Server

```powershell
Set-Location frontend
npm install
npm run dev
```

Vite proxies `/api` and `/ws` to `127.0.0.1:9001`. Open the local URL shown by Vite, typically `http://127.0.0.1:5173`.

## HTTP API

### `GET /api/health`

Returns basic server liveness:

```json
{
  "status": "ok",
  "running": true,
  "replayActive": false
}
```

### `GET /api/state`

Returns engine and connection metadata:

```json
{
  "running": true,
  "replayActive": false,
  "symbol": "SIM",
  "sequence": 12,
  "nextOrderId": 7,
  "tradeCount": 1,
  "totalVolume": 10,
  "orderCount": 2,
  "bidLevels": 1,
  "askLevels": 1,
  "clientCount": 2,
  "connections": 1
}
```

### `POST /api/orders`

Handled by `OrderEntryGateway`. The gateway parses JSON, submits to the engine thread synchronously, and returns the execution result.

Supported request types:

- `limit`
- `market`
- `cancel`
- `modify`

Example limit order:

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

Example response:

```json
{
  "submittedOrderId": 1,
  "orderType": "limit",
  "side": "buy",
  "tif": "GTC",
  "status": "resting",
  "rejectReason": "None",
  "orderId": 1,
  "filledQuantity": 0,
  "remainingQuantity": 10,
  "message": "",
  "trades": []
}
```

## WebSocket API

### JSON Path — `/ws/market`

Connect to `/ws/market` for JSON text frames.

Connection behavior:

1. Server sends one `snapshot` immediately after connect.
2. Server then streams `book_delta`, `trade`, `stats`, and `pnl` envelopes.
3. Clients can send a subscribe message to request a different depth.

Subscribe message:

```json
{
  "type": "subscribe",
  "depth": 20
}
```

Depth defaults to `20` and is clamped to `1..100`.

Envelope shape:

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

Telemetry fields in payloads:

- `book_delta.payload.engineLatencyNs` — gateway-to-mutation latency in nanoseconds (0 for replay orders)
- `trade.payload.engineLatencyNs` — gateway-to-trade latency in nanoseconds (0 for replay orders)
- `stats.payload.messagesPerSecond` — engine-thread throughput sampled every ~1 second

The frontend uses `sequence` to ignore stale frames and detect gaps that require a resync.

### Binary Path — `/ws/market/bin`

Connect to `/ws/market/bin` for packed binary frames (book deltas and trades only).

- No snapshot on connect. Binary clients should use `GET /api/state` or the JSON WebSocket for initial state.
- Message format is defined by packed structs in `include/BinaryProtocol.h`.
- Read `BinaryHeader.type` (1 = book_delta, 2 = trade) to determine the struct layout.
- All fields are little-endian (host-order on x86/x64).

## Frontend Dashboard

The React dashboard (`/frontend`) provides:

- **Top Bar** — symbol, mid-price, spread, connection status, clock
- **Stats Strip** — bid, ask, mid, spread, spread bps, trades, volume, orders, bid/ask levels
- **Order Entry Form** — limit/market/cancel/modify with buy/sell toggle, price, qty, clientId, loading state
- **Position / PnL Card** — net position, total PnL, realized, unrealized (green/red coloring)
- **System Health Card** — engine latency (color-coded), throughput (msg/s), connection indicator
- **Mid-Price Chart** — lightweight-charts line chart with delta tracking
- **Order Book Ladder** — L2 depth with asks (red) above, spread marker, bids (green) below, cumulative size bars
- **Trade Tape** — time & sales with uptick/downtick arrows, value column, self-trade highlighting ("You" badge)
- **Status Bar** — WS state, active client, trade count, volume, bid/ask levels, timezone, version

## Other Runtime Modes

### File Processing

```powershell
.\build\mercury.exe data\sample_orders.csv trades.csv executions.csv riskevents.csv pnl.csv
.\build\mercury.exe data\sample_orders.csv --concurrent --async-io
```

### Strategy Demos

```powershell
.\build\mercury.exe --strategies
```

### Backtests

```powershell
.\build\mercury.exe --backtest
.\build\mercury.exe --backtest mm
.\build\mercury.exe --backtest momentum
.\build\mercury.exe --backtest multi
.\build\mercury.exe --backtest compare
.\build\mercury.exe --backtest stress
```

## Testing

### Backend

```powershell
ctest --test-dir build --output-on-failure
```

### Frontend

```powershell
Set-Location frontend
npm run test:run
npm run build
```

The current backend suite (243 tests) includes market-data coverage for top-of-book extraction, event sequencing, and engine-service publication flow. The frontend suite covers snapshot and delta application in the Zustand store.

## Current V1 Boundaries

- localhost only, no auth
- single book in core engine
- JSON primary transport, binary secondary transport for throughput-sensitive consumers
- browser writes go over HTTP, not WebSocket
- frontend is a separate dev app, not served by the C++ binary

## License

AGPL-3.0
