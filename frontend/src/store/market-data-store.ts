import { create } from 'zustand'
import type {
  AgentMetricsPayload,
  BookLevel,
  ConnectionState,
  MarketEnvelope,
  OrderResponse,
  PnLPayload,
  SimulationStatePayload,
  StatsPayload,
  TradePayload,
} from '../lib/types'

const MAX_TRADES = 120
/** Initial snapshot depth requested by the WebSocket client. */
const MAX_BOOK_DEPTH = 20

export interface ChartPoint {
  timestamp: number
  open: number
  high: number
  low: number
  close: number
}

export interface SymbolBucket {
  sequence: number
  bids: BookLevel[]
  asks: BookLevel[]
  trades: TradePayload[]
  stats: StatsPayload | null
  simulation: SimulationStatePayload | null
  agentMetricsByClient: Record<number, AgentMetricsPayload>
  chartPoints: ChartPoint[]
  pnlByClient: Record<number, PnLPayload>
  engineLatencyNs: number | null
  messagesPerSecond: number
}

export const EMPTY_BUCKET: SymbolBucket = {
  sequence: 0,
  bids: [],
  asks: [],
  trades: [],
  stats: null,
  simulation: null,
  agentMetricsByClient: {},
  chartPoints: [],
  pnlByClient: {},
  engineLatencyNs: null,
  messagesPerSecond: 0,
}

function newBucket(): SymbolBucket {
  return {
    sequence: 0,
    bids: [],
    asks: [],
    trades: [],
    stats: null,
    simulation: null,
    agentMetricsByClient: {},
    chartPoints: [],
    pnlByClient: {},
    engineLatencyNs: null,
    messagesPerSecond: 0,
  }
}

interface MarketDataState {
  activeSymbol: string
  symbols: string[]
  bySymbol: Record<string, SymbolBucket>
  streamSequence: number
  connectionState: ConnectionState
  activeClientId: number
  lastOrderResponse: OrderResponse | null
  submittedOrderIds: Set<number>
  setActiveSymbol: (symbol: string) => void
  setConnectionState: (state: ConnectionState) => void
  setActiveClientId: (clientId: number) => void
  setLastOrderResponse: (response: OrderResponse | null) => void
  trackOrderId: (orderId: number) => void
  applyEnvelope: (envelope: MarketEnvelope) => void
  /** Clear market sequences so the next snapshot is always accepted (WS resync). */
  prepareResync: () => void
  reset: () => void
}

function upsertLevel(levels: BookLevel[], nextLevel: BookLevel, descending: boolean) {
  const filtered = levels.filter((level) => level.price !== nextLevel.price)
  if (nextLevel.quantity > 0 && nextLevel.orderCount > 0) {
    filtered.push(nextLevel)
  }

  filtered.sort((left, right) => (descending ? right.price - left.price : left.price - right.price))
  // Keep every active level learned from the delta stream. Trimming a live
  // level is lossy because the server will not resend it merely because a
  // better level was later removed.
  return filtered
}

function restingOrderCount(bids: BookLevel[], asks: BookLevel[]): number {
  return (
    bids.reduce((sum, level) => sum + (level.orderCount || 0), 0) +
    asks.reduce((sum, level) => sum + (level.orderCount || 0), 0)
  )
}

/** Expected client IDs for built-in agents given counts (mirrors MarketRuntime). */
function expectedAgentClientIds(payload: SimulationStatePayload): Set<number> {
  const ids = new Set<number>()
  for (let i = 0; i < (payload.marketMakerCount ?? 0); i += 1) ids.add(1000 + i)
  for (let i = 0; i < (payload.momentumCount ?? 0); i += 1) ids.add(2000 + i)
  for (let i = 0; i < (payload.meanReversionCount ?? 0); i += 1) ids.add(3000 + i)
  for (let i = 0; i < (payload.noiseTraderCount ?? 0); i += 1) ids.add(4000 + i)
  return ids
}

function pruneAgentMetrics(
  metrics: Record<number, AgentMetricsPayload>,
  allowed: Set<number>,
): Record<number, AgentMetricsPayload> {
  const next: Record<number, AgentMetricsPayload> = {}
  for (const [key, value] of Object.entries(metrics)) {
    const clientId = Number(key)
    // Keep custom agents (outside 1000-4999 built-in bands) and live slots.
    const local = clientId % 10000
    const isBuiltinBand = local >= 1000 && local < 5000
    if (!isBuiltinBand || allowed.has(local) || allowed.has(clientId)) {
      next[clientId] = value
    }
  }
  return next
}

