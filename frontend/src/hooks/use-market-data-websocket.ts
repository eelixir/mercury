import { useEffect } from 'react'
import type { MarketEnvelope } from '../lib/types'
import { useMarketDataStore } from '../store/market-data-store'

const BASE_RECONNECT_MS = 500
const MAX_RECONNECT_MS = 5000

function normalizeWebSocketUrl(rawUrl: string): string {
  if (rawUrl.startsWith('ws://') || rawUrl.startsWith('wss://')) {
    return rawUrl
  }

  if (rawUrl.startsWith('http://')) {
    return `ws://${rawUrl.slice('http://'.length)}`
  }

  if (rawUrl.startsWith('https://')) {
    return `wss://${rawUrl.slice('https://'.length)}`
  }

  return rawUrl
}

function resolveMarketDataWebSocketUrl(): string {
  const configuredUrl = import.meta.env.VITE_MERCURY_WS_URL
  if (typeof configuredUrl === 'string' && configuredUrl.length > 0) {
    return normalizeWebSocketUrl(configuredUrl)
  }

  if (import.meta.env.DEV) {
    return 'ws://127.0.0.1:9001/ws/market'
  }

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${protocol}//${window.location.host}/ws/market`
}

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
      ws = new WebSocket(resolveMarketDataWebSocketUrl())

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
