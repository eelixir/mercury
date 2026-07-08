import { useMemo, useState, type FormEvent } from 'react'
import { Button } from './ui/button'
import { Card, CardBody, CardHeader } from './ui/card'

type CellValue = string | number | boolean | null | undefined
type CsvRow = Record<string, CellValue>
type LabMode = 'backtest' | 'headless' | 'sweep' | 'calibrate_replay'

interface LoadedArtifacts {
  summary: Record<string, unknown> | null
  stats: CsvRow[]
  pnl: CsvRow[]
  trades: CsvRow[]
  simState: CsvRow[]
  agentSummary: CsvRow[]
  sweepSummary: CsvRow[]
  calibration: Record<string, unknown> | null
}

interface LabRunResponse {
  status: string
  mode: LabMode
  outputDir?: string
  summary?: Record<string, unknown>
  summaries?: Array<Record<string, unknown>>
  artifacts?: Partial<Pick<LoadedArtifacts, 'stats' | 'pnl' | 'trades' | 'simState' | 'agentSummary'>>
  sweepSummary?: CsvRow[]
  calibration?: Record<string, unknown> | null
}

const EMPTY: LoadedArtifacts = {
  summary: null,
  stats: [],
  pnl: [],
  trades: [],
  simState: [],
  agentSummary: [],
  sweepSummary: [],
  calibration: null,
}

function parseCsv(text: string): CsvRow[] {
  const rows: string[][] = []
  let row: string[] = []
  let cell = ''
  let quoted = false

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i]
    if (ch === '"') {
      if (quoted && text[i + 1] === '"') {
        cell += '"'
        i += 1
      } else {
        quoted = !quoted
      }
    } else if (ch === ',' && !quoted) {
      row.push(cell)
      cell = ''
    } else if ((ch === '\n' || ch === '\r') && !quoted) {
      if (ch === '\r' && text[i + 1] === '\n') i += 1
      row.push(cell)
      if (row.some((value) => value.length > 0)) rows.push(row)
      row = []
      cell = ''
    } else {
      cell += ch
    }
  }

  if (cell.length > 0 || row.length > 0) {
    row.push(cell)
    rows.push(row)
  }

  const [header, ...body] = rows
  if (!header) return []
  return body.map((values) =>
    Object.fromEntries(header.map((key, index) => [key, values[index] ?? ''])),
  )
}

function toNumber(value: unknown): number {
  const n = Number(value)
  return Number.isFinite(n) ? n : 0
}

function toFiniteNumber(value: unknown): number | null {
  if (value === null || value === undefined || value === '') {
    return null
  }
  const n = Number(value)
  return Number.isFinite(n) ? n : null
}

function metric(summary: Record<string, unknown> | null, key: string): number {
  return toNumber(summary?.[key])
}

function nestedNumber(source: Record<string, unknown> | null, group: string, key: string): number {
  const record = (source?.[group] ?? {}) as Record<string, unknown>
  return toNumber(record[key])
}

function makeSeries(rows: CsvRow[], xKey: string, yKey: string) {
  return rows
    .map((row) => {
      const x = toFiniteNumber(row[xKey])
      const y = toFiniteNumber(row[yKey])
      return x === null || y === null ? null : { x, y }
    })
    .filter((point): point is { x: number; y: number } => point !== null)
    .slice(-180)
}

function formString(form: FormData, key: string): string {
  return String(form.get(key) ?? '').trim()
}

function formNumber(form: FormData, key: string): number {
  return Number(form.get(key) ?? 0)
}

function withOptionalString(payload: Record<string, unknown>, key: string, value: string) {
  if (value.length > 0) {
    payload[key] = value
  }
}

function artifactsFromLabResponse(payload: LabRunResponse): LoadedArtifacts {
  if (payload.mode === 'sweep') {
    return {
      ...EMPTY,
      summary: payload.summaries?.[0] ?? null,
      sweepSummary: payload.sweepSummary ?? [],
    }
  }

  return {
    ...EMPTY,
    summary: payload.summary ?? null,
    stats: payload.artifacts?.stats ?? [],
    pnl: payload.artifacts?.pnl ?? [],
    trades: payload.artifacts?.trades ?? [],
    simState: payload.artifacts?.simState ?? [],
    agentSummary: payload.artifacts?.agentSummary ?? [],
    calibration: payload.calibration ?? null,
  }
}

