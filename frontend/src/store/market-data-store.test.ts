import { beforeEach, describe, expect, it } from 'vitest'
import { useMarketDataStore } from './market-data-store'

describe('market data store', () => {
  beforeEach(() => {
    useMarketDataStore.getState().reset()
  })

  it('applies snapshot then delta updates in order for a symbol', () => {
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
    const bucket = snapshot.bySymbol['SIM']
    expect(needsResync).toBe(false)
    expect(bucket.bids[0].quantity).toBe(9)
    expect(bucket.sequence).toBe(2)
  })

  it('accepts global sequence gaps because non-market frames share the sequence', () => {
    const state = useMarketDataStore.getState()
    state.applyEnvelope({
      type: 'snapshot',
      sequence: 10,
      symbol: 'SIM',
      payload: {
        depth: 20,
        bids: [],
        asks: [],
        bestBid: null,
        bestAsk: null,
        spread: 0,
        midPrice: 0,
        timestamp: 0,
      },
    })

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

    expect(needsResync).toBe(false)
    expect(useMarketDataStore.getState().bySymbol['SIM'].trades).toHaveLength(1)
  })

  it('does not treat other symbols global sequence numbers as active-symbol gaps', () => {
    const store = useMarketDataStore.getState()

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 10,
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

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 10,
      symbol: 'AAPL',
      payload: {
        depth: 20,
        bids: [{ side: 'buy', price: 200, quantity: 3, orderCount: 1 }],
        asks: [{ side: 'sell', price: 201, quantity: 4, orderCount: 1 }],
        bestBid: 200,
        bestAsk: 201,
        spread: 1,
        midPrice: 200,
        timestamp: 10,
      },
    })

    const aaplNeedsResync = store.applyEnvelope({
      type: 'book_delta',
      sequence: 11,
      symbol: 'AAPL',
      payload: {
        side: 'buy',
        price: 200,
        quantity: 8,
        orderCount: 2,
        action: 'upsert',
        timestamp: 11,
      },
    })

    const simNeedsResync = store.applyEnvelope({
      type: 'trade',
      sequence: 12,
      symbol: 'SIM',
      payload: {
        tradeId: 1,
        price: 101,
        quantity: 2,
        buyOrderId: 10,
        sellOrderId: 11,
        buyClientId: 1,
        sellClientId: 2,
        timestamp: 12,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(aaplNeedsResync).toBe(false)
    expect(simNeedsResync).toBe(false)
    expect(snapshot.bySymbol['SIM'].trades).toHaveLength(1)
    expect(snapshot.bySymbol['AAPL'].bids[0].quantity).toBe(8)
  })

  it('applies a symbol frame that is older than the global snapshot but newer than that symbol snapshot', () => {
    const store = useMarketDataStore.getState()

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 10,
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

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 20,
      symbol: 'AAPL',
      payload: {
        depth: 20,
        bids: [{ side: 'buy', price: 200, quantity: 3, orderCount: 1 }],
        asks: [{ side: 'sell', price: 201, quantity: 4, orderCount: 1 }],
        bestBid: 200,
        bestAsk: 201,
        spread: 1,
        midPrice: 200,
        timestamp: 20,
      },
    })

    const needsResync = store.applyEnvelope({
      type: 'book_delta',
      sequence: 15,
      symbol: 'SIM',
      payload: {
        side: 'buy',
        price: 100,
        quantity: 9,
        orderCount: 2,
        action: 'upsert',
        timestamp: 15,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(needsResync).toBe(false)
    expect(snapshot.bySymbol['SIM'].bids[0].quantity).toBe(9)
    expect(snapshot.streamSequence).toBe(20)
  })

  it('applies simulation state without forcing resync', () => {
    const needsResync = useMarketDataStore.getState().applyEnvelope({
      type: 'sim_state',
      sequence: 3,
      symbol: 'SIM',
      payload: {
        enabled: true,
        running: true,
        paused: false,
        clockMode: 'accelerated',
        speed: 10,
        volatility: 'high',
        simulationTimestamp: 5000,
        marketMakerCount: 2,
        momentumCount: 3,
        meanReversionCount: 1,
        noiseTraderCount: 1,
        realizedVolatilityBps: 42.5,
        averageSpread: 5.5,
        toxicityScore: 0.35,
        regime: 'stressed',
        limitLambda: 0.03,
        cancelLambda: 0.08,
        marketableLambda: 0.06,
      },
    })

    expect(needsResync).toBe(false)
    expect(useMarketDataStore.getState().bySymbol['SIM'].simulation?.volatility).toBe('high')
    expect(useMarketDataStore.getState().bySymbol['SIM'].simulation?.regime).toBe('stressed')
  })

  it('keeps mid-price chart history by second instead of raw frame count', () => {
    const store = useMarketDataStore.getState()

    for (let i = 0; i < 1000; i += 1) {
      const needsResync = store.applyEnvelope({
        type: 'stats',
        sequence: i + 1,
        symbol: 'SIM',
        payload: {
          tradeCount: i,
          totalVolume: i * 10,
          orderCount: 20,
          bidLevels: 5,
          askLevels: 5,
          bestBid: 100,
          bestAsk: 101,
          spread: 1,
          midPrice: 100 + i,
          timestamp: i * 10,
          messagesPerSecond: 100,
        },
      })

      expect(needsResync).toBe(false)
    }

    const points = useMarketDataStore.getState().bySymbol['SIM'].chartPoints
    expect(points).toHaveLength(10)
    expect(points[0]).toEqual({ timestamp: 990, open: 100, high: 199, low: 100, close: 199 })
    expect(points[9]).toEqual({ timestamp: 9990, open: 1000, high: 1099, low: 1000, close: 1099 })
  })

  it('resets stale sequence and book state when simulation restarts', () => {
    const store = useMarketDataStore.getState()

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 100,
      symbol: 'SIM',
      payload: {
        depth: 20,
        bids: [{ side: 'buy', price: 100, quantity: 5, orderCount: 1 }],
        asks: [{ side: 'sell', price: 101, quantity: 7, orderCount: 1 }],
        bestBid: 100,
        bestAsk: 101,
        spread: 1,
        midPrice: 100,
        timestamp: 1000,
      },
    })

    store.applyEnvelope({
      type: 'sim_state',
      sequence: 101,
      symbol: 'SIM',
      payload: {
        enabled: true,
        running: true,
        paused: false,
        clockMode: 'realtime',
        speed: 1,
        volatility: 'normal',
        simulationTimestamp: 5000,
        marketMakerCount: 2,
        momentumCount: 2,
        meanReversionCount: 1,
        noiseTraderCount: 2,
        realizedVolatilityBps: 10,
        averageSpread: 2,
        toxicityScore: 0.1,
        regime: 'normal',
        limitLambda: 0.01,
        cancelLambda: 0.02,
        marketableLambda: 0.03,
      },
    })

    store.trackOrderId(44)

    const resetNeedsResync = store.applyEnvelope({
      type: 'sim_state',
      sequence: 0,
      symbol: 'SIM',
      payload: {
        enabled: true,
        running: true,
        paused: false,
        clockMode: 'realtime',
        speed: 1,
        volatility: 'normal',
        simulationTimestamp: 0,
        marketMakerCount: 2,
        momentumCount: 2,
        meanReversionCount: 1,
        noiseTraderCount: 2,
        realizedVolatilityBps: 0,
        averageSpread: 0,
        toxicityScore: 0,
        regime: 'normal',
        limitLambda: 0.01,
        cancelLambda: 0.02,
        marketableLambda: 0.03,
      },
    })

    const statsNeedsResync = store.applyEnvelope({
      type: 'stats',
      sequence: 1,
      symbol: 'SIM',
      payload: {
        tradeCount: 0,
        totalVolume: 0,
        orderCount: 0,
        bidLevels: 0,
        askLevels: 0,
        bestBid: null,
        bestAsk: null,
        spread: 0,
        midPrice: 100,
        timestamp: 2000,
        messagesPerSecond: 1,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(resetNeedsResync).toBe(false)
    expect(statsNeedsResync).toBe(false)
    expect(snapshot.streamSequence).toBe(1)
    expect(snapshot.submittedOrderIds.size).toBe(0)
    expect(snapshot.bySymbol['SIM'].bids).toEqual([])
    expect(snapshot.bySymbol['SIM'].chartPoints).toEqual([
      { timestamp: 2000, open: 100, high: 100, low: 100, close: 100 },
    ])
  })

  it('applies agent metrics without changing other agent rows', () => {
    const needsResync = useMarketDataStore.getState().applyEnvelope({
      type: 'agent_metrics',
      sequence: 4,
      symbol: 'SIM',
      payload: {
        clientId: 1000,
        agentName: 'PassiveMarketMaker',
        agentType: 'market_maker',
        timestamp: 10,
        simulationTimestamp: 500,
        wakeCount: 3,
        intentCount: 8,
        submittedCount: 8,
        limitOrderCount: 6,
        marketOrderCount: 0,
        cancelCount: 1,
        modifyCount: 1,
        fillCount: 2,
        filledQuantity: 20,
        restingQuantity: 100,
        liveOrderCount: 4,
        netPosition: 10,
        realizedPnL: 5,
        unrealizedPnL: -1,
        totalPnL: 4,
        averageQueuePosition: 1.5,
        averageQuantityAhead: 40,
        averageFillProbability: 0.42,
        averageTimeToFillMs: 120,
      },
    })

    const metrics = useMarketDataStore.getState().bySymbol['SIM'].agentMetricsByClient[1000]
    expect(needsResync).toBe(false)
    expect(metrics.agentType).toBe('market_maker')
    expect(metrics.averageFillProbability).toBe(0.42)
  })

  it('applies late pnl frames after newer market-data frames', () => {
    const store = useMarketDataStore.getState()

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 20,
      symbol: 'SIM',
      payload: {
        depth: 20,
        bids: [{ side: 'buy', price: 100, quantity: 5, orderCount: 1 }],
        asks: [{ side: 'sell', price: 101, quantity: 7, orderCount: 1 }],
        bestBid: 100,
        bestAsk: 101,
        spread: 1,
        midPrice: 100,
        timestamp: 20,
      },
    })

    store.applyEnvelope({
      type: 'stats',
      sequence: 21,
      symbol: 'SIM',
      payload: {
        tradeCount: 1,
        totalVolume: 10,
        orderCount: 2,
        bidLevels: 1,
        askLevels: 1,
        bestBid: 100,
        bestAsk: 104,
        spread: 4,
        midPrice: 102,
        timestamp: 30,
        messagesPerSecond: 5,
      },
    })

    const needsResync = store.applyEnvelope({
      type: 'pnl',
      sequence: 20,
      symbol: 'SIM',
      payload: {
        clientId: 1,
        netPosition: 10,
        realizedPnL: 0,
        unrealizedPnL: 20,
        totalPnL: 20,
        timestamp: 2,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(needsResync).toBe(false)
    expect(snapshot.streamSequence).toBe(21)
    expect(snapshot.bySymbol['SIM'].pnlByClient[1].totalPnL).toBe(20)
  })

  it('does not let older pnl frames overwrite newer client pnl', () => {
    const store = useMarketDataStore.getState()

    store.applyEnvelope({
      type: 'pnl',
      sequence: 10,
      symbol: 'SIM',
      payload: {
        clientId: 1,
        netPosition: 10,
        realizedPnL: 0,
        unrealizedPnL: 30,
        totalPnL: 30,
        timestamp: 3,
      },
    })

    store.applyEnvelope({
      type: 'pnl',
      sequence: 9,
      symbol: 'SIM',
      payload: {
        clientId: 1,
        netPosition: 10,
        realizedPnL: 0,
        unrealizedPnL: 10,
        totalPnL: 10,
        timestamp: 2,
      },
    })

    expect(useMarketDataStore.getState().bySymbol['SIM'].pnlByClient[1].totalPnL).toBe(30)
  })

  it('keeps per-symbol buckets separate so swapping preserves state', () => {
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

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 1,
      symbol: 'AAPL',
      payload: {
        depth: 20,
        bids: [{ side: 'buy', price: 200, quantity: 3, orderCount: 1 }],
        asks: [{ side: 'sell', price: 201, quantity: 4, orderCount: 1 }],
        bestBid: 200,
        bestAsk: 201,
        spread: 1,
        midPrice: 200,
        timestamp: 11,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(snapshot.symbols).toContain('SIM')
    expect(snapshot.symbols).toContain('AAPL')
    expect(snapshot.bySymbol['SIM'].bids[0].price).toBe(100)
    expect(snapshot.bySymbol['AAPL'].bids[0].price).toBe(200)

    useMarketDataStore.getState().setActiveSymbol('AAPL')
    expect(useMarketDataStore.getState().activeSymbol).toBe('AAPL')

    useMarketDataStore.getState().setActiveSymbol('SIM')
    expect(useMarketDataStore.getState().bySymbol['SIM'].bids[0].quantity).toBe(5)
    expect(useMarketDataStore.getState().bySymbol['AAPL'].bids[0].quantity).toBe(3)
  })

  it('prunes stale symbols when simulation state reports the server symbol list', () => {
    const store = useMarketDataStore.getState()

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 1,
      symbol: 'SIM',
      payload: {
        depth: 20,
        bids: [],
        asks: [],
        bestBid: null,
        bestAsk: null,
        spread: 0,
        midPrice: 0,
        timestamp: 0,
      },
    })

    store.applyEnvelope({
      type: 'snapshot',
      sequence: 1,
      symbol: 'AAPL',
      payload: {
        depth: 20,
        bids: [{ side: 'buy', price: 200, quantity: 3, orderCount: 1 }],
        asks: [],
        bestBid: 200,
        bestAsk: null,
        spread: 0,
        midPrice: 0,
        timestamp: 0,
      },
    })
    store.setActiveSymbol('AAPL')

    store.applyEnvelope({
      type: 'sim_state',
      sequence: 2,
      symbol: 'SIM',
      payload: {
        symbols: ['SIM'],
        enabled: true,
        running: true,
        paused: false,
        clockMode: 'realtime',
        speed: 1,
        volatility: 'normal',
        simulationTimestamp: 10,
        marketMakerCount: 2,
        momentumCount: 2,
        meanReversionCount: 1,
        noiseTraderCount: 2,
        realizedVolatilityBps: 0,
        averageSpread: 0,
        toxicityScore: 0,
        regime: 'normal',
        limitLambda: 0.01,
        cancelLambda: 0.02,
        marketableLambda: 0.03,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(snapshot.symbols).toEqual(['SIM'])
    expect(snapshot.activeSymbol).toBe('SIM')
    expect(snapshot.bySymbol['SIM'].simulation?.symbols).toEqual(['SIM'])
    expect(snapshot.bySymbol['AAPL']).toBeUndefined()
  })

  it('registers a new symbol on first envelope and makes it active if none was set', () => {
    useMarketDataStore.setState({
      activeSymbol: '',
      symbols: [],
      bySymbol: {},
    })

    useMarketDataStore.getState().applyEnvelope({
      type: 'snapshot',
      sequence: 1,
      symbol: 'GOOG',
      payload: {
        depth: 20,
        bids: [],
        asks: [],
        bestBid: null,
        bestAsk: null,
        spread: 0,
        midPrice: 0,
        timestamp: 0,
      },
    })

    const snapshot = useMarketDataStore.getState()
    expect(snapshot.symbols).toEqual(['GOOG'])
    expect(snapshot.activeSymbol).toBe('GOOG')
  })
})
