import { useEffect } from 'react'
import { MidPriceChart } from './components/mid-price-chart'
import { OrderBookLadder } from './components/order-book-ladder'
import { OrderEntryForm } from './components/order-entry-form'
import { PnLCard } from './components/pnl-card'
import { StatsStrip } from './components/stats-strip'
import { StatusBar } from './components/status-bar'
import { TopBar } from './components/top-bar'
import { TradeTape } from './components/trade-tape'
import { useMarketDataWebSocket } from './hooks/use-market-data-websocket'
import { useMarketDataStore } from './store/market-data-store'

function App() {
  useMarketDataWebSocket()
  const setActiveClientId = useMarketDataStore((state) => state.setActiveClientId)

  useEffect(() => {
    setActiveClientId(1)
  }, [setActiveClientId])

  return (
    <div className="flex h-screen flex-col bg-[color:var(--color-bg-base)] text-[color:var(--color-text-primary)]">
      <TopBar />

      <StatsStrip />

      <div className="grid flex-1 min-h-0 grid-cols-1 gap-1.5 p-1.5 xl:grid-cols-[20rem_minmax(0,1fr)_22rem]">
        <div className="flex min-h-0 flex-col gap-1.5">
          <OrderEntryForm />
          <PnLCard />
        </div>

        <div className="grid min-h-0 grid-rows-[minmax(0,1fr)_minmax(0,1.2fr)] gap-1.5">
          <MidPriceChart />
          <OrderBookLadder />
        </div>

        <TradeTape />
      </div>

      <StatusBar />
    </div>
  )
}

export default App
