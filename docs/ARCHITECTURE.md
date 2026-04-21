# Mercury Architecture

This document describes the architecture that is present in the repository today.

## Build Topology

`CMakeLists.txt` defines the backend build graph:

- `mercury`: executable with terminal, file-processing, unified simulation, headless, replay, and server modes
- `mercury_lib`: core library used by tests and benchmarks
- `mercury_tests`: Google Test binary linked against `mercury_lib`
- `mercury_benchmarks`: optional benchmark binary when `MERCURY_BUILD_BENCHMARKS=ON`

When `MERCURY_BUILD_SERVER=ON`, the configure step also fetches and wires:

- `abseil-cpp`
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

`src/main.cpp` now centers runtime behavior on one market simulation stack:

1. Interactive demo mode when no input file or major mode flag is provided
2. CSV file-processing mode for batch execution and output files
3. Unified simulation runtime via `--sim`
4. Local server mode via `--server`
5. Headless accelerated simulation via `--sim --headless`

Simulation and server flags:

- `--server`
- `--sim`
- `--headless`
- `--host <host>`
- `--port <port>`
- `--symbol <name>`
- `--replay <file>`
- `--replay-speed <x>`
- `--sim-speed <x>`
- `--sim-seed <n>`
- `--sim-volatility <low|normal|high>`
- `--mm-count <n>`
- `--mom-count <n>`
- `--mr-count <n>`
- `--sim-duration-ms <n>`

Legacy `--strategies` and `--backtest` flags still exist for migration safety, but the intended architecture is the unified runtime above the engine.

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

- bid and ask ladders are maintained with `absl::btree_map`
- per-price FIFO queues are implemented with intrusive nodes
- order lookup is O(1) average through a repository `HashMap` wrapper backed by `absl::flat_hash_map`
- node allocation is pooled through `ObjectPool`

Why this split:

- `absl::btree_map` preserves sorted best-price traversal without the pointer-heavy red-black tree layout of `std::map`
- intrusive per-price queues preserve O(1) FIFO insert/remove and direct node ownership
- the `HashMap` wrapper keeps existing call sites and tests stable while allowing the backing hash container to change

Market-data support:

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
- emit execution callbacks
- emit explicit post-mutation book callbacks for market-data deltas

The market-data design does not infer all book changes from trade callbacks. Deltas are emitted from explicit book-affecting points in the matching flow.

## Live Server And Simulation Architecture

### MarketRuntime

Relevant files:

- `include/MarketRuntime.h`
- `src/MarketRuntime.cpp`

`MarketRuntime` is the service layer above `EngineService`.

Responsibilities:

- own `EngineService`
- own the simulation clock and environment state
- own the built-in agent registry
- route manual orders, replay orders, and simulated-agent flow into the same submission path
- maintain runtime metrics such as realized volatility and average spread
- fan out engine and simulation events to multiple subscribers

Design constraints:

- simulation runs on one dedicated runtime thread, not one thread per bot
- `EngineService` remains the only mutation path into `MatchingEngine`
- the core engine remains single-book and single-symbol
- custom v1 strategies are in-process C++ `SimulationAgent` implementations

### Simulation Environment And Agents

The simulation world is intentionally separated from trading logic.

Environment state currently includes:

- latent fair value
- volatility preset and regime parameters
- momentum-burst state
- simulation timestamp
- realized volatility and average spread summaries

Current v1 environment dynamics are intentionally bounded:

- latent fair value evolves as a noisy process with pullback toward the observed market
- momentum bursts are short-lived regime shifts, not unbounded trend generators
- volatility presets change spread, wake cadence, burst behavior, and order sizing without targeting extreme multi-second price explosions

Built-in agent personalities:

- passive market maker
- aggressive momentum trader
- mean-reversion bot

Agents consume:

- the current L2 snapshot
- recent trades
- current stats
- their own live orders, position, and PnL
- an environment view that exposes allowed scenario state

Agents emit engine-agnostic intents:

- place limit order
- place marketable order
- cancel order
- modify order
- no-op

Behavior notes:

- passive market makers are expected to keep the book two-sided even under inventory pressure
- aggressive momentum agents are capped so they stress liquidity without dominating price formation by themselves
- mean-reversion agents and fair-value pullback act as stabilizers after overshoots

### EngineService

Relevant files:

