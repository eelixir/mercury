import { useDeferredValue, useMemo } from 'react'
import { formatClock, formatPrice } from '../lib/format'
import { useMarketDataStore } from '../store/market-data-store'
import { Card, CardBody, CardHeader } from './ui/card'

export function TradeTape() {
  const trades = useDeferredValue(useMarketDataStore((state) => state.trades))

  const decoratedTrades = useMemo(() => {
    // `trades` is newest-first. Compare each trade's price against the next
    // (older) trade to classify uptick / downtick.
    return trades.map((trade, index) => {
      const older = trades[index + 1]
      const direction: 'up' | 'down' | 'flat' = !older
        ? 'flat'
        : trade.price > older.price
          ? 'up'
          : trade.price < older.price
            ? 'down'
            : 'flat'
      return { trade, direction }
    })
  }, [trades])

  const lastPrice = trades[0]?.price ?? null

  return (
    <Card>
      <CardHeader
        title="Time & Sales"
        subtitle="Trade Tape"
        actions={
          lastPrice !== null ? (
            <span className="num text-[11px] text-[color:var(--color-text-primary)]">
              last {formatPrice(lastPrice)}
            </span>
          ) : null
        }
      />
      <CardBody flush>
        <div className="flex h-full min-h-0 flex-col">
          <div className="grid grid-cols-[0.9fr_1fr_1fr_0.8fr] border-b border-[color:var(--color-border-subtle)] px-2 py-1 text-[9.5px] font-semibold uppercase tracking-wider text-[color:var(--color-text-muted)]">
            <span>Time</span>
            <span className="text-right">Price</span>
            <span className="text-right">Size</span>
            <span className="text-right">Value</span>
          </div>

          <div className="flex-1 min-h-0 overflow-y-auto">
            {decoratedTrades.length === 0 ? (
              <div className="px-2 py-6 text-center text-[11px] text-[color:var(--color-text-muted)]">
                Waiting for first trade
              </div>
            ) : (
              decoratedTrades.map(({ trade, direction }) => {
                const priceClass =
                  direction === 'up'
                    ? 'text-[color:var(--color-buy)]'
                    : direction === 'down'
                      ? 'text-[color:var(--color-sell)]'
                      : 'text-[color:var(--color-text-primary)]'
                const arrow = direction === 'up' ? '▲' : direction === 'down' ? '▼' : '·'
                return (
                  <div
                    key={trade.tradeId}
                    className="grid grid-cols-[0.9fr_1fr_1fr_0.8fr] items-center px-2 py-[2px] text-[12px] hover:bg-[color:var(--color-bg-row-hover)]"
                  >
                    <span className="num text-[color:var(--color-text-muted)]">
                      {formatClock(trade.timestamp)}
                    </span>
                    <span className={`num text-right ${priceClass}`}>
                      <span className="mr-1 text-[9px] opacity-70">{arrow}</span>
                      {formatPrice(trade.price)}
                    </span>
                    <span className="num text-right text-[color:var(--color-text-primary)]">
                      {trade.quantity.toLocaleString()}
                    </span>
                    <span className="num text-right text-[color:var(--color-text-secondary)]">
                      {(trade.price * trade.quantity).toLocaleString(undefined, {
                        maximumFractionDigits: 0,
                      })}
                    </span>
                  </div>
                )
              })
            )}
          </div>
        </div>
      </CardBody>
    </Card>
  )
}
