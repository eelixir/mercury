import type { ReactNode } from 'react'
import { formatPrice } from '../lib/format'
import { useActiveBucket } from '../store/market-data-store'

function Tile({
  label,
  value,
  accent,
}: {
  label: string
  value: ReactNode
  accent?: 'buy' | 'sell' | 'warn' | 'neutral'
}) {
  const colorClass =
    accent === 'buy'
      ? 'text-[color:var(--color-buy)]'
      : accent === 'sell'
        ? 'text-[color:var(--color-sell)]'
        : accent === 'warn'
          ? 'text-[color:var(--color-warn)]'
          : 'text-[color:var(--color-text-primary)]'

  return (
    <div className="flex min-w-0 flex-col justify-center border-r border-[color:var(--color-border-subtle)] px-3 py-1">
      <div className="text-[9.5px] font-semibold uppercase tracking-wider text-[color:var(--color-text-muted)]">
        {label}
      </div>
      <div className={`num text-[13px] font-semibold ${colorClass}`}>{value}</div>
    </div>
  )
}

export function StatsStrip() {
  const stats = useActiveBucket().stats

  const bid = stats?.bestBid ?? null
  const ask = stats?.bestAsk ?? null
  const mid = stats?.midPrice ?? null
  const spread = stats?.spread ?? null
  const spreadBps = mid && spread ? (spread / mid) * 10000 : null

  return (
    <div className="flex items-stretch overflow-x-auto border-b border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel-alt)]">
      <Tile label="Bid" value={formatPrice(bid)} accent="buy" />
      <Tile label="Ask" value={formatPrice(ask)} accent="sell" />
      <Tile label="Mid" value={formatPrice(mid)} />
      <Tile label="Spread" value={formatPrice(spread)} accent="warn" />
      <Tile label="Spr (bps)" value={spreadBps !== null ? spreadBps.toFixed(1) : '--'} accent="warn" />
      <Tile label="Trades" value={stats?.tradeCount ?? 0} />
      <Tile label="Volume" value={(stats?.totalVolume ?? 0).toLocaleString()} />
      <Tile label="Orders" value={stats?.orderCount ?? 0} />
      <Tile label="Bid Lvls" value={stats?.bidLevels ?? 0} accent="buy" />
      <Tile label="Ask Lvls" value={stats?.askLevels ?? 0} accent="sell" />
    </div>
  )
}
