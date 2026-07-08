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
- trade and execution callback side effects

Minimum validation:

- `ctest --test-dir build --output-on-failure`
- or targeted `mercury_tests --gtest_filter=MatchingEngineTest.*` if you are working directly with the test binary

## 2. Changing Market Runtime, Server, Replay, Or Backtests

Use this path for `MarketRuntime`, `EngineService`, `ServerApp`, snapshots, deltas, sequence handling, simulation controls, replay flow, instant backtests, or sweep/report output.

Read first:

- `include/MarketRuntime.h`
- `include/MarketData.h`
- `include/EngineService.h`
- `include/ServerApp.h`
- `include/OrderEntryGateway.h`
- `include/MarketDataPublisher.h`
- `include/BinaryProtocol.h`
- `include/ServerHelpers.h`
- `include/BacktestReport.h`
- `include/BacktestRunner.h`
- `src/MarketRuntime.cpp`
- `src/EngineService.cpp`
- `src/ServerApp.cpp`
- `src/BacktestRunner.cpp`
- `src/OrderEntryGateway.cpp`
- `src/MarketDataPublisher.cpp`
- `tests/market_data_test.cpp`
- `tests/simulation_runtime_test.cpp`
- `frontend/src/store/market-data-store.ts`

Required checks:

- all order sources still route through `MarketRuntime`
- all mutations still route through the engine thread
- WebSocket code still does not read the order book directly
- sequence numbers remain monotonic across mixed event types
- snapshot depth remains clamped to `1..100`
- replay and HTTP orders still feed the same runtime path
- simulation controls and runtime-state reporting stay aligned with backend behavior
- live timing and replay controls stay aligned between `/api/simulation/control`, `/api/replay/control`, and the frontend operator panel
- offline lab controls stay aligned between CLI backtest/sweep/calibration behavior, `/api/lab/run`, and the frontend Lab tab
- instant backtests stay CPU-paced and do not block on real-time sleeps
- `--backtest-output` artifacts stay parseable and aligned with emitted DTO fields
- `--sweep` applies the same simulation knobs as the single-run CLI path
- `--scenario`, `--mm-config`, and `--calibrate-replay` use the same JSON field names as sweep runs
- `agent_metrics` stays additive and does not break snapshot, stats, trade, execution, PnL, or simulation-state consumers
- frontend store still understands the emitted envelope shape
- telemetry fields (`engineLatencyNs`, `messagesPerSecond`) are populated correctly
- queue-position fields stay sourced from the actual intrusive FIFO `PriceLevel` order, not from copied snapshots
- toxicity changes spread behavior without creating runaway price drift
- both JSON (`/ws/market`) and binary (`/ws/market/bin`) paths publish consistently
- volatility preset changes still produce believable market behavior rather than runaway multi-second price drift

Minimum validation:

```powershell
ctest --test-dir build --output-on-failure
Set-Location frontend
npm run test:run
```

Recommended manual smoke test:

1. Start the server with `.\build\mercury.exe --server --sim --port 9001`.
2. Start the frontend with `npm run dev` inside `frontend/`.
3. Confirm the ladder and trade tape are active before any manual order is submitted.
4. Submit a buy or sell order from the browser or with `POST /api/orders`.
5. Confirm the ladder, trade tape, stats, simulation controls, PnL, and system health card update.
6. Call `POST /api/simulation/control` with `pause`, `resume`, `restart`, `set_timing`, `set_volatility`, `set_regime`, `set_counts`, `set_market_maker`, or `apply_scenario` and confirm the UI changes.
7. Call `POST /api/replay/control` with `start` and `stop`, using `data\sample_orders_with_clients.csv`, and confirm replay state returns through `/api/health`.
8. Run an instant backtest from the Lab tab or `POST /api/lab/run` and confirm returned artifacts populate summary charts/tables.
9. Confirm self-trade highlighting appears in the trade tape.
10. Refresh the browser and confirm the UI resyncs from a fresh snapshot.
11. Optionally connect a raw WebSocket client to `/ws/market/bin` and verify binary frames arrive.
12. Optionally run `.\build\mercury.exe --backtest --scenario scenarios\toxic-flow.json --backtest-output .codex-run\toxic-flow` twice and compare summary output.
13. When changing simulation dynamics, sample `GET /api/state` over a few intervals at `normal` and `high` volatility and confirm spreads and activity increase without 100% to 1000% price jumps over a few seconds.

## 3. Changing Frontend Dashboard Behavior

Use this path for layout, store logic, order entry, operator controls, or WebSocket handling.

Read first:

- `frontend/src/App.tsx`
- `frontend/src/store/market-data-store.ts`
- `frontend/src/hooks/use-market-data-websocket.ts`
- `frontend/src/components/`
- `frontend/src/lib/types.ts`
- `frontend/src/store/market-data-store.test.ts`

Rules:

- preserve the current backend contract unless the task explicitly changes it
- keep sequence-gap handling explicit
- keep simulation status and operator controls consistent with `sim_state`
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

