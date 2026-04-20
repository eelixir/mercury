import type { PropsWithChildren, ReactNode } from 'react'
import { cn } from '../../lib/utils'

export function Card({
  children,
  className,
}: PropsWithChildren<{ className?: string }>) {
  return <section className={cn('panel', className)}>{children}</section>
}

export function CardHeader({
  title,
  subtitle,
  actions,
  className,
}: {
  title: ReactNode
  subtitle?: ReactNode
  actions?: ReactNode
  className?: string
}) {
  return (
    <div className={cn('panel-header', className)}>
      <div className="flex items-baseline gap-2 min-w-0">
        <span className="panel-title truncate">{title}</span>
        {subtitle ? <span className="panel-subtitle truncate">{subtitle}</span> : null}
      </div>
      {actions ? <div className="flex items-center gap-2 shrink-0">{actions}</div> : null}
    </div>
  )
}

export function CardBody({
  children,
  className,
  flush,
}: PropsWithChildren<{ className?: string; flush?: boolean }>) {
  return (
    <div className={cn(flush ? 'panel-body-flush' : 'panel-body', className)}>
      {children}
    </div>
  )
}
