import { useState } from 'react'
import { useActiveBucket } from '../store/market-data-store'
import { Button } from './ui/button'
import { Card, CardBody, CardHeader } from './ui/card'

async function postControl(action: string, volatility?: string) {
  const response = await fetch('/api/simulation/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action, volatility }),
  })

  if (!response.ok) {
    const text = await response.text()
    throw new Error(text || `HTTP ${response.status}`)
  }
}

export function SimulationControls() {
  const simulation = useActiveBucket().simulation
  const [busy, setBusy] = useState(false)

  const disabled = !simulation?.enabled || busy
  const pauseLabel = simulation?.paused ? 'Resume' : 'Pause'

  return (
    <Card>
      <CardHeader title="Simulation" subtitle="Operator" />
      <CardBody>
        <div className="space-y-3">
          <div className="grid grid-cols-2 gap-2 text-[11px] text-[color:var(--color-text-secondary)]">
            <div className="flex justify-between rounded-sm bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5">
              <span>Status</span>
              <span className="num text-[color:var(--color-text-primary)]">
                {simulation?.enabled ? (simulation.paused ? 'paused' : 'running') : 'off'}
              </span>
            </div>
            <div className="flex justify-between rounded-sm bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5">
              <span>Clock</span>
              <span className="num text-[color:var(--color-text-primary)]">{simulation?.clockMode ?? '--'}</span>
            </div>
            <div className="flex justify-between rounded-sm bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5">
              <span>Speed</span>
              <span className="num text-[color:var(--color-text-primary)]">{simulation?.speed ?? '--'}x</span>
            </div>
            <div className="flex justify-between rounded-sm bg-[color:var(--color-bg-panel-alt)] px-2 py-1.5">
              <span>Vol</span>
              <span className="num text-[color:var(--color-text-primary)]">{simulation?.volatility ?? '--'}</span>
            </div>
          </div>

          <div className="grid grid-cols-2 gap-2">
            <Button
              variant="default"
              disabled={disabled}
              onClick={async () => {
                setBusy(true)
                try {
                  await postControl(simulation?.paused ? 'resume' : 'pause')
                } finally {
                  setBusy(false)
                }
              }}
            >
              {pauseLabel}
            </Button>

            <Button
              variant="default"
              disabled={disabled}
              onClick={async () => {
                setBusy(true)
                try {
                  await postControl('restart')
                } finally {
                  setBusy(false)
                }
              }}
            >
              Restart
            </Button>
          </div>

          <div className="space-y-1">
            <label className="text-[10.5px] uppercase tracking-wider text-[color:var(--color-text-muted)]">
              Volatility
            </label>
            <select
              className="h-9 w-full rounded-sm border border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel-alt)] px-3 text-[12px] text-[color:var(--color-text-primary)] outline-none"
              disabled={disabled}
              value={simulation?.volatility ?? 'normal'}
              onChange={async (event) => {
                setBusy(true)
                try {
                  await postControl('set_volatility', event.target.value)
                } finally {
                  setBusy(false)
                }
              }}
            >
              <option value="low">Low</option>
              <option value="normal">Normal</option>
              <option value="high">High</option>
            </select>
          </div>
        </div>
      </CardBody>
    </Card>
  )
}
