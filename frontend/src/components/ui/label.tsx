import type { LabelHTMLAttributes } from 'react'
import { cn } from '../../lib/utils'

export function Label({ className, ...props }: LabelHTMLAttributes<HTMLLabelElement>) {
  return (
    <label
      className={cn(
        'text-[10px] font-semibold uppercase tracking-wider text-[color:var(--color-text-muted)]',
        className,
      )}
      {...props}
    />
  )
}