function ensureSymbol(
  symbols: string[],
  bySymbol: Record<string, SymbolBucket>,
  symbol: string,
): { symbols: string[]; bySymbol: Record<string, SymbolBucket> } {
  const nextSymbols = symbols.includes(symbol) ? symbols : [...symbols, symbol].sort()
  const nextBySymbol = bySymbol[symbol] ? bySymbol : { ...bySymbol, [symbol]: newBucket() }
  return { symbols: nextSymbols, bySymbol: nextBySymbol }
}

function updateBucket(
  bySymbol: Record<string, SymbolBucket>,
  symbol: string,
  patch: Partial<SymbolBucket>,
): Record<string, SymbolBucket> {
  const current = bySymbol[symbol] ?? newBucket()
  return { ...bySymbol, [symbol]: { ...current, ...patch } }
}

function chartSecond(timestamp: number): number {
  return Math.floor(timestamp / 1000)
}

function newChartCandle(timestamp: number, price: number): ChartPoint {
  return {
    timestamp,
    open: price,
    high: price,
    low: price,
    close: price,
  }
}

function updateChartCandle(candle: ChartPoint, timestamp: number, price: number): ChartPoint {
  return {
    timestamp,
    open: candle.open,
    high: Math.max(candle.high, price),
    low: Math.min(candle.low, price),
    close: price,
  }
}

function appendChartPoint(points: ChartPoint[], point: { timestamp: number; value: number }): ChartPoint[] {
  if (point.value <= 0) return points

  const nextSecond = chartSecond(point.timestamp)
  const last = points.at(-1)
  if (!last) return [newChartCandle(point.timestamp, point.value)]

  const lastSecond = chartSecond(last.timestamp)
  if (nextSecond > lastSecond) {
    return [...points, newChartCandle(point.timestamp, point.value)]
  }

  if (nextSecond === lastSecond) {
    if (last.timestamp === point.timestamp && last.close === point.value) {
      return points
    }
    return [...points.slice(0, -1), updateChartCandle(last, point.timestamp, point.value)]
  }

  const bySecond = new Map<number, ChartPoint>()
  for (const existing of points) {
    bySecond.set(chartSecond(existing.timestamp), existing)
  }
  const existing = bySecond.get(nextSecond)
  bySecond.set(
    nextSecond,
    existing
      ? updateChartCandle(existing, point.timestamp, point.value)
      : newChartCandle(point.timestamp, point.value),
  )
  return Array.from(bySecond.values()).sort((left, right) => left.timestamp - right.timestamp)
}

