import { useActiveBucket, useMarketDataStore } from '../store/market-data-store'

export function StatusBar() {
  const bucket = useActiveBucket()
  const simulation = bucket.simulation
  const activeClientId = useMarketDataStore((state) => state.activeClientId)

  return (
    <footer className="flex items-center justify-between gap-4 border-t border-[color:var(--color-border-strong)] bg-black px-2 py-1 text-[10.5px] uppercase text-[color:var(--color-text-muted)]">
      <div className="flex min-w-0 items-center gap-3 overflow-hidden">
        <span>
          Client <span className="num text-[color:var(--color-text-secondary)]">#{activeClientId}</span>
        </span>
        {simulation?.enabled ? (
          <span>
            Sim t <span className="num text-[color:var(--color-text-secondary)]">{simulation.simulationTimestamp}</span>
          </span>
        ) : null}
      </div>
      <div className="flex shrink-0 items-center gap-3">
        <span>Local (UTC{getUtcOffset()})</span>
        <span className="text-[color:var(--color-accent)]">v0.1</span>
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