- `include/EngineService.h`
- `src/EngineService.cpp`

`EngineService` is the live service boundary around the single-book engine.

Responsibilities:

- own the live `MatchingEngine`
- own `PnLTracker`
- own the monotonically increasing outbound `sequence`
- serialize all mutations on one dedicated engine thread
- produce L2 snapshots, deltas, trades, executions, stats, and PnL events on that engine thread

Design constraints:

- browser order POSTs enqueue through `MarketRuntime`
- replay orders enqueue through `MarketRuntime`
- simulated-agent orders enqueue through `MarketRuntime`
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
- `ExecutionEvent`
- `StatsEvent`
- `PnLEvent`
- `SimulationStateEvent`

Every outbound event carries:

- `type`
- `sequence`
- `symbol`
- `payload`

Telemetry fields:

- `BookDelta.engineLatencyNs`: nanoseconds from gateway entry to book mutation
- `TradeEvent.engineLatencyNs`: nanoseconds from gateway entry to trade generation
- `StatsEvent.messagesPerSecond`: engine-thread throughput sampled every ~1 second

Sequence semantics:

- a single monotonically increasing sequence spans `book_delta`, `trade`, `execution`, `stats`, `pnl`, and `sim_state`
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
- `POST /api/simulation/control`

WebSocket endpoints:

- `/ws/market`: JSON text frames, snapshot on connect, then `book_delta`, `trade`, `execution`, `stats`, `pnl`, and `sim_state`
- `/ws/market/bin`: binary frames using packed structs from `BinaryProtocol.h`

Current behavior:

- binds to `127.0.0.1` by default
- accepts browser order entry over HTTP only
- keeps WebSocket read-only except for subscribe messages that request depth
- JSON path sends one `snapshot` on connect and then live runtime events
- binary path sends only `book_delta` and `trade` as packed structs

### MarketDataPublisher

`MarketDataPublisher` implements `MarketDataSink` and bridges engine-thread publication to the uWebSockets event loop.

Important thread-handoff rule:

- engine or runtime thread calls the sink
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

All fields are little-endian (host order on x86/x64).

## Frontend Architecture

Relevant frontend areas:

- `frontend/src/store/market-data-store.ts`
- `frontend/src/hooks/use-market-data-websocket.ts`
- `frontend/src/components/`
- `frontend/src/lib/types.ts`
- `frontend/src/App.tsx`

State model:

- one live store keyed by symbol
- tracked fields include sequence, bids, asks, trades, stats, connection state, per-client PnL, engine latency, throughput, and simulation state
- submitted order IDs are tracked in a `Set<number>` for self-trade detection in the trade tape

UI layout:

- Top Bar: system status, spread, mid, top-of-book, connection state, clock
- Stats Strip: bid, ask, mid, spread, spread bps, trades, volume, orders, bid/ask levels
- Left column: order entry form, PnL card, simulation controls, system health card
- Center column: mid-price chart above, L2 order book ladder below
- Right column: scrolling trade tape with uptick/downtick and self-trade highlighting
- Status Bar: WS state, active client, trade count, volume, bid/ask levels, timezone, version

Client behavior:

- initial WebSocket snapshot seeds the store
- subsequent deltas update only affected levels
- sequence gaps trigger resync logic instead of blindly applying out-of-order frames
- `execution` envelopes are accepted as sequencing events without changing book state
- `sim_state` updates drive the operator controls and runtime status badges
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
- WebSocket for read-only market data
- separate Vite app for frontend development
- browser remains a thin operator client rather than the owner of the market loop

Do not collapse these boundaries casually during unrelated work.

## Tests As Behavioral Spec

High-signal backend suites:

- `tests/order_book_test.cpp`
- `tests/matching_engine_test.cpp`
- `tests/market_data_test.cpp`
- `tests/simulation_runtime_test.cpp`
- `tests/order_validation_test.cpp`
- `tests/risk_manager_test.cpp`
- `tests/pnl_tracker_test.cpp`
- `tests/stress_test.cpp`

High-signal frontend test:

- `frontend/src/store/market-data-store.test.ts`

For market-runtime or dashboard changes, treat both backend and frontend tests as the behavioral spec for the live stack.

`tests/simulation_runtime_test.cpp` now also covers:

- long-run two-sided book maintenance
- continued trading over time
- bounded volatility excursion under deterministic accelerated simulation
