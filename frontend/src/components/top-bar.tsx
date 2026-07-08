import { useEffect, useState } from 'react'
import { Badge } from './ui/badge'
import { formatPrice } from '../lib/format'
import {
  useActiveBucket,
  useActiveSymbol,
  useAvailableSymbols,
  useMarketDataStore,
} from '../store/market-data-store'

function useClock() {
  const [now, setNow] = useState(() => new Date())
  useEffect(() => {
    const id = window.setInterval(() => setNow(new Date()), 1000)
    return () => window.clearInterval(id)
  }, [])
  return now
}

function formatClock(date: Date) {
  return date.toLocaleTimeString([], { hour12: false })
}

function SymbolSelector() {
  const symbols = useAvailableSymbols()
  const activeSymbol = useActiveSymbol()
  const setActiveSymbol = useMarketDataStore((state) => state.setActiveSymbol)

  if (symbols.length <= 1) {
    return (
      <span className="text-[12px] font-semibold text-[color:var(--color-text-primary)]">
        {activeSymbol || '--'}
      </span>
    )
  }

  return (
    <select
      value={activeSymbol}
      onChange={(event) => setActiveSymbol(event.target.value)}
      className="h-5 border border-[color:var(--color-border-strong)] bg-black px-1.5 text-[10.5px] font-bold text-[color:var(--color-text-primary)] outline-none focus:border-[color:var(--color-accent)]"
      aria-label="Active symbol"
    >
      {symbols.map((symbol) => (
        <option key={symbol} value={symbol}>
          {symbol}
        </option>
      ))}
    </select>
  )
}

export function TopBar() {
  const connection = useMarketDataStore((state) => state.connectionState)
  const bucket = useActiveBucket()
  const stats = bucket.stats
  const simulation = bucket.simulation
  const now = useClock()

  const mid = stats?.midPrice ?? null
  const spread = stats?.spread ?? 0

  const connTone = connection === 'connected' ? 'live' : connection === 'connecting' ? 'warn' : 'sell'

  return (
    <header className="flex min-h-5 items-center justify-between gap-4 border-b border-[color:var(--color-border-subtle)] bg-black px-1.5 py-[2px]">
      <div className="flex min-w-0 items-center gap-2">
        <span className="text-[10px] font-bold text-[color:var(--color-text-primary)]">
          MERCURY
        </span>
        <span className="hidden text-[10px] text-[color:var(--color-border-strong)] sm:inline">|</span>
        <div className="flex min-w-0 items-center gap-1.5">
          <SymbolSelector />
          <span className="num text-[10.5px] font-bold text-[color:var(--color-text-primary)]">
            {formatPrice(mid)}
          </span>
          <span className="num text-[10.5px] text-[color:var(--color-warn)]">
            SPR {formatPrice(spread)}
          </span>
        </div>
      </div>

      <div className="flex shrink-0 items-center gap-2">
        <span className="num text-[10px] text-[color:var(--color-text-secondary)]">
          {formatClock(now)}
        </span>
        <Badge tone={connTone} dot>
          {connection}
        </Badge>
        {simulation?.enabled ? (
          <Badge tone={simulation.paused ? 'warn' : 'live'}>
            sim {simulation.paused ? 'paused' : simulation.volatility}
          </Badge>
        ) : null}
      </div>
    </header>
  )
}
