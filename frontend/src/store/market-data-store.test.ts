import { beforeEach, describe, expect, it } from 'vitest'
import { useMarketDataStore } from './market-data-store'

describe('market data store', () => {
  beforeEach(() => {
    useMarketDataStore.setState({
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
    })
  })

  it('applies snapshot then delta updates in order', () => {
    const store = useMarketDataStore.getState()

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 1,
      symbol: 'SIM',
      payload: {
        depth: 20,
        bids: [{ side: 'buy', price: 100, quantity: 5, orderCount: 1 }],
        asks: [{ side: 'sell', price: 101, quantity: 7, orderCount: 1 }],
        bestBid: 100,
        bestAsk: 101,
        spread: 1,
        midPrice: 100,
        timestamp: 10,
      },
    })

    const needsResync = store.applyEnvelope({
      type: 'book_delta',
      sequence: 2,
      symbol: 'SIM',
      payload: {
        side: 'buy',
        price: 100,
        quantity: 9,
        orderCount: 2,
        action: 'upsert',
        timestamp: 11,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(needsResync).toBe(false)
    expect(snapshot.bids[0].quantity).toBe(9)
    expect(snapshot.sequence).toBe(2)
  })

  it('requests resync when a sequence gap appears', () => {
    useMarketDataStore.setState({ sequence: 10 })

    const needsResync = useMarketDataStore.getState().applyEnvelope({
      type: 'trade',
      sequence: 14,
      symbol: 'SIM',
      payload: {
        tradeId: 1,
        price: 100,
        quantity: 3,
        buyOrderId: 1,
        sellOrderId: 2,
        buyClientId: 1,
        sellClientId: 2,
        timestamp: 15,
      },
    })

    expect(needsResync).toBe(true)
  })
})
