import { useEffect } from 'react'
import { unstable_batchedUpdates } from 'react-dom'
import type { MarketEnvelope } from '../lib/types'
import { useMarketDataStore } from '../store/market-data-store'

const BASE_RECONNECT_MS = 500
const MAX_RECONNECT_MS = 5000
const SUBSCRIBE_DEPTH = 20

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
  const prepareResync = useMarketDataStore((state) => state.prepareResync)

  useEffect(() => {
    let ws: WebSocket | null = null
    let reconnectTimer: number | null = null
    let frameTimer: number | null = null
    let attempt = 0
    let disposed = false
    let pendingEnvelopes: MarketEnvelope[] = []

    const flushPending = () => {
      frameTimer = null
      if (disposed || pendingEnvelopes.length === 0) return

      const batch = pendingEnvelopes
      pendingEnvelopes = []

      unstable_batchedUpdates(() => {
        for (const envelope of batch) {
          applyEnvelope(envelope)
        }
      })
    }

    const connect = () => {
      if (disposed) return

      // Drop any frames buffered from a prior socket so they cannot interleave
      // with the fresh connect snapshot.
      pendingEnvelopes = []
      if (frameTimer !== null) {
        window.cancelAnimationFrame(frameTimer)
        frameTimer = null
      }

      setConnectionState('connecting')
      ws = new WebSocket(resolveMarketDataWebSocketUrl())

      ws.addEventListener('open', () => {
        if (disposed) return
        attempt = 0
        // Reset sequence gate so the server's connect snapshot is always applied
        // (including when sequence equals the last pre-disconnect frame).
        prepareResync()
        setConnectionState('connected')
        try {
          ws?.send(JSON.stringify({ type: 'subscribe', depth: SUBSCRIBE_DEPTH }))
        } catch {
          // ignore
        }
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
          pendingEnvelopes.push(envelope)
          if (frameTimer === null) {
            frameTimer = window.requestAnimationFrame(flushPending)
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
      if (frameTimer !== null) {
        window.cancelAnimationFrame(frameTimer)
      }
      pendingEnvelopes = []
      if (ws) {
        try {
          ws.close()
        } catch {
          // ignore
        }
      }
    }
  }, [applyEnvelope, prepareResync, setConnectionState])
}
