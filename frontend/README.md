# Mercury Frontend

React + TypeScript + Vite dashboard for the Mercury matching engine.

## Stack

- **React 19** with TypeScript
- **Vite 8** dev server with HMR
- **Zustand** for state management
- **Tailwind CSS 4** via `@tailwindcss/vite`
- **Lightweight Charts** for the mid-price chart
- **shadcn/ui-style** local primitives in `src/components/ui/`

## Getting Started

```powershell
npm install
npm run dev
```

Vite proxies `/api` and `/ws` to `127.0.0.1:9001` (configured in `vite.config.ts`). The backend server must be running before the dashboard can connect.

## Dashboard Layout

| Area | Component | Description |
|------|-----------|-------------|
| Top | `TopBar` | Symbol, mid-price, spread, connection badge, clock |
| Top | `StatsStrip` | Bid, ask, mid, spread, bps, trades, volume, orders, levels |
| Left | `OrderEntryForm` | Limit/market/cancel/modify with buy/sell, price, qty, clientId |
| Left | `PnLCard` | Net position plus total/realized/unrealized PnL marked live to the current mid |
| Left | `SimulationControls` | Pause/resume, restart, timing, replay, volatility, regime, scenarios, agent counts, market-maker tuning, attribution |
| Left | `SystemHealth` | Engine latency (us), throughput (msg/s), connection dot |
| Center | `MidPriceChart` | Lightweight-charts 1s candlestick chart with zoom/pan |
| Center | `OrderBookLadder` | L2 depth: asks (red) above, spread marker, bids (green) below |
| Right | `TradeTape` | Time & sales with uptick/downtick, value, self-trade "You" badge |
| Main | `BacktestResultsViewer` | Lab tab for running instant/headless backtests, sweeps, replay calibration, or importing artifacts for P&L, inventory, queue, toxicity, and agents |
| Bottom | `StatusBar` | WS state, client, trade count, volume, levels, timezone, version |

## State Management

The Zustand store (`src/store/market-data-store.ts`) handles:

- **Snapshot**: initializes bids, asks, stats, chart points
- **Book deltas**: upserts/removes price levels with cumulative tracking
- **Trades**: newest-first ring buffer (120 entries)
- **Stats**: trade count, volume, spread, mid-price, MPS
- **PnL**: per-client position and P&L tracking from fills and live mark-to-market frames
- **Simulation**: runtime status, timing, volatility, regime, noise-trader count, toxicity, market-maker config, and arrival rates
- **Agent metrics**: per-agent P&L, fill counts, queue position, quantity ahead, and fill probability
- **Telemetry**: `engineLatencyNs` from deltas/trades, `messagesPerSecond` from stats
- **Self-trade detection**: submitted order IDs tracked in a `Set<number>`
- **Sequence handling**: rejects stale market frames while accepting global sequence gaps caused by PnL, execution, simulation, and agent frames

## WebSocket Connection

`src/hooks/use-market-data-websocket.ts` manages:

- Auto-connect to `/ws/market` with exponential backoff reconnect
- Subscribe message with configurable depth
- Animation-frame batching before store updates
- Cleanup on unmount

## Testing

```powershell
npm run test:run    # vitest run
npm run build       # tsc + vite build
```

## Lab API

The Lab tab posts to `/api/lab/run` on the backend. Supported modes are `backtest`, `headless`, `sweep`, and `calibrate_replay`. Responses include summary JSON plus chart/table-ready artifacts, so a run can be inspected immediately without first importing files.

## Project Structure

```text
src/
|-- App.tsx                          # Root layout
|-- main.tsx                         # Entry point
|-- index.css                        # Design tokens + Tailwind
|-- components/
|   |-- mid-price-chart.tsx
|   |-- backtest-results-viewer.tsx
|   |-- order-book-ladder.tsx
|   |-- order-entry-form.tsx
|   |-- pnl-card.tsx
|   |-- stats-strip.tsx
|   |-- status-bar.tsx
|   |-- system-health.tsx
|   |-- top-bar.tsx
|   |-- trade-tape.tsx
|   `-- ui/                          # Primitives (badge, button, card, input, label)
|-- hooks/
|   `-- use-market-data-websocket.ts
|-- lib/
|   |-- format.ts                    # Number/clock formatters
|   |-- types.ts                     # DTOs matching backend envelope payloads
|   `-- utils.ts                     # cn() helper
|-- store/
|   |-- market-data-store.ts
|   `-- market-data-store.test.ts
`-- test/
    `-- setup.ts                     # Vitest setup
```
