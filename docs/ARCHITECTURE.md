# Mercury Architecture

This document describes the architecture that is present in the repository today.

## Build Topology

`CMakeLists.txt` defines the backend build graph:

- `mercury`: executable with terminal, file-processing, replay, and server modes
- `mercury_lib`: core library used by tests and benchmarks
- `mercury_tests`: Google Test binary linked against `mercury_lib`
- `mercury_benchmarks`: optional benchmark binary when `MERCURY_BUILD_BENCHMARKS=ON`

When `MERCURY_BUILD_SERVER=ON`, the configure step also fetches and wires:

- `nlohmann_json`
- `libuv`
- `uWebSockets`
- `uSockets`

The browser UI is a separate app in `frontend/`:

- Vite dev server
- React + TypeScript
- Zustand store
- Tailwind and local shadcn-style UI primitives
- Lightweight Charts for the small price panel

The frontend is not served from the C++ executable in v1.

## Runtime Modes

`src/main.cpp` currently supports four distinct runtime paths:

1. Interactive demo mode when no input file or major mode flag is provided
2. CSV file-processing mode for batch execution and output files
3. Strategy demo mode via `--strategies`
4. Local server mode via `--server`

Server-mode flags:

- `--server`
- `--host <host>`
- `--port <port>`
- `--symbol <name>`
- `--replay <file>`
- `--replay-speed <x>`

## Core Engine Layer

### Order Model

Key types live in `include/Order.h`:

- `Order`
- `Trade`
- `ExecutionResult`
- enums for type, side, TIF, execution status, and reject reasons

The core engine remains single-book in v1. Symbol is not carried inside `Order`.

### Order Book

Relevant files:

- `include/OrderBook.h`
- `include/OrderNode.h`
- `include/PriceLevel.h`
- `include/HashMap.h`
- `include/IntrusiveList.h`
- `include/ObjectPool.h`

Key design choices:

- bid and ask ladders are maintained with `std::map`
- per-price FIFO queues are implemented with intrusive nodes
- order lookup is O(1) average through the custom hash map
- node allocation is pooled through `ObjectPool`

Market-data support added in v1:

- `OrderBook::getTopLevels(side, depth)` extracts only the requested L2 depth
- best-bid, best-ask, spread, and mid-price helpers are used directly for snapshot and stats publication

### Matching Engine

Relevant files:

- `include/MatchingEngine.h`
- `src/MatchingEngine.cpp`

Responsibilities:

- validate orders and map rejects
- execute price-time priority matching
- maintain trade counts and total volume
- support limit, market, cancel, and modify flows
- emit trade callbacks
- emit explicit post-mutation book callbacks for market-data deltas

The market-data design does not infer all book changes from trade callbacks. Deltas are emitted from explicit book-affecting points in the matching flow.

## Live Server Architecture

### EngineService

Relevant files:

- `include/EngineService.h`
- `src/EngineService.cpp`

`EngineService` is the live service boundary around the single-book engine.

Responsibilities:

- own the live `MatchingEngine`
- own `PnLTracker`
- own replay execution
- own the monotonically increasing outbound `sequence`
- serialize all mutations on one dedicated engine thread
- produce L2 snapshots, deltas, trades, stats, and PnL events on that engine thread

Design constraints:

- browser order POSTs enqueue onto the engine thread
- replay orders enqueue onto the same engine thread
- WebSocket threads never read the order book directly
- snapshot extraction is synchronous on the engine thread

### Market-Data DTOs

Relevant file:

- `include/MarketData.h`

Internal DTOs:

- `BookLevel`
- `L2Snapshot`
- `BookDelta`
- `TradeEvent`
- `StatsEvent`
- `PnLEvent`

Every outbound event carries:

- `type`
- `sequence`
- `symbol`
- `payload`

Telemetry fields added in Phase 6:

- `BookDelta.engineLatencyNs`: nanoseconds from gateway entry to book mutation (0 for replay or non-gateway orders)
- `TradeEvent.engineLatencyNs`: nanoseconds from gateway entry to trade generation
- `StatsEvent.messagesPerSecond`: engine-thread throughput sampled every ~1 second

Sequence semantics:

- a single monotonically increasing sequence spans `book_delta`, `trade`, `stats`, and `pnl`
- snapshots use the current engine sequence at extraction time
- frontend consumers use sequence to reject stale frames and detect resync conditions