export function BacktestResultsViewer() {
  const [artifacts, setArtifacts] = useState<LoadedArtifacts>(EMPTY)
  const [loadedNames, setLoadedNames] = useState<string[]>([])
  const [mode, setMode] = useState<LabMode>('backtest')
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)

  const pnlSeries = useMemo(() => makeSeries(artifacts.pnl, 'timestamp', 'totalPnL'), [artifacts.pnl])
  const inventorySeries = useMemo(() => makeSeries(artifacts.pnl, 'timestamp', 'netPosition'), [artifacts.pnl])
  const midSeries = useMemo(() => makeSeries(artifacts.stats, 'timestamp', 'midPrice'), [artifacts.stats])
  const spreadSeries = useMemo(() => makeSeries(artifacts.stats, 'timestamp', 'spread'), [artifacts.stats])
  const toxicitySeries = useMemo(
    () => makeSeries(artifacts.simState, 'simulationTimestamp', 'toxicityScore'),
    [artifacts.simState],
  )

  async function loadFiles(files: FileList | null) {
    if (!files) return
    const next: LoadedArtifacts = { ...EMPTY }
    const names: string[] = []

    for (const file of Array.from(files)) {
      const text = await file.text()
      names.push(file.name)
      if (file.name === 'summary.json') {
        next.summary = JSON.parse(text) as Record<string, unknown>
      } else if (file.name === 'calibration.json') {
        next.calibration = JSON.parse(text) as Record<string, unknown>
      } else if (file.name === 'stats.csv') {
        next.stats = parseCsv(text)
      } else if (file.name === 'pnl.csv') {
        next.pnl = parseCsv(text)
      } else if (file.name === 'trades.csv') {
        next.trades = parseCsv(text)
      } else if (file.name === 'sim_state.csv') {
        next.simState = parseCsv(text)
      } else if (file.name === 'agent_summary.csv') {
        next.agentSummary = parseCsv(text)
      } else if (file.name === 'sweep_summary.csv') {
        next.sweepSummary = parseCsv(text)
      }
    }

    setArtifacts(next)
    setLoadedNames(names.sort())
    setMessage('artifacts loaded')
    setError(null)
  }

  async function runLab(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    const form = new FormData(event.currentTarget)
    const requestedMode = formString(form, 'mode') as LabMode
    const payload: Record<string, unknown> = {
      mode: requestedMode,
      name: formString(form, 'name') || requestedMode,
      symbol: formString(form, 'symbol') || 'SIM',
      seed: formNumber(form, 'seed'),
      durationMs: formNumber(form, 'durationMs'),
      volatility: formString(form, 'volatility') || 'normal',
      marketMakerCount: formNumber(form, 'marketMakerCount'),
      momentumCount: formNumber(form, 'momentumCount'),
      meanReversionCount: formNumber(form, 'meanReversionCount'),
      noiseTraderCount: formNumber(form, 'noiseTraderCount'),
      replaySpeed: formNumber(form, 'replaySpeed'),
    }

    withOptionalString(payload, 'outputDir', formString(form, 'outputDir'))
    withOptionalString(payload, 'scenarioFile', formString(form, 'scenarioFile'))
    withOptionalString(payload, 'marketMakerConfigFile', formString(form, 'marketMakerConfigFile'))
    withOptionalString(payload, 'replayFile', formString(form, 'replayFile'))
    withOptionalString(payload, 'sweepFile', formString(form, 'sweepFile'))

    setBusy(true)
    setError(null)
    setMessage(null)

    try {
      const response = await fetch('/api/lab/run', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      })
      const text = await response.text()
      if (!response.ok) {
        throw new Error(text || `HTTP ${response.status}`)
      }

      const result = JSON.parse(text) as LabRunResponse
      setArtifacts(artifactsFromLabResponse(result))
      setLoadedNames([
        `api:${result.mode}`,
        result.outputDir ? `output:${result.outputDir}` : 'output:memory',
      ])
      setMessage(result.outputDir ? `run complete: ${result.outputDir}` : 'run complete')
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Lab run failed')
    } finally {
      setBusy(false)
    }
  }

  const summary = artifacts.summary
  const queue = (summary?.queueAnalytics ?? {}) as Record<string, unknown>

  return (
    <div className="grid h-full min-h-0 grid-cols-1 gap-1 p-1 xl:grid-cols-[23rem_minmax(0,1fr)]">
      <div className="flex min-h-0 flex-col gap-1 overflow-y-auto pr-1">
        <Card>
          <CardHeader title="Simulation Lab" subtitle={busy ? 'running' : 'local'} />
          <CardBody>
            <form className="space-y-3" onSubmit={runLab}>
              <div className="grid grid-cols-2 gap-2">
                <SelectField
                  label="Mode"
                  name="mode"
                  value={mode}
                  onChange={(value) => setMode(value as LabMode)}
                  options={[
                    ['backtest', 'Instant'],
                    ['headless', 'Headless'],
                    ['sweep', 'Sweep'],
                    ['calibrate_replay', 'Calibration'],
                  ]}
                />
                <TextField label="Name" name="name" defaultValue="ui-backtest" />
              </div>

              <div className="grid grid-cols-2 gap-2">
                <TextField label="Symbol" name="symbol" defaultValue="SIM" />
                <TextField label="Output" name="outputDir" defaultValue="runs\\ui-lab" />
              </div>

              <TextField
                label="Scenario"
                name="scenarioFile"
                defaultValue="scenarios\\calm-two-sided-market.json"
              />
              <TextField label="MM config" name="marketMakerConfigFile" placeholder="optional json" />

              <div className="grid grid-cols-3 gap-2">
                <NumberField label="Duration" name="durationMs" defaultValue={30000} min={1000} step={1000} />
                <NumberField label="Seed" name="seed" defaultValue={42} min={0} />
                <SelectField
                  label="Vol"
                  name="volatility"
                  defaultValue="normal"
                  options={[
                    ['low', 'Low'],
                    ['normal', 'Normal'],
                    ['high', 'High'],
                  ]}
                />
              </div>

              <div className="grid grid-cols-4 gap-1.5">
                <NumberField label="MM" name="marketMakerCount" defaultValue={2} />
                <NumberField label="Mom" name="momentumCount" defaultValue={2} />
                <NumberField label="MR" name="meanReversionCount" defaultValue={2} />
                <NumberField label="Noise" name="noiseTraderCount" defaultValue={1} />
              </div>

              {mode === 'sweep' ? (
                <TextField label="Sweep file" name="sweepFile" defaultValue="runs\\sweep.json" />
              ) : (
                <div className="grid grid-cols-[minmax(0,1fr)_6rem] gap-2">
                  <TextField
                    key={mode}
                    label={mode === 'calibrate_replay' ? 'Replay CSV' : 'Replay CSV'}
                    name="replayFile"
                    defaultValue={mode === 'calibrate_replay' ? 'data\\sample_orders_with_clients.csv' : ''}
                    placeholder="optional csv"
                  />
                  <NumberField label="Replay x" name="replaySpeed" defaultValue={10} min={0.1} step={1} />
                </div>
              )}

              {error ? (
                <div className="rounded-sm border border-[color:var(--color-sell)] bg-[color:var(--color-sell-dim)] px-2 py-1.5 text-[11px] text-[color:var(--color-sell)]">
                  {error}
                </div>
              ) : message ? (
                <div className="rounded-sm border border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5 text-[11px] text-[color:var(--color-text-secondary)]">
                  {message}
                </div>
              ) : null}

              <Button className="w-full" variant="default" disabled={busy} type="submit">
                {busy ? 'Running...' : 'Run Job'}
              </Button>
            </form>
          </CardBody>
        </Card>

        <Card>
          <CardHeader title="Run Artifacts" subtitle="Import" />
          <CardBody>
            <div className="space-y-3">
              <input
                className="block w-full text-[12px] text-[color:var(--color-text-secondary)] file:mr-3 file:rounded-sm file:border-0 file:bg-[color:var(--color-accent)] file:px-3 file:py-1.5 file:text-xs file:font-semibold file:uppercase file:tracking-wider file:text-white"
                type="file"
                multiple
                accept=".json,.csv"
                onChange={(event) => loadFiles(event.target.files)}
              />
              <div className="max-h-24 overflow-auto border border-[color:var(--color-border-subtle)] bg-black p-2 text-[11px] text-[color:var(--color-text-muted)]">
                {loadedNames.length === 0 ? 'No files loaded' : loadedNames.join(', ')}
              </div>
            </div>
          </CardBody>
        </Card>

        <Card>
          <CardHeader title="Run Summary" subtitle={String(summary?.name ?? '--')} />
          <CardBody>
            <div className="grid grid-cols-2 gap-2 text-[11px]">
              <Metric label="Trades" value={metric(summary, 'tradeCount')} />
              <Metric label="Volume" value={metric(summary, 'totalVolume')} />
              <Metric label="Final PnL" value={metric(summary, 'finalTotalPnL')} />
              <Metric label="Drawdown bps" value={metric(summary, 'maxDrawdownBps').toFixed(2)} />
              <Metric label="Avg queue" value={toNumber(queue.averageQueuePosition).toFixed(2)} />
              <Metric label="Fill prob" value={toNumber(queue.averageFillProbability).toFixed(2)} />
            </div>
          </CardBody>
        </Card>

        {artifacts.calibration ? (
          <Card>
            <CardHeader title="Calibration" subtitle="Replay" />
            <CardBody>
              <div className="grid grid-cols-2 gap-2 text-[11px]">
                <Metric label="Target orders" value={nestedNumber(artifacts.calibration, 'target', 'orderCount')} />
                <Metric label="Observed trades" value={nestedNumber(artifacts.calibration, 'observed', 'tradeCount')} />
                <Metric
                  label="Volume ratio"
                  value={nestedNumber(artifacts.calibration, 'comparison', 'volumeToReplayQuantity').toFixed(3)}
                />
                <Metric
                  label="Cancel ratio"
                  value={nestedNumber(artifacts.calibration, 'comparison', 'agentCancelToSubmitRatio').toFixed(3)}
                />
              </div>
            </CardBody>
          </Card>
        ) : null}

        <Card className="min-h-0 flex-1">
          <CardHeader
            title={artifacts.sweepSummary.length > 0 ? 'Sweep Runs' : 'Agents'}
            subtitle={`${artifacts.sweepSummary.length || artifacts.agentSummary.length} rows`}
          />
          <CardBody flush>
            {artifacts.sweepSummary.length > 0 ? (
              <SweepTable rows={artifacts.sweepSummary} />
            ) : (
              <AgentTable rows={artifacts.agentSummary} />
            )}
          </CardBody>
        </Card>
      </div>

      <div className="grid min-h-0 grid-rows-2 gap-1">
        <div className="grid min-h-0 grid-cols-1 gap-1 lg:grid-cols-2">
          <ChartCard title="Mid Price" points={midSeries} />
          <ChartCard title="Spread" points={spreadSeries} />
        </div>
        <div className="grid min-h-0 grid-cols-1 gap-1 lg:grid-cols-3">
          <ChartCard title="PnL" points={pnlSeries} />
          <ChartCard title="Inventory" points={inventorySeries} />
          <ChartCard title="Toxicity" points={toxicitySeries} />
        </div>
      </div>
    </div>
  )
}

