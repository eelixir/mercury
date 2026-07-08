import { useEffect, useMemo, useState, type ReactNode } from 'react'
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

async function postReplayControl(payload: Record<string, unknown>): Promise<{ replayActive?: boolean }> {
  const response = await fetch('/api/replay/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })

  const text = await response.text()
  if (!response.ok) {
    throw new Error(text || `HTTP ${response.status}`)
  }
  return JSON.parse(text) as { replayActive?: boolean }
}

async function fetchReplayActive(): Promise<boolean> {
  const response = await fetch('/api/health')
  if (!response.ok) return false
  const payload = (await response.json()) as { replayActive?: boolean }
  return Boolean(payload.replayActive)
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
  const marketMaker = simulation?.marketMaker
  const agentMetrics = useMemo(
    () =>
      Object.values(bucket.agentMetricsByClient)
        .sort((left, right) => right.totalPnL - left.totalPnL)
        .slice(0, 4),
    [bucket.agentMetricsByClient],
  )

  const [busy, setBusy] = useState(false)
  const [scenario, setScenario] = useState('calm-two-sided-market')
  const [replayActive, setReplayActive] = useState(false)
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)

  const disabled = !simulation?.enabled || busy
  const pauseLabel = simulation?.paused ? 'Resume' : 'Pause'

  useEffect(() => {
    let disposed = false
    fetchReplayActive()
      .then((active) => {
        if (!disposed) setReplayActive(active)
      })
      .catch(() => {
        // Health polling is non-critical; controls report actionable errors.
      })

    const id = window.setInterval(() => {
      fetchReplayActive()
        .then((active) => {
          if (!disposed) setReplayActive(active)
        })
        .catch(() => {
          // ignore transient health failures
        })
    }, 3000)

    return () => {
      disposed = true
      window.clearInterval(id)
    }
  }, [])

  async function runControl(payload: Record<string, unknown>) {
    setBusy(true)
    setError(null)
    try {
      await postControl(payload)
      setMessage(String(payload.action ?? 'updated'))
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Control failed')
    } finally {
      setBusy(false)
    }
  }

  async function runReplay(payload: Record<string, unknown>) {
    setBusy(true)
    setError(null)
    try {
      const result = await postReplayControl(payload)
      setReplayActive(Boolean(result.replayActive))
      setMessage(payload.action === 'start' ? 'replay started' : 'replay stopped')
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Replay control failed')
    } finally {
      setBusy(false)
    }
  }

  const inputClass =
    'h-8 w-full border border-[color:var(--color-border-subtle)] bg-black px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none focus:border-[color:var(--color-accent)]'

  return (
    <Card>
      <CardHeader
        title="Simulation Control"
        subtitle={busy ? 'working' : replayActive ? 'replay' : 'simulation'}
      />
      <CardBody>
        <div className="space-y-3.5">
          <div className="grid grid-cols-2 border border-[color:var(--color-border-subtle)] text-[11px] text-[color:var(--color-text-secondary)]">
            <Metric label="Status" value={simulation?.enabled ? (simulation.paused ? 'paused' : 'running') : 'off'} />
            <Metric label="Clock" value={simulation?.clockMode ?? '--'} />
            <Metric label="Speed" value={`${simulation?.speed?.toFixed(1) ?? '--'}x`} />
            <Metric label="Vol" value={simulation?.volatility ?? '--'} />
            <Metric label="Regime" value={simulation?.regime ?? '--'} />
            <Metric label="Limit/ms" value={formatLambda(simulation?.limitLambda)} />
            <Metric label="Mkt/ms" value={formatLambda(simulation?.marketableLambda)} />
          </div>

          {error ? (
            <div className="rounded-sm border border-[color:var(--color-sell)] bg-[color:var(--color-sell-dim)] px-2 py-1.5 text-[11px] text-[color:var(--color-sell)]">
              {error}
            </div>
          ) : message ? (
            <div className="rounded-sm border border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5 text-[11px] text-[color:var(--color-text-secondary)]">
              {message}
            </div>
          ) : null}

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

          <Section title="Timing">
            <form
              key={`${simulation?.clockMode}-${simulation?.speed}`}
              className="space-y-2"
              onSubmit={(event) => {
                event.preventDefault()
                runControl({
                  action: 'set_timing',
                  clockMode: String(new FormData(event.currentTarget).get('clockMode') ?? 'realtime'),
                  speed: formNumber(event.currentTarget, 'speed'),
                })
              }}
            >
              <div className="grid grid-cols-[minmax(0,1fr)_5.5rem] gap-2">
                <SelectField
                  label="Clock"
                  name="clockMode"
                  defaultValue={simulation?.clockMode === 'accelerated' ? 'accelerated' : 'realtime'}
                  disabled={disabled}
                  options={[
                    ['realtime', 'Realtime'],
                    ['accelerated', 'Accelerated'],
                  ]}
                />
                <NumberField
                  label="Speed"
                  name="speed"
                  defaultValue={num(simulation?.speed, 1)}
                  step={0.5}
                  min={0.1}
                  disabled={disabled}
                />
              </div>
              <Button className="w-full" variant="secondary" disabled={disabled} type="submit">
                Apply Timing
              </Button>
            </form>
          </Section>

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

          <Section title="Scenario">
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
          </Section>

          <Section title="Replay">
            <form
              className="space-y-2"
              onSubmit={(event) => {
                event.preventDefault()
                const form = event.currentTarget
                runReplay({
                  action: 'start',
                  replayFile: String(new FormData(form).get('replayFile') ?? ''),
                  speed: formNumber(form, 'replaySpeed'),
                  loop: Boolean(new FormData(form).get('replayLoop')),
                  loopPauseMs: formNumber(form, 'loopPauseMs'),
                })
              }}
            >
              <label className="space-y-1">
                <span className="block text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
                  Replay file
                </span>
                <input
                  className={inputClass}
                  name="replayFile"
                  defaultValue="data\\sample_orders_with_clients.csv"
                  disabled={disabled}
                />
              </label>
              <div className="grid grid-cols-[5.5rem_5.5rem_minmax(0,1fr)] gap-2">
                <NumberField label="Speed" name="replaySpeed" defaultValue={10} step={1} min={0.1} disabled={disabled} />
                <NumberField label="Pause" name="loopPauseMs" defaultValue={1000} step={100} min={0} disabled={disabled} />
                <label className="flex items-end gap-2 pb-2 text-[11px] text-[color:var(--color-text-secondary)]">
                  <input className="h-3.5 w-3.5" type="checkbox" name="replayLoop" disabled={disabled} />
                  Loop
                </label>
              </div>
              <div className="grid grid-cols-2 gap-2">
                <Button variant="secondary" disabled={disabled || replayActive} type="submit">
                  Start Replay
                </Button>
                <Button
                  variant="secondary"
                  disabled={busy || !replayActive}
                  type="button"
                  onClick={() => runReplay({ action: 'stop' })}
                >
                  Stop Replay
                </Button>
              </div>
            </form>
          </Section>

          <Section title="Agent Mix">
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
          </Section>

          <Section title="Market Maker">
          <form
            key={`${marketMaker?.levels}-${marketMaker?.quoteQuantity}-${marketMaker?.minQuantity}-${marketMaker?.baseSpreadTicks}-${marketMaker?.toxicitySensitivity}-${marketMaker?.wakeIntervalMs}-${marketMaker?.inventorySkewDivisor}`}
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
                  inventorySkewDivisor: formNumber(event.currentTarget, 'inventorySkewDivisor'),
                },
              })
            }}
          >
            <div className="grid grid-cols-3 gap-1.5">
              <NumberField
                label="Levels"
                name="levels"
                defaultValue={num(marketMaker?.levels)}
                disabled={disabled}
              />
              <NumberField
                label="Qty"
                name="quoteQuantity"
                defaultValue={num(marketMaker?.quoteQuantity)}
                disabled={disabled}
              />
              <NumberField
                label="Spread"
                name="baseSpreadTicks"
                defaultValue={num(marketMaker?.baseSpreadTicks)}
                disabled={disabled}
              />
              <NumberField
                label="Min"
                name="minQuantity"
                defaultValue={num(marketMaker?.minQuantity)}
                disabled={disabled}
              />
              <NumberField
                label="Toxic"
                name="toxicitySensitivity"
                defaultValue={num(marketMaker?.toxicitySensitivity, 1)}
                step={0.1}
                disabled={disabled}
              />
              <NumberField
                label="Wake"
                name="wakeIntervalMs"
                defaultValue={num(marketMaker?.wakeIntervalMs)}
                disabled={disabled}
              />
              <NumberField
                label="Skew"
                name="inventorySkewDivisor"
                defaultValue={num(marketMaker?.inventorySkewDivisor, 50)}
                disabled={disabled}
              />
            </div>
            <Button className="w-full" variant="secondary" disabled={disabled} type="submit">
              Apply Market Maker
            </Button>
          </form>
          </Section>

          <Section title="Agent Attribution">
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
          </Section>
        </div>
      </CardBody>
    </Card>
  )
}

