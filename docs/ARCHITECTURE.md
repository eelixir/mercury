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
- `--noise-count <n>` (Poisson-flow noise traders)
- `--sim-duration-ms <n>`

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
- the core engine layer remains single-book, with multi-instrument support managed by a registry in EngineService
- custom v1 strategies are in-process C++ `SimulationAgent` implementations

### Simulation Environment And Agents

The simulation world is intentionally separated from trading logic.

Environment state currently includes:

- latent fair value
- volatility preset and regime parameters
- momentum-burst state
- toxicity score, derived from recent sweep-like flow versus displayed top-book liquidity
- simulation timestamp
- realized volatility and average spread summaries
- a per-symbol `RegimeManager` exposing the current regime label and the three arrival intensities

Current v1 environment dynamics are intentionally bounded:

- latent fair value evolves as a noisy process with pullback toward the observed market
- momentum bursts are short-lived regime shifts, not unbounded trend generators
- volatility presets change spread, wake cadence, burst behavior, and order sizing without targeting extreme multi-second price explosions

Built-in agent personalities:

- passive market maker
- aggressive momentum trader
- mean-reversion bot
- Poisson-flow noise trader (rate-driven resting, cancel, and marketable flow)

Agents consume:

- the current L2 snapshot
- recent trades
- current stats
- their own live orders, position, and PnL
- their live orders' FIFO queue position, quantity ahead, touch status, and estimated fill probability
- an environment view that exposes allowed scenario state, including the current regime label, arrival intensities, and order-size dispersion

Agents emit engine-agnostic intents:

- place limit order
- place marketable order
- cancel order
- modify order
- no-op

Behavior notes:

- passive market makers are expected to keep the book two-sided even under inventory pressure
- passive market makers quote multiple levels per side, reduce size with depth, and widen away from the touch as toxicity rises
- aggressive momentum agents are capped so they stress liquidity without dominating price formation by themselves
- mean-reversion agents and fair-value pullback act as stabilizers after overshoots

### Regime Manager And Arrival Intensities

Relevant files:

- `include/RegimeManager.h`
- `src/RegimeManager.cpp`

`RegimeManager` replaces generic "volatility" knobs with explicit control of market micro-dynamics. Each `PerSymbolEnvironment` owns one `RegimeManager` instance.

Three independent Poisson λ values govern order arrivals, expressed as expected events per millisecond:

- `limitLambda` drives new resting orders
- `cancelLambda` drives removals of existing orders
- `marketableLambda` drives orders crossing the spread

Base intensities come from the active volatility preset. They are then scaled by the current `MarketRegime`:

- `Calm`: thinner tails, sparser crossings, slightly faster passive posting
- `Normal`: preset baseline, no scaling applied
- `Stressed`: `limitLambda` ×0.5, `cancelLambda` ×2.0, `marketableLambda` ×2.0, fatter Pareto tail for order sizes

The regime is auto-detected on every simulation tick from realized volatility and momentum-burst state, with hysteresis so single-tick spikes do not trigger transitions. Manual overrides via `forceRegime()` hold the target regime for a short dwell window before auto-detection resumes.

Order sizes are drawn from a Pareto (power-law) distribution so flow mixes frequent small "retail" prints with rare large "whale" trades. The exponent tightens in Calm and loosens in Stressed. Both the intensities and the dispersion are surfaced on `SimulationEnvironmentView` so custom agents can sample from them without holding a reference to the manager.

### PoissonFlowAgent

`PoissonFlowAgent` is the noise-trader personality that consumes the regime's arrival intensities. On each wake it samples, for each arrival type, a Poisson event count across the elapsed interval and emits that many intents:

- cancels pick uniformly among the agent's own resting orders
- marketable IOC orders fire on either side when opposing liquidity exists
- limit orders rest near top-of-book with a small random book-offset

Sizes come from `RegimeManager::sampleOrderSize`. The agent carries its own RNG seeded deterministically from `(simulation seed, symbol index, agent index)` so reproducibility is preserved across runs.

### EngineService

Relevant files:

- `include/EngineService.h`
- `src/EngineService.cpp`

`EngineService` is the live service boundary around a registry of `InstrumentBook` instances.

Responsibilities:

- own the registry of live `MatchingEngine` instances keyed by symbol
- own `PnLTracker` instances for each symbol
- own the monotonically increasing outbound `sequence`
- serialize all mutations across all symbols on one dedicated engine thread
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

`SimulationStateEvent` also carries simulation microstructure state such as regime intensities and `toxicityScore`, so the dashboard and external clients can distinguish normal volatility from adverse-selection pressure.

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
- `BinaryBookDelta` (61 bytes): header + sequence, symbol (8 bytes), side, price, quantity, orderCount, action, timestamp, engineLatencyNs
- `BinaryTradeEvent` (85 bytes): header + sequence, symbol (8 bytes), tradeId, price, quantity, orderIds, clientIds, timestamp, engineLatencyNs

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
- incoming WebSocket frames are batched to animation frames before store application so high-rate streams do not force hundreds of React renders per second
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
- `tests/regime_manager_test.cpp`
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

`tests/regime_manager_test.cpp` covers:

- regime-scaled arrival intensities (Calm, Normal, Stressed) against the documented multipliers
- auto-detection into Stressed on sustained realized volatility, and Calm on a quiet tape
- `forceRegime` hold-down keeping a manual override stable against short observations
- Pareto order-size dispersion producing whale-vs-retail distributions within expected tail bounds
- `samplePoissonCount` matching the expected mean across many draws
- runtime integration: `MarketRuntimeState` publishes regime label + λ values, and enabling noise traders measurably increases traded volume