function AgentTable({ rows }: { rows: CsvRow[] }) {
  return (
    <div className="h-full overflow-auto">
      <table className="w-full text-[11px]">
        <thead className="sticky top-0 bg-[color:var(--color-bg-header)] text-[color:var(--color-text-muted)]">
          <tr>
            <th className="px-2 py-1 text-left font-medium">Agent</th>
            <th className="px-2 py-1 text-right font-medium">PnL</th>
            <th className="px-2 py-1 text-right font-medium">Fill</th>
          </tr>
        </thead>
        <tbody>
          {rows.slice(0, 80).map((agent) => (
            <tr key={`${agent.symbol}-${agent.clientId}`} className="border-t border-[color:var(--color-border-subtle)]">
              <td className="px-2 py-1 text-[color:var(--color-text-secondary)]">
                {String(agent.agentType ?? '--')} #{String(agent.clientId ?? '--')}
              </td>
              <td className="num px-2 py-1 text-right">{agent.totalPnL ?? 0}</td>
              <td className="num px-2 py-1 text-right">
                {toNumber(agent.averageFillProbability).toFixed(2)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function SweepTable({ rows }: { rows: CsvRow[] }) {
  return (
    <div className="h-full overflow-auto">
      <table className="w-full text-[11px]">
        <thead className="sticky top-0 bg-[color:var(--color-bg-header)] text-[color:var(--color-text-muted)]">
          <tr>
            <th className="px-2 py-1 text-left font-medium">Run</th>
            <th className="px-2 py-1 text-right font-medium">Trades</th>
            <th className="px-2 py-1 text-right font-medium">PnL</th>
            <th className="px-2 py-1 text-right font-medium">Eff x</th>
          </tr>
        </thead>
        <tbody>
          {rows.slice(0, 80).map((row) => (
            <tr key={String(row.name)} className="border-t border-[color:var(--color-border-subtle)]">
              <td className="px-2 py-1 text-[color:var(--color-text-secondary)]">{row.name}</td>
              <td className="num px-2 py-1 text-right">{row.tradeCount}</td>
              <td className="num px-2 py-1 text-right">{row.finalTotalPnL}</td>
              <td className="num px-2 py-1 text-right">{toNumber(row.effectiveSpeed).toFixed(1)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function Metric({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="terminal-row flex justify-between bg-black px-2 py-1.5 text-[color:var(--color-text-secondary)]">
      <span>{label}</span>
      <span className="num truncate pl-2 font-bold text-[color:var(--color-text-primary)]">{value}</span>
    </div>
  )
}

function ChartCard({ title, points }: { title: string; points: Array<{ x: number; y: number }> }) {
  return (
    <Card className="terminal-grid">
      <CardHeader title={title} subtitle={`${points.length} pts`} />
      <CardBody>
        <LineChart points={points} />
      </CardBody>
    </Card>
  )
}

function LineChart({ points }: { points: Array<{ x: number; y: number }> }) {
  const width = 480
  const height = 180
  if (points.length < 2) {
    return <div className="flex h-full items-center justify-center text-[11px] text-[color:var(--color-text-muted)]">No series loaded</div>
  }

  const minX = Math.min(...points.map((point) => point.x))
  const maxX = Math.max(...points.map((point) => point.x))
  const minY = Math.min(...points.map((point) => point.y))
  const maxY = Math.max(...points.map((point) => point.y))
  const spanX = maxX - minX || 1
  const spanY = maxY - minY || 1
  const path = points
    .map((point, index) => {
      const x = ((point.x - minX) / spanX) * width
      const y = height - ((point.y - minY) / spanY) * height
      return `${index === 0 ? 'M' : 'L'}${x.toFixed(2)} ${y.toFixed(2)}`
    })
    .join(' ')

  return (
    <div className="flex h-full min-h-[11rem] flex-col">
      <svg className="min-h-0 flex-1 w-full" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none">
        <path d={path} fill="none" stroke="var(--color-accent)" strokeWidth="2" vectorEffect="non-scaling-stroke" />
      </svg>
      <div className="mt-1 flex justify-between text-[10px] text-[color:var(--color-text-muted)]">
        <span className="num">{minY.toFixed(2)}</span>
        <span className="num">{maxY.toFixed(2)}</span>
      </div>
    </div>
  )
}

function fieldClass() {
  return 'h-8 w-full border border-[color:var(--color-border-subtle)] bg-black px-2 text-[12px] text-[color:var(--color-text-primary)] outline-none focus:border-[color:var(--color-accent)]'
}

function TextField({
  label,
  name,
  defaultValue = '',
  placeholder,
}: {
  label: string
  name: string
  defaultValue?: string
  placeholder?: string
}) {
  return (
    <label className="space-y-1">
      <span className="block text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">{label}</span>
      <input className={fieldClass()} name={name} defaultValue={defaultValue} placeholder={placeholder} />
    </label>
  )
}

function NumberField({
  label,
  name,
  defaultValue,
  step = 1,
  min = 0,
}: {
  label: string
  name: string
  defaultValue: number
  step?: number
  min?: number
}) {
  return (
    <label className="space-y-1">
      <span className="block text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">{label}</span>
      <input className={fieldClass()} type="number" min={min} name={name} step={step} defaultValue={defaultValue} />
    </label>
  )
}

function SelectField({
  label,
  name,
  defaultValue,
  value,
  onChange,
  options,
}: {
  label: string
  name: string
  defaultValue?: string
  value?: string
  onChange?: (value: string) => void
  options: Array<readonly [string, string]>
}) {
  return (
    <label className="space-y-1">
      <span className="block text-[10px] uppercase tracking-wider text-[color:var(--color-text-muted)]">{label}</span>
      <select
        className={fieldClass()}
        name={name}
        value={value}
        defaultValue={value === undefined ? defaultValue : undefined}
        onChange={(event) => onChange?.(event.target.value)}
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
