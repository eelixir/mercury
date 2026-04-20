import type { ReactNode } from 'react'
import { cn } from '../../lib/utils'

export function Badge({
  children,
  tone = 'neutral',
  dot = false,
}: {
  children: ReactNode
  tone?: 'neutral' | 'buy' | 'sell' | 'live' | 'warn'
  dot?: boolean
}) {
  const toneClass = {
    neutral: 'text-[color:var(--color-text-secondary)] bg-white/[0.03] border-[color:var(--color-border-subtle)]',
    buy: 'text-[color:var(--color-buy)] bg-[color:var(--color-buy-dim)] border-transparent',
    sell: 'text-[color:var(--color-sell)] bg-[color:var(--color-sell-dim)] border-transparent',
    live: 'text-[color:var(--color-buy)] bg-[color:var(--color-buy-dim)] border-transparent',
    warn: 'text-[color:var(--color-warn)] bg-[color:var(--color-warn)]/10 border-transparent',
  }[tone]

  const dotColor = {
    neutral: 'bg-slate-500',
    buy: 'bg-[color:var(--color-buy)]',
    sell: 'bg-[color:var(--color-sell)]',
    live: 'bg-[color:var(--color-buy)]',
    warn: 'bg-[color:var(--color-warn)]',
  }[tone]

  return (
    <span
      className={cn(
        'inline-flex items-center gap-1.5 rounded-sm border px-1.5 py-[1px] text-[10px] font-semibold uppercase tracking-wider',
        toneClass,
      )}
    >
      {dot ? <span className={cn('inline-block h-1.5 w-1.5 rounded-full pulse-dot', dotColor)} /> : null}
      {children}
    </span>
  )
}
