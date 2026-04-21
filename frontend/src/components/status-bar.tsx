import { useActiveBucket, useMarketDataStore } from '../store/market-data-store'

export function StatusBar() {
  const bucket = useActiveBucket()
  const stats = bucket.stats
  const simulation = bucket.simulation
  const connection = useMarketDataStore((state) => state.connectionState)
  const activeClientId = useMarketDataStore((state) => state.activeClientId)

  return (
    <footer className="flex items-center justify-between gap-4 border-t border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel)] px-3 py-1 text-[10.5px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
      <div className="flex items-center gap-4">
        <span>
          WS <span className="text-[color:var(--color-text-secondary)]">{connection}</span>
        </span>
        <span>
          Client <span className="num text-[color:var(--color-text-secondary)]">#{activeClientId}</span>
        </span>
        <span>
          Trades <span className="num text-[color:var(--color-text-secondary)]">{stats?.tradeCount ?? 0}</span>
        </span>
        <span>
          Volume <span className="num text-[color:var(--color-text-secondary)]">{stats?.totalVolume ?? 0}</span>
        </span>
        <span>
          Bid lvls <span className="num text-[color:var(--color-text-secondary)]">{stats?.bidLevels ?? 0}</span>
        </span>
        <span>
          Ask lvls <span className="num text-[color:var(--color-text-secondary)]">{stats?.askLevels ?? 0}</span>
        </span>
        {simulation?.enabled ? (
          <span>
            Sim t <span className="num text-[color:var(--color-text-secondary)]">{simulation.simulationTimestamp}</span>
          </span>
        ) : null}
      </div>
      <div className="flex items-center gap-4">
        <span>Local (UTC{getUtcOffset()})</span>
        <span>v0.1</span>
      </div>
    </footer>
  )
}

function getUtcOffset() {
  const mins = -new Date().getTimezoneOffset()
  const sign = mins >= 0 ? '+' : '-'
  const abs = Math.abs(mins)
  const h = Math.floor(abs / 60)
  return `${sign}${h}`
}
