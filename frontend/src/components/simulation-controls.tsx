import { useMemo, useState } from 'react'
import { useActiveBucket } from '../store/market-data-store'
import { Button } from './ui/button'
import { Card, CardBody, CardHeader } from './ui/card'

const SCENARIOS = [
  ['calm-two-sided-market', 'Calm two-sided'],
  ['toxic-flow', 'Toxic flow'],
  ['thin-book-stress', 'Thin stress'],
  ['high-cancel-rate', 'High cancel'],
  ['momentum-burst', 'Momentum burst'],
] as const

async function postControl(payload: Record<string, unknown>) {
  const response = await fetch('/api/simulation/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })

  if (!response.ok) {
    const text = await response.text()
    throw new Error(text || `HTTP ${response.status}`)
  }
}

function formatLambda(value: number | undefined): string {
  if (value === undefined) return '--'
  return value.toFixed(3)
}

function num(value: number | undefined, fallback = 0): number {
  return Number.isFinite(value) ? Number(value) : fallback
}

function formNumber(form: HTMLFormElement, key: string): number {
  return Number(new FormData(form).get(key) ?? 0)
}

export function SimulationControls() {
  const bucket = useActiveBucket()
  const simulation = bucket.simulation
  const agentMetrics = useMemo(
    () =>
      Object.values(bucket.agentMetricsByClient)
        .sort((left, right) => right.totalPnL - left.totalPnL)
        .slice(0, 4),
    [bucket.agentMetricsByClient],
  )

  const [busy, setBusy] = useState(false)
  const [scenario, setScenario] = useState('calm-two-sided-market')

  const disabled = !simulation?.enabled || busy
  const pauseLabel = simulation?.paused ? 'Resume' : 'Pause'

  async function runControl(payload: Record<string, unknown>) {
    setBusy(true)
    try {
      await postControl(payload)
    } finally {
      setBusy(false)
    }
  }

  const inputClass =
    'h-8 w-full rounded-sm border border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel-alt)] px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none'

  return (
    <Card>
      <CardHeader title="Simulation" subtitle="Operator" />
      <CardBody>
        <div className="space-y-3">
          <div className="grid grid-cols-2 gap-2 text-[11px] text-[color:var(--color-text-secondary)]">
            <Metric label="Status" value={simulation?.enabled ? (simulation.paused ? 'paused' : 'running') : 'off'} />
            <Metric label="Clock" value={simulation?.clockMode ?? '--'} />
            <Metric label="Vol" value={simulation?.volatility ?? '--'} />
            <Metric label="Regime" value={simulation?.regime ?? '--'} />
            <Metric label="Limit/ms" value={formatLambda(simulation?.limitLambda)} />
            <Metric label="Mkt/ms" value={formatLambda(simulation?.marketableLambda)} />
          </div>

          <div className="grid grid-cols-2 gap-2">
            <Button
              variant="default"
              disabled={disabled}
              onClick={() => runControl({ action: simulation?.paused ? 'resume' : 'pause' })}
            >
              {pauseLabel}
            </Button>
            <Button variant="default" disabled={disabled} onClick={() => runControl({ action: 'restart' })}>
              Restart
            </Button>
          </div>

          <div className="grid grid-cols-2 gap-2">
            <SelectControl
              label="Volatility"
              value={simulation?.volatility ?? 'normal'}
              disabled={disabled}
              options={[
                ['low', 'Low'],
                ['normal', 'Normal'],
                ['high', 'High'],
              ]}
              onChange={(value) => runControl({ action: 'set_volatility', volatility: value })}
            />
            <SelectControl
              label="Regime"
              value={simulation?.regime ?? 'normal'}
              disabled={disabled}
              options={[
                ['calm', 'Calm'],
                ['normal', 'Normal'],
                ['stressed', 'Stressed'],
              ]}
              onChange={(value) => runControl({ action: 'set_regime', volatility: value })}
            />
          </div>

          <div className="space-y-1">
            <label className="text-[10.5px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
              Scenario
            </label>
            <div className="grid grid-cols-[minmax(0,1fr)_5rem] gap-2">
              <select
                className={inputClass}
                disabled={disabled}
                value={scenario}
                onChange={(event) => setScenario(event.target.value)}
              >
                {SCENARIOS.map(([id, label]) => (
                  <option key={id} value={id}>
                    {label}
                  </option>
                ))}
              </select>
              <Button
                variant="secondary"
                disabled={disabled}
                onClick={() => runControl({ action: 'apply_scenario', scenario })}
              >
                Apply
              </Button>
            </div>
          </div>

          <form
            key={`${simulation?.marketMakerCount}-${simulation?.momentumCount}-${simulation?.meanReversionCount}-${simulation?.noiseTraderCount}`}
            className="space-y-2"
            onSubmit={(event) => {
              event.preventDefault()
              runControl({
                action: 'set_counts',
                marketMakerCount: formNumber(event.currentTarget, 'marketMakerCount'),
                momentumCount: formNumber(event.currentTarget, 'momentumCount'),
                meanReversionCount: formNumber(event.currentTarget, 'meanReversionCount'),
                noiseTraderCount: formNumber(event.currentTarget, 'noiseTraderCount'),
              })
            }}
          >
            <div className="grid grid-cols-4 gap-1.5">
              <NumberField label="MM" name="marketMakerCount" defaultValue={simulation?.marketMakerCount ?? 0} disabled={disabled} />
              <NumberField label="Mom" name="momentumCount" defaultValue={simulation?.momentumCount ?? 0} disabled={disabled} />
              <NumberField label="MR" name="meanReversionCount" defaultValue={simulation?.meanReversionCount ?? 0} disabled={disabled} />
              <NumberField label="Noise" name="noiseTraderCount" defaultValue={simulation?.noiseTraderCount ?? 0} disabled={disabled} />
            </div>
            <Button className="w-full" variant="secondary" disabled={disabled} type="submit">
              Apply Agents
            </Button>
          </form>

          <form
            key={`${simulation?.marketMaker?.levels}-${simulation?.marketMaker?.quoteQuantity}-${simulation?.marketMaker?.minQuantity}-${simulation?.marketMaker?.baseSpreadTicks}-${simulation?.marketMaker?.toxicitySensitivity}-${simulation?.marketMaker?.wakeIntervalMs}`}
            className="space-y-2"
            onSubmit={(event) => {
              event.preventDefault()
              runControl({
                action: 'set_market_maker',
                marketMaker: {
                  levels: formNumber(event.currentTarget, 'levels'),
                  quoteQuantity: formNumber(event.currentTarget, 'quoteQuantity'),
                  minQuantity: formNumber(event.currentTarget, 'minQuantity'),
                  baseSpreadTicks: formNumber(event.currentTarget, 'baseSpreadTicks'),
                  toxicitySensitivity: formNumber(event.currentTarget, 'toxicitySensitivity'),
                  wakeIntervalMs: formNumber(event.currentTarget, 'wakeIntervalMs'),
                },
              })
            }}
          >
            <div className="grid grid-cols-3 gap-1.5">
              <NumberField
                label="Levels"
                name="levels"
                defaultValue={num(simulation?.marketMaker?.levels)}
                disabled={disabled}
              />
              <NumberField
                label="Qty"
                name="quoteQuantity"
                defaultValue={num(simulation?.marketMaker?.quoteQuantity)}
                disabled={disabled}
              />
              <NumberField
                label="Spread"
                name="baseSpreadTicks"
                defaultValue={num(simulation?.marketMaker?.baseSpreadTicks)}
                disabled={disabled}
              />
              <NumberField
                label="Min"
                name="minQuantity"
                defaultValue={num(simulation?.marketMaker?.minQuantity)}
                disabled={disabled}
              />
              <NumberField
                label="Toxic"
                name="toxicitySensitivity"
                defaultValue={num(simulation?.marketMaker?.toxicitySensitivity, 1)}
                step={0.1}
                disabled={disabled}
              />
              <NumberField
                label="Wake"
                name="wakeIntervalMs"
                defaultValue={num(simulation?.marketMaker?.wakeIntervalMs)}
                disabled={disabled}
              />
            </div>
            <Button className="w-full" variant="secondary" disabled={disabled} type="submit">
              Apply Market Maker
            </Button>
          </form>

          <div className="space-y-1">
            <div className="text-[10.5px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
              Agent Attribution
            </div>
            <div className="max-h-28 overflow-auto rounded-sm border border-[color:var(--color-border-subtle)]">
              {agentMetrics.length === 0 ? (
                <div className="px-2 py-2 text-[11px] text-[color:var(--color-text-muted)]">No agent metrics</div>
              ) : (
                agentMetrics.map((agent) => (
                  <div
                    key={`${agent.clientId}-${agent.agentType}`}
                    className="grid grid-cols-[minmax(0,1fr)_4rem_4rem] gap-2 border-b border-[color:var(--color-border-subtle)] px-2 py-1.5 text-[11px] last:border-b-0"
                  >
                    <span className="truncate text-[color:var(--color-text-secondary)]">
                      {agent.agentType} #{agent.clientId}
                    </span>
                    <span className="num text-right">{agent.totalPnL}</span>
                    <span className="num text-right">{agent.averageFillProbability.toFixed(2)}</span>
                  </div>
                ))
              )}
            </div>
          </div>
        </div>
      </CardBody>
    </Card>
  )
}

function Metric({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="flex justify-between rounded-sm bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5">
      <span>{label}</span>
      <span className="num truncate pl-2 text-[color:var(--color-text-primary)]">{value}</span>
    </div>
  )
}

function SelectControl({
  label,
  value,
  disabled,
  options,
  onChange,
}: {
  label: string
  value: string
  disabled: boolean
  options: Array<readonly [string, string]>
  onChange: (value: string) => void
}) {
  return (
    <div className="space-y-1">
      <label className="text-[10.5px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
        {label}
      </label>
      <select
        className="h-8 w-full rounded-sm border border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel-alt)] px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none"
        disabled={disabled}
        value={value}
        onChange={(event) => onChange(event.target.value)}
      >
        {options.map(([id, optionLabel]) => (
          <option key={id} value={id}>
            {optionLabel}
          </option>
        ))}
      </select>
    </div>
  )
}

function NumberField({
  label,
  name,
  defaultValue,
  step = 1,
  disabled,
}: {
  label: string
  name: string
  defaultValue: number
  step?: number
  disabled: boolean
}) {
  return (
    <label className="space-y-1">
      <span className="block text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">{label}</span>
      <input
        className="h-8 w-full rounded-sm border border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel-alt)] px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none"
        type="number"
        min="0"
        name={name}
        step={step}
        disabled={disabled}
        defaultValue={defaultValue}
      />
    </label>
  )
}
