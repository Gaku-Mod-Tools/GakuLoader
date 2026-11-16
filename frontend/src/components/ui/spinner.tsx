import { Loader2 } from 'lucide-react'
import { cn } from '@/lib/utils'

// Mirrors the ImGui manager's busy spinner (DrawSpinner) for waiting/failed rows.
export function Spinner({ className }: { className?: string }) {
  return <Loader2 className={cn('size-4 animate-spin text-primary', className)} aria-label="busy" />
}
