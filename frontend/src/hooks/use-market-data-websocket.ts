import { useEffect } from 'react'
import type { MarketEnvelope } from '../lib/types'
import { useMarketDataStore } from '../store/market-data-store'

const BASE_RECONNECT_MS = 500
const MAX_RECONNECT_MS = 5000

export function useMarketDataWebSocket() {
  const applyEnvelope = useMarketDataStore((state) => state.applyEnvelope)
  const setConnectionState = useMarketDataStore((state) => state.setConnectionState)

  useEffect(() => {
    let ws: WebSocket | null = null
    let reconnectTimer: number | null = null
    let attempt = 0
    let disposed = false

    const connect = () => {
      if (disposed) return
      setConnectionState('connecting')
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
      ws = new WebSocket(`${protocol}//${window.location.host}/ws/market`)

      ws.addEventListener('open', () => {
        if (disposed) return
        attempt = 0
        setConnectionState('connected')
        ws?.send(JSON.stringify({ type: 'subscribe', depth: 20 }))
      })

      const scheduleReconnect = () => {
        if (disposed) return
        setConnectionState('disconnected')
        const delay = Math.min(MAX_RECONNECT_MS, BASE_RECONNECT_MS * 2 ** attempt)
        attempt += 1
        reconnectTimer = window.setTimeout(connect, delay)
      }

      ws.addEventListener('close', scheduleReconnect)
      ws.addEventListener('error', () => {
        try {
          ws?.close()
        } catch {
          // ignore
        }
      })

      ws.addEventListener('message', (event) => {
        try {
          const envelope = JSON.parse(String(event.data)) as MarketEnvelope
          const needsResync = applyEnvelope(envelope)
          if (needsResync && ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'subscribe', depth: 20 }))
          }
        } catch (err) {
          console.warn('Failed to handle market message', err)
        }
      })
    }

    connect()

    return () => {
      disposed = true
      if (reconnectTimer !== null) {
        window.clearTimeout(reconnectTimer)
      }
      if (ws) {
        try {
          ws.close()
        } catch {
          // ignore
        }
      }
    }
  }, [applyEnvelope, setConnectionState])
}
