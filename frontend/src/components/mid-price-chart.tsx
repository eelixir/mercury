import { createChart, LineSeries, type IChartApi, type ISeriesApi } from 'lightweight-charts'
import { useEffect, useMemo, useRef } from 'react'
import { Card, CardBody, CardHeader } from './ui/card'
import { formatPrice } from '../lib/format'
import { useMarketDataStore } from '../store/market-data-store'

export function MidPriceChart() {
  const containerRef = useRef<HTMLDivElement | null>(null)
  const chartRef = useRef<IChartApi | null>(null)
  const seriesRef = useRef<ISeriesApi<'Line'> | null>(null)
  const chartPoints = useMarketDataStore((state) => state.chartPoints)
  const stats = useMarketDataStore((state) => state.stats)

  const deferredPoints = useMemo(() => {
    // lightweight-charts requires strictly ascending `time`. Our chartPoints
    // can arrive multiple-per-second and aren't guaranteed to be sorted, so
    // bucket to the nearest second and keep the last value per bucket.
    const byTime = new Map<number, number>()
    for (const point of chartPoints) {
      const t = Math.floor(point.timestamp / 1000)
      byTime.set(t, point.value)
    }
    return Array.from(byTime.entries())
      .sort(([a], [b]) => a - b)
      .map(([time, value]) => ({ time: time as never, value }))
  }, [chartPoints])

  const first = chartPoints[0]?.value
  const last = chartPoints[chartPoints.length - 1]?.value
  const delta = first !== undefined && last !== undefined ? last - first : 0
  const deltaPct = first ? (delta / first) * 100 : 0
  const deltaClass =
    delta > 0
      ? 'text-[color:var(--color-buy)]'
      : delta < 0
        ? 'text-[color:var(--color-sell)]'
        : 'text-[color:var(--color-text-secondary)]'

  useEffect(() => {
    if (!containerRef.current) return

    const chart = createChart(containerRef.current, {
      autoSize: true,
      layout: {
        background: { color: 'transparent' },
        textColor: '#8b94a4',
        fontFamily: "'JetBrains Mono', 'IBM Plex Mono', monospace",
        fontSize: 10,
      },
      grid: {
        vertLines: { color: 'rgba(148, 163, 184, 0.05)' },
        horzLines: { color: 'rgba(148, 163, 184, 0.05)' },
      },
      rightPriceScale: {
        borderColor: 'rgba(148, 163, 184, 0.12)',
      },
      timeScale: {
        borderColor: 'rgba(148, 163, 184, 0.12)',
        timeVisible: true,
        secondsVisible: true,
      },
      crosshair: {
        vertLine: { color: 'rgba(148, 163, 184, 0.3)', width: 1, style: 3 },
        horzLine: { color: 'rgba(148, 163, 184, 0.3)', width: 1, style: 3 },
      },
    })

    const series = chart.addSeries(LineSeries, {
      color: '#4f8cff',
      lineWidth: 2,
      lastValueVisible: true,
      priceLineVisible: true,
      priceLineColor: 'rgba(79, 140, 255, 0.4)',
      priceLineStyle: 2,
    })

    chartRef.current = chart
    seriesRef.current = series

    return () => {
      chart.remove()
      chartRef.current = null
      seriesRef.current = null
    }
  }, [])

  useEffect(() => {
    if (!seriesRef.current) return
    seriesRef.current.setData(deferredPoints)
  }, [deferredPoints])

  return (
    <Card>
      <CardHeader
        title="Mid-Price"
        subtitle="1s · Line"
        actions={
          <div className="flex items-center gap-3">
            <span className="num text-[12px] font-semibold text-[color:var(--color-text-primary)]">
              {formatPrice(stats?.midPrice ?? null)}
            </span>
            <span className={`num text-[11px] font-semibold ${deltaClass}`}>
              {delta >= 0 ? '+' : ''}
              {delta.toFixed(2)} ({deltaPct >= 0 ? '+' : ''}
              {deltaPct.toFixed(2)}%)
            </span>
          </div>
        }
      />
      <CardBody flush>
        <div ref={containerRef} className="h-full w-full" />
      </CardBody>
    </Card>
  )
}
