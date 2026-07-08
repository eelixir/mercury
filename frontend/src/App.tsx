import { useEffect, useState } from 'react'
import { BacktestResultsViewer } from './components/backtest-results-viewer'
import { MidPriceChart } from './components/mid-price-chart'
import { OrderBookLadder } from './components/order-book-ladder'
import { OrderEntryForm } from './components/order-entry-form'
import { PnLCard } from './components/pnl-card'
import { SimulationControls } from './components/simulation-controls'
import { StatsStrip } from './components/stats-strip'
import { StatusBar } from './components/status-bar'
import { SystemHealth } from './components/system-health'
import { TopBar } from './components/top-bar'
import { TradeTape } from './components/trade-tape'
import { useMarketDataWebSocket } from './hooks/use-market-data-websocket'
import { useMarketDataStore } from './store/market-data-store'

function CommandRail({
  view,
  onViewChange,
}: {
  view: 'live' | 'lab'
  onViewChange: (view: 'live' | 'lab') => void
}) {
  const commands: Array<{ id: 'live' | 'lab'; label: string }> = [
    { id: 'live', label: 'Simulation' },
    { id: 'lab', label: 'Lab' },
  ]

  return (
    <div className="terminal-strip flex min-h-6 items-center px-1">
      <div className="flex min-w-0 items-center gap-0.5 overflow-x-auto">
        {commands.map((command) => {
          const active = view === command.id
          return (
            <button
              key={command.id}
              type="button"
              onClick={() => onViewChange(command.id)}
              className={`border px-2 py-[2px] text-left text-[10.5px] font-bold ${
                active
                  ? 'border-[color:var(--color-accent)] bg-[color:var(--color-accent)] text-white'
                  : 'border-transparent text-[color:var(--color-text-secondary)] hover:border-[color:var(--color-border-strong)] hover:text-[color:var(--color-text-primary)]'
              }`}
            >
              {command.label}
            </button>
          )
        })}
      </div>
    </div>
  )
}

function App() {
  useMarketDataWebSocket()
  const setActiveClientId = useMarketDataStore((state) => state.setActiveClientId)
  const [view, setView] = useState<'live' | 'lab'>('live')

  useEffect(() => {
    setActiveClientId(1)
  }, [setActiveClientId])

  return (
    <div className="flex h-screen flex-col bg-[color:var(--color-bg-base)] text-[color:var(--color-text-primary)]">
      <TopBar />
      <CommandRail view={view} onViewChange={setView} />

      {view === 'live' ? (
        <>
          <StatsStrip />

          <div className="grid flex-1 min-h-0 grid-cols-1 gap-1 p-1 xl:grid-cols-[22rem_minmax(0,1fr)_19rem]">
            <div className="flex min-h-0 flex-col gap-1 overflow-y-auto pr-1 [&>.panel]:shrink-0">
              <OrderEntryForm />
              <PnLCard />
              <SimulationControls />
              <SystemHealth />
            </div>

            <div className="grid min-h-0 grid-rows-[minmax(18rem,0.95fr)_minmax(22rem,1.05fr)] gap-1">
              <MidPriceChart />
              <OrderBookLadder />
            </div>

            <TradeTape />
          </div>
        </>
      ) : (
        <div className="flex-1 min-h-0">
          <BacktestResultsViewer />
        </div>
      )}

      <StatusBar />
    </div>
  )
}

export default App
