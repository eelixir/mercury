import { useActiveBucket, useMarketDataStore } from '../store/market-data-store'
import { Card, CardBody, CardHeader } from './ui/card'

function latencyColor(ns: number | null): string {
  if (ns === null) return 'text-[color:var(--color-text-muted)]'
  const us = ns / 1000
  if (us < 100) return 'text-[color:var(--color-buy)]'
  if (us < 1000) return 'text-[color:var(--color-warn)]'
  return 'text-[color:var(--color-sell)]'
}

function formatLatency(ns: number | null): string {
  if (ns === null) return '--'
  const us = ns / 1000
  if (us < 1000) return `${us.toFixed(1)} µs`
  return `${(us / 1000).toFixed(2)} ms`
}

function Metric({
  label,
  value,
  valueClass,
}: {
  label: string
  value: string
  valueClass?: string
}) {
  return (
    <div className="flex items-baseline justify-between border-b border-[color:var(--color-border-subtle)] px-2 py-1 last:border-b-0">
      <span className="text-[10.5px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
        {label}
      </span>
      <span className={`num text-[13px] font-semibold ${valueClass ?? 'text-[color:var(--color-text-primary)]'}`}>
        {value}
      </span>
    </div>
  )
}

export function SystemHealth() {
  const bucket = useActiveBucket()
  const latencyNs = bucket.engineLatencyNs
  const mps = bucket.messagesPerSecond
  const connection = useMarketDataStore((state) => state.connectionState)

  const latencyClass = latencyColor(latencyNs)
  const mpsClass =
    mps > 0 ? 'text-[color:var(--color-buy)]' : 'text-[color:var(--color-text-muted)]'

  const connDot =
    connection === 'connected'
      ? 'bg-[color:var(--color-buy)]'
      : connection === 'connecting'
        ? 'bg-[color:var(--color-warn)]'
        : 'bg-[color:var(--color-sell)]'

  return (
    <Card>
      <CardHeader
        title="System Health"
        subtitle="Engine"
        actions={
          <span className={`inline-block h-1.5 w-1.5 rounded-full pulse-dot ${connDot}`} />
        }
      />
      <CardBody flush>
        <Metric label="Engine Latency" value={formatLatency(latencyNs)} valueClass={latencyClass} />
        <Metric label="Throughput" value={mps > 0 ? `${mps.toLocaleString()} msg/s` : '--'} valueClass={mpsClass} />
        <Metric label="Connection" value={connection} />
      </CardBody>
    </Card>
  )
}
