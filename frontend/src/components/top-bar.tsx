import { useEffect, useState } from 'react'
import { Badge } from './ui/badge'
import { formatPrice } from '../lib/format'
import { useMarketDataStore } from '../store/market-data-store'

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

export function TopBar() {
  const symbol = useMarketDataStore((state) => state.symbol)
  const connection = useMarketDataStore((state) => state.connectionState)
  const stats = useMarketDataStore((state) => state.stats)
  const now = useClock()

  const mid = stats?.midPrice ?? null
  const spread = stats?.spread ?? 0

  const connTone = connection === 'connected' ? 'live' : connection === 'connecting' ? 'warn' : 'sell'

  return (
    <header className="flex items-center justify-between gap-4 border-b border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel)] px-3 py-1.5">
      <div className="flex items-center gap-4">
        <div className="flex items-center gap-2">
          <div className="flex h-5 w-5 items-center justify-center rounded-sm bg-[color:var(--color-accent)] text-[10px] font-bold text-white">
            M
          </div>
          <span className="text-[13px] font-semibold tracking-tight text-[color:var(--color-text-primary)]">
            Mercury
          </span>
          <span className="text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
            Terminal
          </span>
        </div>

        <div className="h-4 w-px bg-[color:var(--color-border-subtle)]" />

        <div className="flex items-center gap-3">
          <span className="text-[12px] font-semibold text-[color:var(--color-text-primary)]">{symbol}</span>
          <span className="num text-[12px] text-[color:var(--color-text-primary)]">
            {formatPrice(mid)}
          </span>
          <span className="num text-[11px] text-[color:var(--color-text-secondary)]">
            spr {formatPrice(spread)}
          </span>
        </div>
      </div>

      <div className="flex items-center gap-3">
        <Badge tone={connTone} dot>
          {connection}
        </Badge>
        <span className="num text-[11px] text-[color:var(--color-text-secondary)]">
          {formatClock(now)}
        </span>
      </div>
    </header>
  )
}
