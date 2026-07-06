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
import { Button } from './components/ui/button'
import { useMarketDataWebSocket } from './hooks/use-market-data-websocket'
import { useMarketDataStore } from './store/market-data-store'

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

      <div className="flex items-center gap-1 border-b border-[color:var(--color-border-subtle)] bg-[color:var(--color-bg-panel)] px-1.5 py-1">
        <Button size="sm" variant={view === 'live' ? 'default' : 'ghost'} onClick={() => setView('live')}>
          Live
        </Button>
        <Button size="sm" variant={view === 'lab' ? 'default' : 'ghost'} onClick={() => setView('lab')}>
          Lab
        </Button>
      </div>

      {view === 'live' ? (
        <>
          <StatsStrip />

          <div className="grid flex-1 min-h-0 grid-cols-1 gap-1.5 p-1.5 xl:grid-cols-[20rem_minmax(0,1fr)_22rem]">
            <div className="flex min-h-0 flex-col gap-1.5">
              <OrderEntryForm />
              <PnLCard />
              <SimulationControls />
              <SystemHealth />
            </div>

            <div className="grid min-h-0 grid-rows-[minmax(0,1fr)_minmax(0,1.2fr)] gap-1.5">
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
