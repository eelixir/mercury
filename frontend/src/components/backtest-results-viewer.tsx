import { useMemo, useState } from 'react'
import { Card, CardBody, CardHeader } from './ui/card'

type CsvRow = Record<string, string>

interface LoadedArtifacts {
  summary: Record<string, unknown> | null
  stats: CsvRow[]
  pnl: CsvRow[]
  trades: CsvRow[]
  simState: CsvRow[]
  agentSummary: CsvRow[]
  sweepSummary: CsvRow[]
}

const EMPTY: LoadedArtifacts = {
  summary: null,
  stats: [],
  pnl: [],
  trades: [],
  simState: [],
  agentSummary: [],
  sweepSummary: [],
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

function metric(summary: Record<string, unknown> | null, key: string): number {
  return toNumber(summary?.[key])
}

function makeSeries(rows: CsvRow[], xKey: string, yKey: string) {
  return rows
    .map((row) => ({ x: toNumber(row[xKey]), y: toNumber(row[yKey]) }))
    .filter((point) => point.y !== 0)
    .slice(-180)
}

export function BacktestResultsViewer() {
  const [artifacts, setArtifacts] = useState<LoadedArtifacts>(EMPTY)
  const [loadedNames, setLoadedNames] = useState<string[]>([])

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
  }

  const summary = artifacts.summary
  const queue = (summary?.queueAnalytics ?? {}) as Record<string, unknown>

  return (
    <div className="grid h-full min-h-0 grid-cols-1 gap-1.5 p-1.5 xl:grid-cols-[22rem_minmax(0,1fr)]">
      <div className="flex min-h-0 flex-col gap-1.5">
        <Card>
          <CardHeader title="Backtest Artifacts" subtitle="Import" />
          <CardBody>
            <div className="space-y-3">
              <input
                className="block w-full text-[12px] text-[color:var(--color-text-secondary)] file:mr-3 file:rounded-sm file:border-0 file:bg-[color:var(--color-accent)] file:px-3 file:py-1.5 file:text-xs file:font-semibold file:uppercase file:tracking-wider file:text-white"
                type="file"
                multiple
                accept=".json,.csv"
                onChange={(event) => loadFiles(event.target.files)}
              />
              <div className="max-h-24 overflow-auto rounded-sm bg-[color:var(--color-bg-panel-alt)] p-2 text-[11px] text-[color:var(--color-text-muted)]">
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

        <Card className="min-h-0 flex-1">
          <CardHeader title="Agents" subtitle={`${artifacts.agentSummary.length} rows`} />
          <CardBody flush>
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
                  {artifacts.agentSummary.slice(0, 80).map((agent) => (
                    <tr key={`${agent.symbol}-${agent.clientId}`} className="border-t border-[color:var(--color-border-subtle)]">
                      <td className="px-2 py-1 text-[color:var(--color-text-secondary)]">
                        {agent.agentType} #{agent.clientId}
                      </td>
                      <td className="num px-2 py-1 text-right">{agent.totalPnL}</td>
                      <td className="num px-2 py-1 text-right">
                        {toNumber(agent.averageFillProbability).toFixed(2)}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </CardBody>
        </Card>
      </div>

      <div className="grid min-h-0 grid-rows-2 gap-1.5">
        <div className="grid min-h-0 grid-cols-1 gap-1.5 lg:grid-cols-2">
          <ChartCard title="Mid Price" points={midSeries} />
          <ChartCard title="Spread" points={spreadSeries} />
        </div>
        <div className="grid min-h-0 grid-cols-1 gap-1.5 lg:grid-cols-3">
          <ChartCard title="PnL" points={pnlSeries} />
          <ChartCard title="Inventory" points={inventorySeries} />
          <ChartCard title="Toxicity" points={toxicitySeries} />
        </div>
      </div>
    </div>
  )
}

function Metric({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="flex justify-between rounded-sm bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5 text-[color:var(--color-text-secondary)]">
      <span>{label}</span>
      <span className="num truncate pl-2 text-[color:var(--color-text-primary)]">{value}</span>
    </div>
  )
}

function ChartCard({ title, points }: { title: string; points: Array<{ x: number; y: number }> }) {
  return (
    <Card>
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
    <div className="h-full min-h-[11rem]">
      <svg className="h-full w-full" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none">
        <path d={path} fill="none" stroke="var(--color-accent)" strokeWidth="2" vectorEffect="non-scaling-stroke" />
      </svg>
      <div className="mt-1 flex justify-between text-[10px] text-[color:var(--color-text-muted)]">
        <span className="num">{minY.toFixed(2)}</span>
        <span className="num">{maxY.toFixed(2)}</span>
      </div>
    </div>
  )
}
