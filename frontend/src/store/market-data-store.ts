import { create } from 'zustand'
import type {
  BookLevel,
  ConnectionState,
  MarketEnvelope,
  OrderResponse,
  PnLPayload,
  StatsPayload,
  TradePayload,
} from '../lib/types'

const MAX_TRADES = 120
const MAX_POINTS = 240

export interface ChartPoint {
  timestamp: number
  value: number
}

interface MarketDataState {
  symbol: string
  sequence: number
  connectionState: ConnectionState
  bids: BookLevel[]
  asks: BookLevel[]
  trades: TradePayload[]
  stats: StatsPayload | null
  pnlByClient: Record<number, PnLPayload>
  chartPoints: ChartPoint[]
  activeClientId: number
  lastOrderResponse: OrderResponse | null
  submittedOrderIds: Set<number>
  engineLatencyNs: number | null
  messagesPerSecond: number
  setConnectionState: (state: ConnectionState) => void
  setActiveClientId: (clientId: number) => void
  setLastOrderResponse: (response: OrderResponse | null) => void
  trackOrderId: (orderId: number) => void
  applyEnvelope: (envelope: MarketEnvelope) => boolean
}

function upsertLevel(levels: BookLevel[], nextLevel: BookLevel, descending: boolean) {
  const filtered = levels.filter((level) => level.price !== nextLevel.price)
  if (nextLevel.quantity > 0 && nextLevel.orderCount > 0) {
    filtered.push(nextLevel)
  }

  filtered.sort((left, right) => (descending ? right.price - left.price : left.price - right.price))
  return filtered
}

export const useMarketDataStore = create<MarketDataState>((set, get) => ({
  symbol: 'SIM',
  sequence: 0,
  connectionState: 'connecting',
  bids: [],
  asks: [],
  trades: [],
  stats: null,
  pnlByClient: {},
  chartPoints: [],
  activeClientId: 1,
  lastOrderResponse: null,
  submittedOrderIds: new Set<number>(),
  engineLatencyNs: null,
  messagesPerSecond: 0,
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
  applyEnvelope: (envelope) => {
    const currentSequence = get().sequence

    if (envelope.type !== 'snapshot' && currentSequence !== 0 && envelope.sequence > currentSequence + 1) {
      return true
    }

    set((state) => {
      if (envelope.type === 'snapshot') {
        const nextStats: StatsPayload = {
          tradeCount: state.stats?.tradeCount ?? 0,
          totalVolume: state.stats?.totalVolume ?? 0,
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
                ...state.chartPoints,
                { timestamp: envelope.payload.timestamp, value: envelope.payload.midPrice },
              ].slice(-MAX_POINTS)
            : state.chartPoints

        return {
          symbol: envelope.symbol,
          sequence: envelope.sequence,
          bids: envelope.payload.bids,
          asks: envelope.payload.asks,
          stats: nextStats,
          chartPoints: nextChartPoints,
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
          symbol: envelope.symbol,
          sequence: envelope.sequence,
          bids:
            envelope.payload.side === 'buy'
              ? upsertLevel(state.bids, nextLevel, true)
              : state.bids,
          asks:
            envelope.payload.side === 'sell'
              ? upsertLevel(state.asks, nextLevel, false)
              : state.asks,
          ...latencyUpdate,
        }
      }

      if (envelope.type === 'trade') {
        const latencyUpdate =
          envelope.payload.engineLatencyNs != null && envelope.payload.engineLatencyNs > 0
            ? { engineLatencyNs: envelope.payload.engineLatencyNs }
            : {}

        return {
          symbol: envelope.symbol,
          sequence: envelope.sequence,
          trades: [envelope.payload, ...state.trades].slice(0, MAX_TRADES),
          ...latencyUpdate,
        }
      }

      if (envelope.type === 'stats') {
        const nextPoints =
          envelope.payload.midPrice > 0
            ? [...state.chartPoints, { timestamp: envelope.payload.timestamp, value: envelope.payload.midPrice }].slice(
                -MAX_POINTS,
              )
            : state.chartPoints

        return {
          symbol: envelope.symbol,
          sequence: envelope.sequence,
          stats: envelope.payload,
          chartPoints: nextPoints,
          messagesPerSecond: envelope.payload.messagesPerSecond ?? state.messagesPerSecond,
        }
      }

      const pnlByClient = {
        ...state.pnlByClient,
        [envelope.payload.clientId]: envelope.payload,
      }

      return {
        symbol: envelope.symbol,
        sequence: envelope.sequence,
        pnlByClient,
      }
    })

    return false
  },
}))
