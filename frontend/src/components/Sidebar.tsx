import { LayoutDashboard, Puzzle } from 'lucide-react'
import { cn } from '@/lib/utils'
import { Separator } from '@/components/ui/separator'
import { store, useStore, useT } from '@/state/store'

// Mirrors the ImGui left navigation: Manager entry, separator, Plugins label,
// then one selectable entry per registered plugin.
export function Sidebar() {
  const t = useT()
  const snapshot = useStore((s) => s.snapshot)
  const selectedPanel = useStore((s) => s.selectedPanel)
  const managerId = useStore((s) => s.shell?.managerPanelId ?? 'gakuloader.manager')
  const plugins = snapshot?.plugins ?? []
  const isManager = selectedPanel === managerId

  return (
    <nav className="flex w-[190px] shrink-0 flex-col gap-1 overflow-y-auto border-r bg-sidebar p-2 text-sidebar-foreground">
      <button
        type="button"
        onClick={() => void store.selectPanel(managerId)}
        className={cn(
          'flex items-center gap-2 rounded-md px-2.5 py-2 text-sm font-medium outline-none transition-colors focus-visible:ring-2 focus-visible:ring-ring',
          isManager ? 'bg-sidebar-accent text-sidebar-accent-foreground shadow-sm' : 'hover:bg-sidebar-accent/60',
        )}
      >
        <LayoutDashboard className="size-4 opacity-80" />
        {t('Manager')}
      </button>

      <Separator className="my-1.5 bg-sidebar-border" />
      <div className="flex items-center gap-1.5 px-2.5 pb-1 text-xs font-semibold uppercase tracking-wide text-muted-foreground">
        <Puzzle className="size-3.5" />
        {t('Plugins')}
      </div>

      {plugins.map((plugin) => {
        const selected = selectedPanel === plugin.id
        return (
          <button
            key={plugin.id}
            type="button"
            onClick={() => void store.selectPanel(plugin.id)}
            className={cn(
              'flex items-center gap-2 rounded-md px-2.5 py-2 text-left text-sm outline-none transition-colors focus-visible:ring-2 focus-visible:ring-ring',
              selected ? 'bg-sidebar-accent text-sidebar-accent-foreground shadow-sm' : 'hover:bg-sidebar-accent/60',
            )}
          >
            <span className="truncate">{plugin.name}</span>
          </button>
        )
      })}
    </nav>
  )
}
