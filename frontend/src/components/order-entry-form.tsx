import { useState } from 'react'
import type { OrderResponse, OrderType, Side } from '../lib/types'
import { useMarketDataStore } from '../store/market-data-store'
import { Button } from './ui/button'
import { Card, CardBody, CardHeader } from './ui/card'
import { Input } from './ui/input'
import { Label } from './ui/label'

async function submitOrder(request: Record<string, unknown>): Promise<OrderResponse> {
  const response = await fetch('/api/orders', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(request),
  })

  const text = await response.text()
  try {
    return JSON.parse(text) as OrderResponse
  } catch {
    return {
      submittedOrderId: 0,
      orderType: (request.type as OrderResponse['orderType']) ?? 'limit',
      side: (request.side as OrderResponse['side']) ?? 'buy',
      tif: 'GTC',
      status: 'error',
      rejectReason: `HTTP ${response.status}: ${text.slice(0, 80) || 'no body'}`,
      orderId: 0,
      filledQuantity: 0,
      remainingQuantity: 0,
      message: '',
      trades: [],
    }
  }
}

const ORDER_TYPES: Array<{ value: OrderType; label: string }> = [
  { value: 'limit', label: 'Limit' },
  { value: 'market', label: 'Market' },
  { value: 'cancel', label: 'Cancel' },
  { value: 'modify', label: 'Modify' },
]

function SegmentedButton<T extends string>({
  value,
  options,
  onChange,
  toneFor,
}: {
  value: T
  options: Array<{ value: T; label: string }>
  onChange: (value: T) => void
  toneFor?: (v: T) => 'buy' | 'sell' | 'accent'
}) {
  return (
    <div className="flex overflow-hidden rounded-sm border border-[color:var(--color-border-subtle)]">
      {options.map((opt) => {
        const active = opt.value === value
        const tone = toneFor?.(opt.value)
        let activeBg = 'bg-[color:var(--color-accent)] text-white'
        if (tone === 'buy') activeBg = 'bg-[color:var(--color-buy)] text-white'
        if (tone === 'sell') activeBg = 'bg-[color:var(--color-sell)] text-white'
        return (
          <button
            key={opt.value}
            type="button"
            onClick={() => onChange(opt.value)}
            className={`flex-1 px-2 py-1.5 text-[11px] font-semibold uppercase tracking-wider transition-colors ${
              active
                ? activeBg
                : 'bg-[color:var(--color-bg-panel-alt)] text-[color:var(--color-text-secondary)] hover:text-[color:var(--color-text-primary)]'
            }`}
          >
            {opt.label}
          </button>
        )
      })}
    </div>
  )
}

function Field({
  label,
  children,
}: {
  label: string
  children: React.ReactNode
}) {
  return (
    <div className="space-y-1">
      <Label>{label}</Label>
      {children}
    </div>
  )
}

