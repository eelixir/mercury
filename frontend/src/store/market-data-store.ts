import { create } from 'zustand'
import type {
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
const MAX_POINTS = 240

export interface ChartPoint {
  timestamp: number
  value: number
}

export interface SymbolBucket {
  sequence: number
  bids: BookLevel[]
  asks: BookLevel[]
  trades: TradePayload[]
  stats: StatsPayload | null
  simulation: SimulationStatePayload | null
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
  applyEnvelope: (envelope: MarketEnvelope) => boolean
  reset: () => void
}

function upsertLevel(levels: BookLevel[], nextLevel: BookLevel, descending: boolean) {
  const filtered = levels.filter((level) => level.price !== nextLevel.price)
  if (nextLevel.quantity > 0 && nextLevel.orderCount > 0) {
    filtered.push(nextLevel)
  }

  filtered.sort((left, right) => (descending ? right.price - left.price : left.price - right.price))
  return filtered
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
    if (!symbol) return false

    const state = get()
    const isStreamSequenced = envelope.type !== 'snapshot' && envelope.type !== 'sim_state'

    const isGap =
      isStreamSequenced &&
      state.streamSequence !== 0 &&
      envelope.sequence > state.streamSequence + 1

    if (isGap) {
      return true
    }

    const symbolSequence = state.bySymbol[symbol]?.sequence ?? 0
    if (
      isStreamSequenced &&
      state.streamSequence !== 0 &&
      envelope.sequence <= state.streamSequence &&
      envelope.sequence <= symbolSequence
    ) {
      return false
    }

    set((prev) => {
      const ensured = ensureSymbol(prev.symbols, prev.bySymbol, symbol)
      const bucket = ensured.bySymbol[symbol]
      const streamSequence =
        envelope.type === 'sim_state'
          ? prev.streamSequence
          : Math.max(prev.streamSequence, envelope.sequence)

      let activePatch: Partial<MarketDataState> | null = null
      if (prev.activeSymbol === '' || !prev.symbols.includes(prev.activeSymbol)) {
        activePatch = { activeSymbol: symbol }
      }

      if (envelope.type === 'snapshot') {
        const nextStats: StatsPayload = {
          tradeCount: bucket.stats?.tradeCount ?? 0,
          totalVolume: bucket.stats?.totalVolume ?? 0,
          orderCount: envelope.payload.bids.length + envelope.payload.asks.length,
          bidLevels: envelope.payload.bids.length,
          askLevels: envelope.payload.asks.length,
          bestBid: envelope.payload.bestBid,
          bestAsk: envelope.payload.bestAsk,
          spread: envelope.payload.spread,
          midPrice: envelope.payload.midPrice,
          timestamp: envelope.payload.timestamp,
        }

        const nextChartPoints =
          envelope.payload.midPrice > 0
            ? [
                ...bucket.chartPoints,
                { timestamp: envelope.payload.timestamp, value: envelope.payload.midPrice },
              ].slice(-MAX_POINTS)
            : bucket.chartPoints

        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: envelope.sequence,
            bids: envelope.payload.bids,
            asks: envelope.payload.asks,
            stats: nextStats,
            chartPoints: nextChartPoints,
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

        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: Math.max(bucket.sequence, envelope.sequence),
            bids:
              envelope.payload.side === 'buy'
                ? upsertLevel(bucket.bids, nextLevel, true)
                : bucket.bids,
            asks:
              envelope.payload.side === 'sell'
                ? upsertLevel(bucket.asks, nextLevel, false)
                : bucket.asks,
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
        const nextPoints =
          envelope.payload.midPrice > 0
            ? [
                ...bucket.chartPoints,
                { timestamp: envelope.payload.timestamp, value: envelope.payload.midPrice },
              ].slice(-MAX_POINTS)
            : bucket.chartPoints

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
        return {
          symbols: ensured.symbols,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: Math.max(bucket.sequence, envelope.sequence),
            simulation: envelope.payload,
          }),
          ...(activePatch ?? {}),
        }
      }

      if (envelope.type === 'execution') {
        return {
          symbols: ensured.symbols,
          streamSequence,
          bySymbol: updateBucket(ensured.bySymbol, symbol, {
            sequence: Math.max(bucket.sequence, envelope.sequence),
          }),
          ...(activePatch ?? {}),
        }
      }

      // pnl
      const nextPnl = {
        ...bucket.pnlByClient,
        [envelope.payload.clientId]: envelope.payload,
      }

      return {
        symbols: ensured.symbols,
        streamSequence,
        bySymbol: updateBucket(ensured.bySymbol, symbol, {
          sequence: Math.max(bucket.sequence, envelope.sequence),
          pnlByClient: nextPnl,
        }),
        ...(activePatch ?? {}),
      }
    })

    return false
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