### ServerApp

Relevant files:

- `include/ServerApp.h`
- `src/ServerApp.cpp`

`ServerApp` hosts the localhost API surface.

HTTP endpoints:

- `GET /api/health`
- `GET /api/state`
- `POST /api/orders`

WebSocket endpoints:

- `/ws/market` — JSON text frames (snapshot on connect, then deltas/trades/stats/pnl)
- `/ws/market/bin` — binary frames using packed structs from `BinaryProtocol.h` (no snapshot on connect; binary clients should use JSON WebSocket or `GET /api/state` for initial state)

Current behavior:

- binds to `127.0.0.1` by default
- accepts browser order entry over HTTP only
- keeps WebSocket read-only except for subscribe messages that request depth
- JSON path sends one `snapshot` on connect, then publishes `book_delta`, `trade`, `stats`, and `pnl`
- binary path sends only `book_delta` and `trade` as packed structs

### MarketDataPublisher

`MarketDataPublisher` implements `MarketDataSink` and bridges engine-thread publication to the uWebSockets event loop.

Important thread-handoff rule:

- engine thread calls the sink
- publisher uses `uWS::Loop::defer(...)` before broadcasting
- uWebSockets app thread performs the actual publish

That separation is what keeps WebSocket threads from touching the order book.

### Binary Protocol

Relevant file:

- `include/BinaryProtocol.h`

Fixed-width `#pragma pack(push, 1)` structs for wire-efficient streaming:

- `BinaryHeader` (4 bytes): type (1=book_delta, 2=trade), reserved, length
- `BinaryBookDelta` (53 bytes): header + sequence, side, price, quantity, orderCount, action, timestamp, engineLatencyNs
- `BinaryTradeEvent` (77 bytes): header + sequence, tradeId, price, quantity, orderIds, clientIds, timestamp, engineLatencyNs

All fields are little-endian (host-order on x86/x64).

## Frontend Architecture

Relevant frontend areas:

- `frontend/src/store/market-data-store.ts`
- `frontend/src/hooks/use-market-data-websocket.ts`
- `frontend/src/components/`
- `frontend/src/lib/types.ts`
- `frontend/src/App.tsx`

State model:

- one live store keyed by symbol
- tracked fields include sequence, bids, asks, trades, stats, connection state, per-client PnL, engine latency, and throughput
- submitted order IDs are tracked in a `Set<number>` for self-trade detection in the trade tape

UI layout:

- **Top Bar**: system status, spread, mid, top-of-book, connection state, clock
- **Stats Strip**: bid, ask, mid, spread, spread bps, trades, volume, orders, bid/ask levels
- **Left column**: order entry form, PnL card, system health card
- **Center column**: mid-price chart (lightweight-charts) above, L2 order book ladder below
- **Right column**: scrolling trade tape with uptick/downtick, self-trade highlighting ("You" badge)
- **Status Bar**: WS state, active client, trade count, volume, bid/ask levels, timezone, version

Client behavior:

- initial WebSocket snapshot seeds the store
- subsequent deltas update only affected levels
- sequence gaps trigger resync logic instead of blindly applying out-of-order frames
- order entry uses `POST /api/orders` with an editable `clientId`
- submitted order IDs are tracked so the trade tape can highlight the user's own fills
- `engineLatencyNs` from `book_delta` and `trade` payloads feeds the System Health card
- `messagesPerSecond` from `stats` payloads feeds the System Health card

## Contract Boundaries

The current v1 boundaries are intentional:

- single-book core engine
- symbol only at API and frontend layer
- JSON primary transport, binary secondary transport for throughput-sensitive consumers
- HTTP for order submission
- WebSocket for read-only market data (JSON and binary paths)
- separate Vite app for frontend development

Do not collapse these boundaries casually during unrelated work.

## Tests As Behavioral Spec

High-signal backend suites:

- `tests/order_book_test.cpp`
- `tests/matching_engine_test.cpp`
- `tests/market_data_test.cpp`
- `tests/order_validation_test.cpp`
- `tests/risk_manager_test.cpp`
- `tests/pnl_tracker_test.cpp`
- `tests/stress_test.cpp`

High-signal frontend test:

- `frontend/src/store/market-data-store.test.ts`

For market-data or dashboard changes, treat both backend and frontend tests as the behavioral spec for the live stack.
