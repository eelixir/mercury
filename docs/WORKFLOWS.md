# Mercury Development Workflows

This file gives contributors a repeatable way to make safe changes in the current backend and frontend stack.

## 1. Changing Matching Behavior

Use this path for limit, market, cancel, modify, TIF, and trade-generation changes.

Read first:

- `include/Order.h`
- `include/OrderBook.h`
- `include/MatchingEngine.h`
- `src/MatchingEngine.cpp`
- `tests/matching_engine_test.cpp`

Required checks:

- duplicate order handling
- price-time priority
- partial-fill accounting
- remaining quantity reporting
- cancel and modify semantics
- IOC and FOK behavior
- trade callback side effects

Minimum validation:

- `ctest --test-dir build --output-on-failure`
- or targeted `mercury_tests --gtest_filter=MatchingEngineTest.*` if you are working directly with the test binary

## 2. Changing Market Data, Server, Or Replay

Use this path for `EngineService`, `ServerApp`, snapshots, deltas, sequence handling, or replay flow.

Read first:

- `include/MarketData.h`
- `include/EngineService.h`
- `include/ServerApp.h`
- `src/EngineService.cpp`
- `src/ServerApp.cpp`
- `tests/market_data_test.cpp`
- `frontend/src/store/market-data-store.ts`

Required checks:

- all mutations still route through the engine thread
- WebSocket code still does not read the order book directly
- sequence numbers remain monotonic across mixed event types
- snapshot depth remains clamped to `1..100`
- replay and HTTP orders still feed the same engine path
- frontend store still understands the emitted envelope shape

Minimum validation:

```powershell
ctest --test-dir build --output-on-failure
Set-Location frontend
npm run test:run
```

Recommended manual smoke test:

1. Start the server with `.\build\mercury.exe --server --port 9001`.
2. Start the frontend with `npm run dev` inside `frontend/`.
3. Submit a buy and sell order from the browser or with `POST /api/orders`.
4. Confirm the ladder, trade tape, stats, and PnL update.
5. Refresh the browser and confirm the UI resyncs from a fresh snapshot.

## 3. Changing Frontend Dashboard Behavior

Use this path for layout, store logic, order entry, or WebSocket handling.

Read first:

- `frontend/src/App.tsx`
- `frontend/src/store/market-data-store.ts`
- `frontend/src/hooks/use-market-data-websocket.ts`
- `frontend/src/components/`
- `frontend/src/store/market-data-store.test.ts`

Rules:

- preserve the current backend contract unless the task explicitly changes it
- keep sequence-gap handling explicit
- prefer simple fixed-view rendering for the ladder over table abstractions
- keep the UI localhost-development oriented; production serving is not in scope

Minimum validation:

```powershell
Set-Location frontend
npm run test:run
npm run build
```

If the change touches API calls or WebSocket envelopes, also run backend tests.

## 4. Changing Build Or Tooling

Read first:

- `CMakeLists.txt`
- `README.md`
- `AGENTS.md`
- this file

If you touch any of these, update all affected docs in the same pass:

- backend build commands
- server build assumptions
- frontend setup commands
- runtime flags

Do not document dependency managers that the repo does not use. The current backend dependency model is CMake `FetchContent`.

## 5. Updating Documentation

Documentation in this repo should describe the current state, not the hoped-for future state.

Update rules:

- if code behavior changed, update the nearest subsystem doc
- if commands changed, update both `README.md` and `AGENTS.md` when applicable
- if the architecture changed, update `docs/ARCHITECTURE.md`
- if developer guidance changed, update `AGENTS.md`
- if API or dashboard workflows changed, update `docs/WORKFLOWS.md`

## 6. Recommended Command Sets

### Configure And Build Backend

```powershell
cmake -B build -G Ninja
cmake --build build
```

### Run Backend Tests

```powershell
ctest --test-dir build --output-on-failure
```

### Run Backend Server

```powershell
.\build\mercury.exe --server --host 127.0.0.1 --port 9001 --symbol SIM
```

### Run Backend Server With Replay

```powershell
.\build\mercury.exe --server --host 127.0.0.1 --port 9001 --symbol SIM --replay data\sample_orders_with_clients.csv --replay-speed 10
```

### Run Frontend

```powershell
Set-Location frontend
npm install
npm run dev
```

### Frontend Verification

```powershell
Set-Location frontend
npm run test:run
npm run build
```

### Basic HTTP Smoke Checks

Health:

```powershell
Invoke-RestMethod http://127.0.0.1:9001/api/health
```

State:

```powershell
Invoke-RestMethod http://127.0.0.1:9001/api/state
```

Submit order:

```powershell
Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:9001/api/orders `
  -ContentType "application/json" `
  -Body '{"type":"limit","side":"buy","price":101,"quantity":10,"clientId":1,"tif":"GTC"}'
```

## 7. Practical Review Checklist

Before finishing a change, confirm:

- the modified behavior is covered by a backend or frontend test
- sequence handling still makes sense for reconnect and resync paths
- HTTP and WebSocket contracts are still documented correctly
- docs do not overstate unsupported production features
- no change silently expanded scope from a targeted fix into a broad refactor
