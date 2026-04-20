export function formatPrice(value: number | null | undefined) {
  if (value === null || value === undefined) {
    return '--'
  }
  return value.toLocaleString()
}

export function formatSigned(value: number | null | undefined) {
  if (value === null || value === undefined) {
    return '--'
  }
  return `${value >= 0 ? '+' : ''}${value.toLocaleString()}`
}

export function formatClock(timestamp: number | null | undefined) {
  if (!timestamp) {
    return '--'
  }
  return new Date(timestamp).toLocaleTimeString()
}