If you change core containers, keep the docs aligned with the current implementation:

- `absl::btree_map` for sorted bid and ask ladders
- the repository `HashMap` wrapper backed by `absl::flat_hash_map` for O(1) average lookup
- intrusive FIFO queues inside each price level, not `std::deque`

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
.\build\mercury.exe --server --sim --host 127.0.0.1 --port 9001 --symbol SIM
```

### Run Backend Server With Replay

```powershell
.\build\mercury.exe --server --sim --host 127.0.0.1 --port 9001 --symbol SIM --replay data\sample_orders_with_clients.csv --replay-speed 10
```

Loop the replay continuously:

```powershell
.\build\mercury.exe --server --sim --host 127.0.0.1 --port 9001 --symbol SIM --replay data\sample_orders_with_clients.csv --replay-speed 10 --replay-loop --replay-loop-pause 1000
```

### Run Headless Simulation

```powershell
.\build\mercury.exe --sim --headless --sim-speed 25 --sim-seed 42 --sim-duration-ms 30000 --sim-volatility normal
```

### Run Instant Backtest

```powershell
.\build\mercury.exe --backtest --sim-seed 42 --sim-duration-ms 30000 --sim-volatility normal --backtest-output runs\baseline
```

Add Poisson-flow noise traders to stress arrival intensity:

```powershell
.\build\mercury.exe --backtest --sim-volatility high --noise-count 3 --sim-duration-ms 30000 --backtest-output runs\high-noise
```

The output directory contains `summary.json`, `config.json`, `trades.csv`, `stats.csv`, `pnl.csv`, `sim_state.csv`, `agent_metrics.csv`, and `agent_summary.csv`.

Run a scenario preset:

```powershell
.\build\mercury.exe --backtest --scenario scenarios\toxic-flow.json --backtest-output runs\toxic-flow
```

Run with a market-maker config file:

```powershell
.\build\mercury.exe --backtest --mm-config scenarios\calm-two-sided-market.json --backtest-output runs\custom-mm
```

Run replay calibration:

```powershell
.\build\mercury.exe --calibrate-replay data\sample_orders_with_clients.csv --backtest-output runs\replay-calibration
```

### Run Backtest Sweep

Create a JSON sweep file:

```json
{
  "runs": [
    { "name": "baseline", "seed": 42, "volatility": "normal", "marketMakerCount": 2, "noiseTraderCount": 1 },
    { "name": "wide-mm", "seed": 42, "volatility": "high", "marketMakerCount": 4, "noiseTraderCount": 3 }
  ]
}
```

Run all scenarios:

```powershell
.\build\mercury.exe --sweep runs\sweep.json --sim-duration-ms 30000 --backtest-output runs\sweep
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
  -Body '{"type":"limit","side":"buy","price":101,"quantity":10,"clientId":1,"tif":"GTC","symbol":"SIM"}'
```

Simulation control:

```powershell
Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:9001/api/simulation/control `
  -ContentType "application/json" `
  -Body '{"action":"set_volatility","volatility":"high"}'
```

Force a specific regime (supported values: `calm`, `normal`, `stressed`):

```powershell
Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:9001/api/simulation/control `
  -ContentType "application/json" `
  -Body '{"action":"set_regime","volatility":"stressed"}'
```

Change live timing:

```powershell
Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:9001/api/simulation/control `
  -ContentType "application/json" `
  -Body '{"action":"set_timing","clockMode":"accelerated","speed":25}'
```

Start and stop replay:

```powershell
Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:9001/api/replay/control `
  -ContentType "application/json" `
  -Body '{"action":"start","replayFile":"data\\sample_orders_with_clients.csv","speed":10,"loop":false,"loopPauseMs":1000}'

Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:9001/api/replay/control `
  -ContentType "application/json" `
  -Body '{"action":"stop"}'
```

Run an instant lab backtest over HTTP:

```powershell
Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:9001/api/lab/run `
  -ContentType "application/json" `
  -Body '{"mode":"backtest","name":"api-smoke","symbol":"SIM","scenarioFile":"scenarios\\calm-two-sided-market.json","durationMs":1000,"seed":42,"volatility":"normal","marketMakerCount":2,"momentumCount":1,"meanReversionCount":1,"noiseTraderCount":1,"outputDir":".codex-run\\api-smoke"}'
```

## 7. Practical Review Checklist

Before finishing a change, confirm:

- the modified behavior is covered by a backend or frontend test
- HTTP or WebSocket contract changes are covered by `tests/server_api_contract_test.cpp` or an equivalent integration test
- sequence handling still makes sense for reconnect and resync paths
- manual orders, replay, and simulated agents still converge on the same runtime path
- HTTP and WebSocket contracts are still documented correctly
- docs do not overstate unsupported production features
- no change silently expanded scope from a targeted fix into a broad refactor
- volatility tuning changes are backed by deterministic tests, not only by visual inspection in the browser
