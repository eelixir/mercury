export type Side = 'buy' | 'sell'
export type OrderType = 'limit' | 'market' | 'cancel' | 'modify'
export type ConnectionState = 'connecting' | 'connected' | 'disconnected'

export interface BookLevel {
  price: number
  quantity: number
  orderCount: number
  side: Side
}

export interface MarketSnapshotPayload {
  depth: number
  bids: BookLevel[]
  asks: BookLevel[]
  bestBid: number | null
  bestAsk: number | null
  spread: number
  midPrice: number
  timestamp: number
}

export interface BookDeltaPayload {
  side: Side
  price: number
  quantity: number
  orderCount: number
  action: 'upsert' | 'remove'
  timestamp: number
}

export interface TradePayload {
  tradeId: number
  price: number
  quantity: number
  buyOrderId: number
  sellOrderId: number
  buyClientId: number
  sellClientId: number
  timestamp: number
}

export interface StatsPayload {
  tradeCount: number
  totalVolume: number
  orderCount: number
  bidLevels: number
  askLevels: number
  bestBid: number | null
  bestAsk: number | null
  spread: number
  midPrice: number
  timestamp: number
}

export interface PnLPayload {
  clientId: number
  netPosition: number
  realizedPnL: number
  unrealizedPnL: number
  totalPnL: number
  timestamp: number
}

export type MarketEnvelope =
  | {
      type: 'snapshot'
      sequence: number
      symbol: string
      payload: MarketSnapshotPayload
    }
  | {
      type: 'book_delta'
      sequence: number
      symbol: string
      payload: BookDeltaPayload
    }
  | {
      type: 'trade'
      sequence: number
      symbol: string
      payload: TradePayload
    }
  | {
      type: 'stats'
      sequence: number
      symbol: string
      payload: StatsPayload
    }
  | {
      type: 'pnl'
      sequence: number
      symbol: string
      payload: PnLPayload
    }

export interface ExecutionTrade {
  tradeId: number
  buyOrderId: number
  sellOrderId: number
  buyClientId: number
  sellClientId: number
  price: number
  quantity: number
  timestamp: number
}

export interface OrderResponse {
  submittedOrderId: number
  orderType: OrderType
  side: Side
  tif: string
  status: string
  rejectReason: string
  orderId: number
  filledQuantity: number
  remainingQuantity: number
  message: string
  trades: ExecutionTrade[]
}