export function OrderEntryForm() {
  const setLastOrderResponse = useMarketDataStore((state) => state.setLastOrderResponse)
  const setActiveClientId = useMarketDataStore((state) => state.setActiveClientId)
  const lastResponse = useMarketDataStore((state) => state.lastOrderResponse)

  const [orderType, setOrderType] = useState<OrderType>('limit')
  const [side, setSide] = useState<Side>('buy')
  const [price, setPrice] = useState('100')
  const [quantity, setQuantity] = useState('10')
  const [clientId, setClientId] = useState('1')
  const [orderId, setOrderId] = useState('')
  const [newPrice, setNewPrice] = useState('')
  const [newQuantity, setNewQuantity] = useState('')
  const [submitting, setSubmitting] = useState(false)

  const isModify = orderType === 'modify'
  const isCancel = orderType === 'cancel'
  const needsPrice = orderType === 'limit'
  const needsQuantity = orderType === 'limit' || orderType === 'market'

  const submitVariant: 'buy' | 'sell' | 'default' =
    isCancel || isModify ? 'default' : side === 'buy' ? 'buy' : 'sell'
  const submitLabel = submitting
    ? 'Working…'
    : isCancel
      ? 'Cancel Order'
      : isModify
        ? 'Modify Order'
        : `${side === 'buy' ? 'Buy' : 'Sell'} · ${orderType.toUpperCase()}`

  return (
    <Card>
      <CardHeader title="Order Entry" subtitle="Manual" />
      <CardBody>
        <form
          className="space-y-2.5"
          onSubmit={async (event) => {
            event.preventDefault()
            setSubmitting(true)
            try {
              const nextClientId = Number(clientId || '1')
              setActiveClientId(nextClientId)

              const toNum = (s: string) => {
                const n = Number(s)
                return Number.isFinite(n) ? n : 0
              }
              const request: Record<string, unknown> = {
                type: orderType,
                side,
                clientId: Number.isFinite(nextClientId) ? nextClientId : 1,
                tif: 'GTC',
              }
              if (needsPrice) request.price = toNum(price)
              if (needsQuantity) request.quantity = toNum(quantity)
              if (isCancel || isModify) request.orderId = toNum(orderId)
              if (isModify && newPrice) request.newPrice = toNum(newPrice)
              if (isModify && newQuantity) request.newQuantity = toNum(newQuantity)

              try {
                const response = await submitOrder(request)
                setLastOrderResponse(response)
              } catch (err) {
                setLastOrderResponse({
                  submittedOrderId: 0,
                  orderType,
                  side,
                  tif: 'GTC',
                  status: 'error',
                  rejectReason: err instanceof Error ? err.message : 'Network error',
                  orderId: 0,
                  filledQuantity: 0,
                  remainingQuantity: 0,
                  message: '',
                  trades: [],
                })
              }
            } finally {
              setSubmitting(false)
            }
          }}
        >
          <SegmentedButton
            value={orderType}
            options={ORDER_TYPES}
            onChange={(v) => setOrderType(v)}
          />

          {!isCancel && !isModify ? (
            <SegmentedButton
              value={side}
              options={[
                { value: 'buy', label: 'Buy' },
                { value: 'sell', label: 'Sell' },
              ]}
              onChange={(v) => setSide(v)}
              toneFor={(v) => (v === 'buy' ? 'buy' : 'sell')}
            />
          ) : null}

          <div className="grid grid-cols-2 gap-2">
            <Field label="Price">
              <Input
                type="number"
                value={price}
                onChange={(e) => setPrice(e.target.value)}
                disabled={!needsPrice}
              />
            </Field>
            <Field label="Qty">
              <Input
                type="number"
                value={quantity}
                onChange={(e) => setQuantity(e.target.value)}
                disabled={!needsQuantity}
              />
            </Field>
          </div>

          <div className="grid grid-cols-2 gap-2">
            <Field label="Client ID">
              <Input
                type="number"
                value={clientId}
                onChange={(e) => setClientId(e.target.value)}
              />
            </Field>
            <Field label="Target Order">
              <Input
                type="number"
                value={orderId}
                onChange={(e) => setOrderId(e.target.value)}
                disabled={!isCancel && !isModify}
                placeholder={isCancel || isModify ? 'Order ID' : '—'}
              />
            </Field>
          </div>

          {isModify ? (
            <div className="grid grid-cols-2 gap-2">
              <Field label="New Price">
                <Input
                  type="number"
                  value={newPrice}
                  onChange={(e) => setNewPrice(e.target.value)}
                  placeholder="keep"
                />
              </Field>
              <Field label="New Qty">
                <Input
                  type="number"
                  value={newQuantity}
                  onChange={(e) => setNewQuantity(e.target.value)}
                  placeholder="keep"
                />
              </Field>
            </div>
          ) : null}

          <Button
            className="w-full"
            variant={submitVariant === 'default' ? 'default' : submitVariant}
            disabled={submitting}
          >
            {submitLabel}
          </Button>
        </form>

        {lastResponse ? (() => {
          const status = lastResponse.status || 'unknown'
          const isBad = status === 'rejected' || status === 'error'
          const filled = lastResponse.filledQuantity ?? 0
          const remaining = lastResponse.remainingQuantity ?? 0
          const id = lastResponse.orderId || lastResponse.submittedOrderId || 0
          return (
            <div className="mt-3 border-t border-[color:var(--color-border-subtle)] pt-2">
              <div className="flex items-center justify-between">
                <span className="text-[10px] font-semibold uppercase tracking-wider text-[color:var(--color-text-muted)]">
                  Last Response
                </span>
                <span
                  className={`num text-[11px] font-semibold ${
                    isBad
                      ? 'text-[color:var(--color-sell)]'
                      : 'text-[color:var(--color-buy)]'
                  }`}
                >
                  {status}
                </span>
              </div>
              <div className="mt-1 space-y-0.5 text-[11px] text-[color:var(--color-text-secondary)]">
                <div className="flex justify-between">
                  <span>ID</span>
                  <span className="num text-[color:var(--color-text-primary)]">#{id}</span>
                </div>
                <div className="flex justify-between">
                  <span>Filled</span>
                  <span className="num text-[color:var(--color-text-primary)]">
                    {filled} / {filled + remaining}
                  </span>
                </div>
                {lastResponse.rejectReason ? (
                  <div className="flex justify-between gap-2">
                    <span>Reject</span>
                    <span className="truncate text-[color:var(--color-sell)]">
                      {lastResponse.rejectReason}
                    </span>
                  </div>
                ) : null}
              </div>
            </div>
          )
        })() : null}
      </CardBody>
    </Card>
  )
}
