import { formatSigned } from '../lib/format'
import { useActiveBucket, useMarketDataStore } from '../store/market-data-store'
import { Card, CardBody, CardHeader } from './ui/card'

function signClass(value: number | undefined | null) {
  if (value === undefined || value === null || value === 0) {
    return 'text-[color:var(--color-text-primary)]'
  }
  return value > 0 ? 'text-[color:var(--color-buy)]' : 'text-[color:var(--color-sell)]'
}

function Row({ label, value, valueClass }: { label: string; value: string; valueClass?: string }) {
  return (
    <div className="terminal-row flex items-baseline justify-between px-2 py-1 last:border-b-0">
      <span className="text-[10.5px] uppercase text-[color:var(--color-text-muted)]">
        {label}
      </span>
      <span className={`num text-[13px] font-bold ${valueClass ?? 'text-[color:var(--color-text-primary)]'}`}>
        {value}
      </span>
    </div>
  )
}

export function PnLCard() {
  const activeClientId = useMarketDataStore((state) => state.activeClientId)
  const pnl = useActiveBucket().pnlByClient[activeClientId]

  const net = pnl?.netPosition ?? 0
  const total = pnl?.totalPnL ?? 0

  return (
    <Card>
      <CardHeader
        title="Positions"
        subtitle={`client ${activeClientId}`}
        actions={
          <span
            className={`num text-[12px] font-semibold ${signClass(total)}`}
          >
            {formatSigned(total)}
          </span>
        }
      />
      <CardBody flush>
        <Row
          label="Net Pos"
          value={net.toLocaleString()}
          valueClass={signClass(net)}
        />
        <Row
          label="Total PnL"
          value={formatSigned(total)}
          valueClass={signClass(total)}
        />
        <Row
          label="Realized"
          value={formatSigned(pnl?.realizedPnL ?? 0)}
          valueClass={signClass(pnl?.realizedPnL ?? 0)}
        />
        <Row
          label="Unrealized"
          value={formatSigned(pnl?.unrealizedPnL ?? 0)}
          valueClass={signClass(pnl?.unrealizedPnL ?? 0)}
        />
      </CardBody>
    </Card>
  )
}