function Section({ title, children }: { title: string; children: ReactNode }) {
  return (
    <section className="space-y-2 border-t border-[color:var(--color-border-subtle)] pt-3 first:border-t-0 first:pt-0">
      <div className="border-l-2 border-[color:var(--color-accent)] pl-2 text-[10.5px] font-bold uppercase text-[color:var(--color-text-muted)]">
        {title}
      </div>
      {children}
    </section>
  )
}

function Metric({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="terminal-row flex justify-between bg-black px-2 py-1.5">
      <span>{label}</span>
      <span className="num truncate pl-2 font-bold text-[color:var(--color-text-primary)]">{value}</span>
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
        className="h-8 w-full border border-[color:var(--color-border-subtle)] bg-black px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none focus:border-[color:var(--color-accent)]"
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

function SelectField({
  label,
  name,
  defaultValue,
  disabled,
  options,
}: {
  label: string
  name: string
  defaultValue: string
  disabled: boolean
  options: Array<readonly [string, string]>
}) {
  return (
    <label className="space-y-1">
      <span className="block text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">{label}</span>
      <select
        className="h-8 w-full border border-[color:var(--color-border-subtle)] bg-black px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none focus:border-[color:var(--color-accent)]"
        name={name}
        defaultValue={defaultValue}
        disabled={disabled}
      >
        {options.map(([id, optionLabel]) => (
          <option key={id} value={id}>
            {optionLabel}
          </option>
        ))}
      </select>
    </label>
  )
}

function NumberField({
  label,
  name,
  defaultValue,
  step = 1,
  min = 0,
  disabled,
}: {
  label: string
  name: string
  defaultValue: number
  step?: number
  min?: number
  disabled: boolean
}) {
  return (
    <label className="space-y-1">
      <span className="block text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">{label}</span>
      <input
        className="h-8 w-full border border-[color:var(--color-border-subtle)] bg-black px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none focus:border-[color:var(--color-accent)]"
        type="number"
        min={min}
        name={name}
        step={step}
        disabled={disabled}
        defaultValue={defaultValue}
      />
    </label>
  )
}