export const useMarketDataStore = create<MarketDataState>((set, get) => ({
  activeSymbol: 'SIM',
  symbols: ['SIM'],
  bySymbol: { SIM: newBucket() },
  streamSequence: 0,
  connectionState: 'connecting',
  activeClientId: 1,
  lastOrderResponse: null,
  submittedOrderIds: new Set<number>(),
  setActiveSymbol: (activeSymbol) =>
    set((state) => {
      if (!activeSymbol || activeSymbol === state.activeSymbol) {
        return {} as Partial<MarketDataState>
      }
      const ensured = ensureSymbol(state.symbols, state.bySymbol, activeSymbol)
      return {
        activeSymbol,
        symbols: ensured.symbols,
        bySymbol: ensured.bySymbol,
      }
    }),
  setConnectionState: (connectionState) => set({ connectionState }),
  setActiveClientId: (activeClientId) => set({ activeClientId }),
  setLastOrderResponse: (lastOrderResponse) => set({ lastOrderResponse }),
  trackOrderId: (orderId) => {
    if (orderId <= 0) return
    set((state) => {
      const next = new Set(state.submittedOrderIds)
      next.add(orderId)
      return { submittedOrderIds: next }
    })
  },
  prepareResync: () =>
    set((state) => {
      const cleared: Record<string, SymbolBucket> = {}
      for (const symbol of state.symbols) {
        const existing = state.bySymbol[symbol] ?? newBucket()
        cleared[symbol] = {
          ...newBucket(),
          // Keep non-book UI state across a reconnect resync boundary.
          simulation: existing.simulation,
          agentMetricsByClient: existing.agentMetricsByClient,
          pnlByClient: existing.pnlByClient,
          chartPoints: existing.chartPoints,
        }
      }
      return {
        streamSequence: 0,
        bySymbol: cleared,
      }
    }),
  reset: () =>
    set({
      activeSymbol: 'SIM',
      symbols: ['SIM'],
      bySymbol: { SIM: newBucket() },
      streamSequence: 0,
      connectionState: 'connecting',
      activeClientId: 1,
      lastOrderResponse: null,
      submittedOrderIds: new Set<number>(),
    }),
  applyEnvelope: (envelope) => {
    const symbol = envelope.symbol
    if (!symbol) return

    const state = get()
    const updatesMarketSequence =
      envelope.type === 'snapshot' ||
      envelope.type === 'book_delta' ||
      envelope.type === 'trade' ||
      envelope.type === 'stats'

    // Snapshots are authoritative resync points: always apply them, even when
    // sequence matches the last frame (server reuses currentSequence_) or
    // restarts from a lower sequence after process restart.
    //
    // Forward gaps are accepted: non-market frames share the global sequence,
    // so a jump does not imply a lost book_delta. Never drop market frames on
    // gap — that froze the live ladder until an opportunistic reconnect.
    const symbolSequence = state.bySymbol[symbol]?.sequence ?? 0
    if (
      envelope.type !== 'snapshot' &&
      updatesMarketSequence &&
      state.streamSequence !== 0 &&
      envelope.sequence <= state.streamSequence &&
      envelope.sequence <= symbolSequence
    ) {
      return
    }

    set((prev) => {
      const ensured = ensureSymbol(prev.symbols, prev.bySymbol, symbol)
      const bucket = ensured.bySymbol[symbol]
      const streamSequence =
        updatesMarketSequence
          ? Math.max(prev.streamSequence, envelope.sequence)
          : prev.streamSequence

      let activePatch: Partial<MarketDataState> | null = null
      if (prev.activeSymbol === '' || !prev.symbols.includes(prev.activeSymbol)) {
        activePatch = { activeSymbol: symbol }
      }

      if (envelope.type === 'snapshot') {
        const bids = envelope.payload.bids.slice(0, MAX_BOOK_DEPTH)
        const asks = envelope.payload.asks.slice(0, MAX_BOOK_DEPTH)
        const nextStats: StatsPayload = {
          tradeCount: bucket.stats?.tradeCount ?? 0,
          totalVolume: bucket.stats?.totalVolume ?? 0,
          orderCount: restingOrderCount(bids, asks),
          bidLevels: bids.length,
          askLevels: asks.length,
          bestBid: envelope.payload.bestBid,
          bestAsk: envelope.payload.bestAsk,
          spread: envelope.payload.spread,
          midPrice: envelope.payload.midPrice,
          timestamp: envelope.payload.timestamp,
        }

        const nextChartPoints = appendChartPoint(bucket.chartPoints, {
          timestamp: envelope.payload.timestamp,
          value: envelope.payload.midPrice,
        })

        return {
          symbols: ensured.symbols,
          streamSequence: Math.max(streamSequence, envelope.sequence),
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: envelope.sequence,
            bids,
            asks,
            trades: [],
            stats: nextStats,
            chartPoints: nextChartPoints,
            engineLatencyNs: null,
          }),
          ...(activePatch ?? {}),
        }
      }

      if (envelope.type === 'book_delta') {
        const nextLevel: BookLevel = {
          side: envelope.payload.side,
          price: envelope.payload.price,
          quantity: envelope.payload.quantity,
          orderCount: envelope.payload.orderCount,
        }

        const latencyUpdate =
          envelope.payload.engineLatencyNs != null && envelope.payload.engineLatencyNs > 0
            ? { engineLatencyNs: envelope.payload.engineLatencyNs }
            : {}

        const nextBids =
          envelope.payload.side === 'buy'
            ? upsertLevel(bucket.bids, nextLevel, true)
            : bucket.bids
        const nextAsks =
          envelope.payload.side === 'sell'
            ? upsertLevel(bucket.asks, nextLevel, false)
            : bucket.asks

        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: Math.max(bucket.sequence, envelope.sequence),
            bids: nextBids,
            asks: nextAsks,
            ...latencyUpdate,
          }),
          ...(activePatch ?? {}),
        }
      }

      if (envelope.type === 'trade') {
        const latencyUpdate =
          envelope.payload.engineLatencyNs != null && envelope.payload.engineLatencyNs > 0
            ? { engineLatencyNs: envelope.payload.engineLatencyNs }
            : {}

        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: Math.max(bucket.sequence, envelope.sequence),
            trades: [envelope.payload, ...bucket.trades].slice(0, MAX_TRADES),
            ...latencyUpdate,
          }),
          ...(activePatch ?? {}),
        }
      }

      if (envelope.type === 'stats') {
        const nextPoints = appendChartPoint(bucket.chartPoints, {
          timestamp: envelope.payload.timestamp,
          value: envelope.payload.midPrice,
        })

        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: Math.max(bucket.sequence, envelope.sequence),
            stats: envelope.payload,
            chartPoints: nextPoints,
            messagesPerSecond: envelope.payload.messagesPerSecond ?? bucket.messagesPerSecond,
          }),
          ...(activePatch ?? {}),
        }
      }

      if (envelope.type === 'sim_state') {
        const previousSimulationTimestamp = bucket.simulation?.simulationTimestamp
        const isRuntimeReset =
          previousSimulationTimestamp !== undefined &&
          envelope.payload.simulationTimestamp < previousSimulationTimestamp
        const serverSymbols = envelope.payload.symbols?.filter(Boolean).sort()
        let nextSymbols = ensured.symbols
        let nextBySymbol = ensured.bySymbol
        let nextActivePatch = activePatch

        if (serverSymbols && serverSymbols.length > 0) {
          const allowedSymbols = new Set(serverSymbols)
          nextSymbols = serverSymbols
          nextBySymbol = Object.fromEntries(
            Object.entries(ensured.bySymbol).filter(([key]) => allowedSymbols.has(key)),
          )

          for (const serverSymbol of serverSymbols) {
            if (!nextBySymbol[serverSymbol]) {
              nextBySymbol[serverSymbol] = newBucket()
            }
          }

          if (prev.activeSymbol === '' || !allowedSymbols.has(prev.activeSymbol)) {
            nextActivePatch = {
              ...(nextActivePatch ?? {}),
              activeSymbol: allowedSymbols.has(symbol) ? symbol : serverSymbols[0],
            }
          }
        }

        if (isRuntimeReset) {
          return {
            symbols: nextSymbols,
            streamSequence: 0,
            bySymbol: {
              ...nextBySymbol,
              [symbol]: {
                ...newBucket(),
                sequence: envelope.sequence,
                simulation: envelope.payload,
              },
            },
            submittedOrderIds: new Set<number>(),
            lastOrderResponse: null,
            ...(nextActivePatch ?? {}),
          }
        }

        const allowedAgents = expectedAgentClientIds(envelope.payload)
        const currentMetrics = nextBySymbol[symbol]?.agentMetricsByClient ?? bucket.agentMetricsByClient
        return {
          symbols: nextSymbols,
          bySymbol: updateBucket(nextBySymbol, symbol, {
            sequence: Math.max(bucket.sequence, envelope.sequence),
            simulation: envelope.payload,
            agentMetricsByClient: pruneAgentMetrics(currentMetrics, allowedAgents),
          }),
          ...(nextActivePatch ?? {}),
        }
      }

      if (envelope.type === 'agent_metrics') {
        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            agentMetricsByClient: {
              ...bucket.agentMetricsByClient,
              [envelope.payload.clientId]: envelope.payload,
            },
          }),
          ...(activePatch ?? {}),
        }
      }

      if (envelope.type === 'execution') {
        // Only surface executions for orders this UI submitted — agent and
        // other-client fills must not overwrite the manual order result panel.
        const exec = envelope.payload
        if (!prev.submittedOrderIds.has(exec.orderId)) {
          return {
            symbols: ensured.symbols,
            streamSequence,
            bySymbol: ensured.bySymbol,
            ...(activePatch ?? {}),
          }
        }
        const asResponse: OrderResponse = {
          submittedOrderId: exec.orderId,
          orderType: 'limit',
          side: 'buy',
          tif: 'GTC',
          status: exec.status,
          rejectReason: exec.rejectReason,
          orderId: exec.orderId,
          filledQuantity: exec.filledQuantity,
          remainingQuantity: exec.remainingQuantity,
          message: exec.rejectReason || exec.status,
          trades: [],
        }
        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: ensured.bySymbol,
          lastOrderResponse: asResponse,
          ...(activePatch ?? {}),
        }
      }

      // pnl
      const existingPnl = bucket.pnlByClient[envelope.payload.clientId]
      if (existingPnl && envelope.payload.timestamp < existingPnl.timestamp) {
        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: ensured.bySymbol,
          ...(activePatch ?? {}),
        }
      }

      const nextPnl = {
        ...bucket.pnlByClient,
        [envelope.payload.clientId]: envelope.payload,
      }

      return {
        symbols: ensured.symbols,
        streamSequence,
        bySymbol: updateBucket(ensured.bySymbol, symbol, {
          pnlByClient: nextPnl,
        }),
        ...(activePatch ?? {}),
      }
    })

  },
}))

// Convenience selectors ------------------------------------------------

export function useActiveBucket(): SymbolBucket {
  return useMarketDataStore((state) => state.bySymbol[state.activeSymbol] ?? EMPTY_BUCKET)
}

export function useActiveSymbol(): string {
  return useMarketDataStore((state) => state.activeSymbol)
}

export function useAvailableSymbols(): string[] {
  return useMarketDataStore((state) => state.symbols)
}
