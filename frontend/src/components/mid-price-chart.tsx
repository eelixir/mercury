import { CandlestickSeries, createChart, type IChartApi, type ISeriesApi } from 'lightweight-charts'
import { useEffect, useMemo, useRef } from 'react'
import { Card, CardBody, CardHeader } from './ui/card'
import { formatPrice } from '../lib/format'
import { useActiveBucket, useActiveSymbol } from '../store/market-data-store'

export function MidPriceChart() {
  const containerRef = useRef<HTMLDivElement | null>(null)
  const chartRef = useRef<IChartApi | null>(null)
  const seriesRef = useRef<ISeriesApi<'Candlestick'> | null>(null)
  const didFitInitialRangeRef = useRef(false)
  const bucket = useActiveBucket()
  const activeSymbol = useActiveSymbol()
  const chartPoints = bucket.chartPoints
  const stats = bucket.stats

  const candles = useMemo(() => {
    const byTime = new Map<number, { open: number; high: number; low: number; close: number }>()
    for (const point of chartPoints) {
      const t = Math.floor(point.timestamp / 1000)
      byTime.set(t, {
        open: point.open,
        high: point.high,
        low: point.low,
        close: point.close,
      })
    }
    return Array.from(byTime.entries())
      .sort(([a], [b]) => a - b)
      .map(([time, value]) => ({ time: time as never, ...value }))
  }, [chartPoints])

  const first = chartPoints[0]?.open
  const last = chartPoints[chartPoints.length - 1]?.close
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
        textColor: '#8f918a',
        fontFamily: "'Cascadia Code', 'JetBrains Mono', monospace",
        fontSize: 10,
      },
      grid: {
        vertLines: { color: 'rgba(255, 255, 255, 0.032)' },
        horzLines: { color: 'rgba(255, 255, 255, 0.032)' },
      },
      rightPriceScale: {
        borderColor: 'rgba(255, 255, 255, 0.14)',
      },
      timeScale: {
        borderColor: 'rgba(255, 255, 255, 0.14)',
        timeVisible: true,
        secondsVisible: true,
      },
      handleScroll: {
        mouseWheel: true,
        pressedMouseMove: true,
        horzTouchDrag: true,
        vertTouchDrag: false,
      },
      handleScale: {
        axisPressedMouseMove: true,
        mouseWheel: true,
        pinch: true,
      },
      crosshair: {
        vertLine: { color: 'rgba(0, 229, 177, 0.42)', width: 1, style: 3 },
        horzLine: { color: 'rgba(0, 229, 177, 0.42)', width: 1, style: 3 },
      },
    })

    const series = chart.addSeries(CandlestickSeries, {
      upColor: '#438b5a',
      downColor: '#c5553f',
      borderUpColor: '#438b5a',
      borderDownColor: '#c5553f',
      wickUpColor: '#6fa77c',
      wickDownColor: '#d06a56',
      lastValueVisible: true,
      priceLineVisible: true,
      priceLineColor: 'rgba(78, 154, 98, 0.48)',
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
    seriesRef.current.setData(candles)

    if (candles.length === 0) {
      didFitInitialRangeRef.current = false
      return
    }

    if (!didFitInitialRangeRef.current) {
      chartRef.current?.timeScale().fitContent()
      didFitInitialRangeRef.current = true
    }
  }, [candles])

  useEffect(() => {
    didFitInitialRangeRef.current = false
  }, [activeSymbol])

  return (
    <Card className="terminal-grid">
      <CardHeader
        title={`Chart | ${activeSymbol}`}
        subtitle="Candles: 1s"
        actions={
          <div className="flex items-center gap-2">
            <span className="num text-[11px] font-semibold text-[color:var(--color-text-primary)]">
              {formatPrice(stats?.midPrice ?? null)}
            </span>
            <span className={`num text-[10px] font-semibold ${deltaClass}`}>
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
