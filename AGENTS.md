# Mercury Agent Guide

This file is the working contract for Codex and other coding agents operating in this repository.

## Goals

- Preserve matching correctness before optimizing for speed.
- Keep the engine single-book and single-writer unless the task explicitly changes that architecture.
- Keep backend and frontend contracts aligned when the HTTP or WebSocket surface changes.
- Keep docs synchronized with the code that actually builds and runs.

## Repository Snapshot

Mercury is now a mixed C++ and TypeScript repository.

Current backend outputs:

- `mercury`: demo, file-processing, unified simulation, headless, replay, and localhost server executable
- `mercury_lib`: core library used by tests and benchmarks
- `mercury_tests`: Google Test suite
- `mercury_benchmarks`: optional Google Benchmark target

Current frontend app:

- `frontend/`: React + Vite + TypeScript dashboard for the live market-data view

Primary code areas:

- `include/`: core headers, market-data DTOs, market-runtime, engine-service, server interfaces, binary protocol
- `src/`: matching engine, service layer, unified runtime, server runtime, order-entry gateway, publisher, CLI entry point
- `tests/`: backend correctness and sequencing coverage
- `frontend/src/`: browser UI, Zustand store, WebSocket handling, order entry, simulation controls, system health
- `docs/`: architecture and workflow docs

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/WORKFLOWS.md](docs/WORKFLOWS.md) before making large changes.

## Source Of Truth

When docs, comments, and implementation disagree, trust in this order:

1. `CMakeLists.txt`
2. `src/` and `include/`
3. `frontend/src/`
4. backend and frontend tests
5. `docs/`
6. `README.md`

Do not assume an older README claim still matches the build. Verify it in code.

## Architecture Constraints To Preserve

- `MatchingEngine` remains the single-book core.
- `MarketRuntime` owns replay, agents, environment state, and simulation-thread orchestration.
- `EngineService` owns live engine mutation on a dedicated engine thread.
- `OrderEntryGateway` handles HTTP order parsing and delegates synchronously to `MarketRuntime`.
- WebSocket threads do not read the order book directly.
- Market-data snapshots and deltas are produced on the engine thread after mutation.
- WebSocket publishing is deferred onto the uWebSockets loop.
- Telemetry (latency, MPS) is computed on the engine thread using out-of-band state, not by modifying `Order`.
- The system supports multiple symbols via an EngineService registry. The core `Order` and `OrderBook` remain single-instrument primitives. Do not push symbol into core order types unless explicitly required.
- Preserve the current order-book storage split unless the task explicitly justifies changing it: `absl::btree_map` price ladders, Abseil-backed `HashMap` lookups, and intrusive FIFO queues within each level.
- Volatility presets should change participation, spread, and short-term noise without causing runaway price drift over a few seconds.
- Queue-position-aware agents must source queue index and quantity-ahead from the actual intrusive FIFO `PriceLevel`, not from copied L2 aggregates.
- Toxicity/adverse-selection metrics should widen spreads and change participation pressure without becoming an unbounded trend generator.

## Working Rules For Agents

- Inspect affected headers, implementations, tests, and frontend consumers before editing an API contract.
- For behavioral changes, update tests in the same pass.
- Keep changes narrow. Avoid speculative framework work.
- Do not hand-edit generated directories such as `build/`, `build/_deps/`, `frontend/dist/`, or `frontend/node_modules/`.
- Treat `src/MatchingEngine.cpp`, `include/OrderBook.h`, `src/MarketRuntime.cpp`, `src/EngineService.cpp`, and `src/ServerApp.cpp` as correctness-sensitive.
- Treat simulation parameter changes as behavior changes that require regression coverage, not just visual spot checks.
- Keep market-data envelopes stable unless the task is explicitly an API-version change.
- If you change HTTP or WebSocket payloads, update both docs and frontend store handling in the same pass.
- If you change CLI flags or runtime modes, update `README.md`, `docs/ARCHITECTURE.md`, and `docs/WORKFLOWS.md`.
- Preserve the current `README.md` showcase style: keep the header image, quote-style intro, richer section layout, and polished formatting unless the user explicitly asks for a different presentation.

## Change Priorities

1. Matching correctness and execution semantics
2. Order-book invariants and data-structure safety
3. Market-data sequencing and snapshot/delta correctness
4. Risk and PnL correctness
5. Concurrency and thread-handoff safety
6. Frontend state consistency
7. Benchmark and throughput improvements
8. Documentation polish

## Code Style Expectations

- Standard: C++17 in backend, TypeScript in frontend
- Favor clear local logic over clever abstractions
- Keep hot paths readable and allocation-aware
- Follow nearby naming and layout conventions
- Add comments only when intent would otherwise be hard to infer
- Do not introduce unnecessary framework churn in the frontend

## Build And Test Commands

### Backend Build

```powershell
cmake -B build -G Ninja
cmake --build build
```

### Backend Tests

```powershell
ctest --test-dir build --output-on-failure
```

### Frontend Setup And Verification

```powershell
Set-Location frontend
npm install
npm run test:run
npm run build
```

### Run The Live Stack

Backend:

```powershell
.\build\mercury.exe --server --sim --host 127.0.0.1 --port 9001 --symbol SIM,AAPL,GOOG
```

Frontend:

```powershell
Set-Location frontend
npm run dev
```

## Expected Workflow

- Reproduce the issue or identify the affected contract.
- Read the closest backend and frontend tests first when the live dashboard is involved.
- Make the smallest coherent change.
- Run targeted validation for the touched subsystem.
- Run broader backend and frontend verification if shared interfaces changed.
- Update docs in the same pass when runtime behavior, API shape, or developer commands changed.

See [docs/WORKFLOWS.md](docs/WORKFLOWS.md) for concrete checklists.

## High-Risk Areas

- `include/OrderBook.h`: resting-order ownership, lookup consistency, price-level maintenance
- `src/MatchingEngine.cpp`: matching semantics, TIF behavior, callbacks, modify/cancel logic
- `src/MarketRuntime.cpp`: simulation-thread scheduling, agent behavior, runtime fanout, control-state transitions
- `src/MarketRuntime.cpp`: volatility tuning, fair-value evolution, and burst dynamics can easily create unrealistic price paths
- `src/EngineService.cpp`: engine-thread serialization, sequencing, stats/PnL/telemetry publication
- `src/OrderEntryGateway.cpp`: request parsing, latency stamping, synchronous engine roundtrip
- `src/ServerApp.cpp`: HTTP/WS route registration, JSON and binary WebSocket lifecycle, publish handoff
- `src/MarketDataPublisher.cpp`: JSON and binary serialization, topic routing
- `frontend/src/store/market-data-store.ts`: sequence handling, telemetry tracking, simulation-state handling, and state application
- `frontend/src/hooks/use-market-data-websocket.ts`: reconnect and resync behavior

## Documentation Maintenance Rule

Any agent changing one of the following must update the relevant docs in the same pass:

- build targets or build options
- public CLI commands
- HTTP or WebSocket payloads
- major subsystem responsibilities
- frontend/backend runtime workflow
- documented matching behavior

At minimum, keep these files in sync:

- `AGENTS.md`
- `docs/ARCHITECTURE.md`
- `docs/WORKFLOWS.md`
- `README.md` when user-facing behavior changes

README-specific style rule:

- keep the README in its richer showcase format rather than flattening it into a minimal plain-text engineering document
- preserve the top image and high-signal presentation structure unless the user asks for a redesign
