import { useDeferredValue, useMemo } from 'react'
import { formatPrice } from '../lib/format'
import { useMarketDataStore } from '../store/market-data-store'
import { Card, CardBody, CardHeader } from './ui/card'

function Row({
  price,
  quantity,
  orderCount,
  side,
  widthPct,
  cumulative,
}: {
  price: number
  quantity: number
  orderCount: number
  side: 'buy' | 'sell'
  widthPct: number
  cumulative: number
}) {
  const priceClass =
    side === 'buy' ? 'text-[color:var(--color-buy)]' : 'text-[color:var(--color-sell)]'
  const bgColor = side === 'buy' ? 'var(--color-buy-dim)' : 'var(--color-sell-dim)'

  return (
    <div className="group relative grid grid-cols-[1fr_1fr_1fr_0.6fr] items-center px-2 py-[2px] text-[12px] hover:bg-[color:var(--color-bg-row-hover)]">
      <div
        className="absolute inset-y-0 right-0 transition-[width] duration-150"
        style={{ width: `${widthPct}%`, background: bgColor }}
      />
      <span className={`num relative ${priceClass}`}>{formatPrice(price)}</span>
      <span className="num relative text-right text-[color:var(--color-text-primary)]">
        {quantity.toLocaleString()}
      </span>
      <span className="num relative text-right text-[color:var(--color-text-secondary)]">
        {cumulative.toLocaleString()}
      </span>
      <span className="num relative text-right text-[color:var(--color-text-muted)]">
        {orderCount}
      </span>
    </div>
  )
}

function Header() {
  return (
    <div className="grid grid-cols-[1fr_1fr_1fr_0.6fr] border-b border-[color:var(--color-border-subtle)] px-2 py-1 text-[9.5px] font-semibold uppercase tracking-wider text-[color:var(--color-text-muted)]">
      <span>Price</span>
      <span className="text-right">Size</span>
      <span className="text-right">Sum</span>
      <span className="text-right">Ord</span>
    </div>
  )
}

export function OrderBookLadder() {
  const asks = useDeferredValue(useMarketDataStore((state) => state.asks))
  const bids = useDeferredValue(useMarketDataStore((state) => state.bids))
  const stats = useMarketDataStore((state) => state.stats)

  const maxQuantity = useMemo(() => {
    let max = 0
    for (const lvl of asks) max = Math.max(max, lvl.quantity)
    for (const lvl of bids) max = Math.max(max, lvl.quantity)
    return max
  }, [asks, bids])

  const askRows = useMemo(() => {
    const reversed = asks.slice().reverse()
    let running = 0
    const totals = asks.map((lvl) => (running += lvl.quantity))
    const cumMap = new Map(asks.map((lvl, i) => [lvl.price, totals[i]]))
    return reversed.map((lvl) => ({ ...lvl, cumulative: cumMap.get(lvl.price) ?? lvl.quantity }))
  }, [asks])

  const bidRows = useMemo(() => {
    let running = 0
    return bids.map((lvl) => {
      running += lvl.quantity
      return { ...lvl, cumulative: running }
    })
  }, [bids])

  const mid = stats?.midPrice ?? null
  const spread = stats?.spread ?? 0

  return (
    <Card>
      <CardHeader
        title="Order Book"
        subtitle="L2 Depth"
        actions={
          <span className="num text-[10.5px] text-[color:var(--color-text-muted)]">
            {asks.length}A · {bids.length}B
          </span>
        }
      />
      <CardBody flush>
        <div className="flex h-full min-h-0 flex-col">
          <Header />

          {/* Asks (top, reversed) */}
          <div className="flex-1 min-h-0 overflow-y-auto">
            {askRows.length === 0 ? (
              <div className="px-2 py-3 text-center text-[11px] text-[color:var(--color-text-muted)]">
                No asks
              </div>
            ) : (
              askRows.map((lvl) => (
                <Row
                  key={`ask-${lvl.price}`}
                  price={lvl.price}
                  quantity={lvl.quantity}
                  orderCount={lvl.orderCount}
                  side="sell"
                  widthPct={maxQuantity > 0 ? (lvl.quantity / maxQuantity) * 100 : 0}
                  cumulative={lvl.cumulative}
                />
              ))
            )}
          </div>

          {/* Spread marker */}
          <div className="flex items-center justify-between border-y border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-header)] px-2 py-1 text-[11px]">
            <span className="num font-semibold text-[color:var(--color-text-primary)]">
              {formatPrice(mid)}
            </span>
            <span className="text-[9.5px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
              Spread
            </span>
            <span className="num text-[color:var(--color-warn)]">{formatPrice(spread)}</span>
          </div>

          {/* Bids (bottom, best-first) */}
          <div className="flex-1 min-h-0 overflow-y-auto">
            {bidRows.length === 0 ? (
              <div className="px-2 py-3 text-center text-[11px] text-[color:var(--color-text-muted)]">
                No bids
              </div>
            ) : (
              bidRows.map((lvl) => (
                <Row
                  key={`bid-${lvl.price}`}
                  price={lvl.price}
                  quantity={lvl.quantity}
                  orderCount={lvl.orderCount}
                  side="buy"
                  widthPct={maxQuantity > 0 ? (lvl.quantity / maxQuantity) * 100 : 0}
                  cumulative={lvl.cumulative}
                />
              ))
            )}
          </div>
        </div>
      </CardBody>
    </Card>
  )
}
