import * as React from 'react'
import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '../../lib/utils'

const buttonVariants = cva(
  'inline-flex items-center justify-center border px-3 py-1.5 text-xs font-bold uppercase transition-colors duration-100 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-[color:var(--color-accent)] disabled:pointer-events-none disabled:opacity-50',
  {
    variants: {
      variant: {
        default:
          'border-[color:var(--color-accent)] bg-[color:var(--color-accent)] text-white hover:brightness-110',
        buy:
          'border-[color:var(--color-buy)] bg-[color:var(--color-buy)] text-white hover:brightness-110',
        sell:
          'border-[color:var(--color-sell)] bg-[color:var(--color-sell)] text-white hover:brightness-110',
        secondary:
          'border-[color:var(--color-border-strong)] bg-[color:var(--color-bg-panel-alt)] text-[color:var(--color-text-primary)] hover:border-[color:var(--color-accent)] hover:bg-[color:var(--color-accent-dim)] hover:text-[color:var(--color-accent)]',
        ghost:
          'border-transparent bg-transparent text-[color:var(--color-text-secondary)] hover:border-[color:var(--color-border-strong)] hover:text-[color:var(--color-accent)]',
        destructive:
          'border-[color:var(--color-sell)] bg-[color:var(--color-sell)] text-white hover:brightness-110',
      },
      size: {
        sm: 'px-2 py-1 text-[10px]',
        md: 'px-3 py-1.5 text-xs',
        lg: 'px-4 py-2 text-xs',
      },
    },
    defaultVariants: {
      variant: 'default',
      size: 'md',
    },
  },
)

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {}

export function Button({ className, variant, size, ...props }: ButtonProps) {
  return <button className={cn(buttonVariants({ variant, size }), className)} {...props} />
}
